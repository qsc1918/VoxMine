#include "world.hpp"
#include "generator.hpp"
#include "mesher.hpp"
#include <cmath>
#include <algorithm>

World::World(uint32_t seed_) : seed(seed_) {
    queue_ = std::priority_queue<WorldTask, std::vector<WorldTask>,
                                 std::function<bool(const WorldTask&, const WorldTask&)>>(
        [](const WorldTask& a, const WorldTask& b) { return worldTaskLess(a, b); });
}

World::~World() { stopWorkers(); }

void World::startWorkers(int n) {
    if (!workers_.empty()) return;
    running_.store(true);
    for (int i = 0; i < n; i++)
        workers_.emplace_back([this] { workerLoop(); });
}

void World::stopWorkers() {
    if (workers_.empty()) return;
    running_.store(false);
    queueCV_.notify_all();
    for (auto& t : workers_) t.join();
    workers_.clear();
}

inline int floordiv(int a, int b) {
    int q = a / b, r = a % b;
    if (r < 0) { q--; }
    return q;
}

std::shared_ptr<Chunk> World::chunkAt(int cx, int cz) const {
    uint64_t key = chunkKey(cx, cz);
    for (int i = 0; i < kChunkCacheN; i++) {
        if (chunkCache_[i].first == key && chunkCache_[i].second) {
            return chunkCache_[i].second;
        }
    }
    std::lock_guard<std::mutex> lk(mapLock_);
    auto it = chunks_.find(key);
    auto result = it == chunks_.end() ? nullptr : it->second;
    if (result) {
        chunkCache_[chunkCacheIdx_ % kChunkCacheN] = {key, result};
        chunkCacheIdx_++;
    }
    return result;
}

void World::forEachChunk(const std::function<void(std::shared_ptr<Chunk>&, int, int)>& fn) {
    std::lock_guard<std::mutex> lk(mapLock_);
    for (auto& kv : chunks_) {
        int cx = (int)(int32_t)(kv.first >> 32);
        int cz = (int)(int32_t)kv.first;
        fn(kv.second, cx, cz);
    }
}

uint8_t World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= WORLD_HEIGHT) return B_AIR;
    int cx = floordiv(x, CHUNK_SIZE), cz = floordiv(z, CHUNK_SIZE);
    int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;
    auto c = chunkAt(cx, cz);
    if (!c || c->state.load() < 1) return B_AIR;
    return c->blocks[chunkIndex(lx, y, lz)];
}

bool World::setBlock(int x, int y, int z, uint8_t id) {
    if (y < 0 || y >= WORLD_HEIGHT) return false;
    int cx = floordiv(x, CHUNK_SIZE), cz = floordiv(z, CHUNK_SIZE);
    auto c = chunkAt(cx, cz);
    if (!c || c->state.load() < 1) return false;
    int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;
    {
        std::unique_lock<std::shared_mutex> lk(blocksMutex_);
        if (c->blocks[chunkIndex(lx, y, lz)] == id) return false;
        c->blocks[chunkIndex(lx, y, lz)] = id;
    }
    editLog_.push_back({x, y, z, (int)id});
    // The owning chunk AND its edge neighbours must be re-meshed: a block edit on a
    // chunk boundary changes the neighbour's boundary face culling/AO, and failing to
    // rebuild it leaves stale faces that z-fight (flicker / see-through seams).
    markDirty(cx, cz);
    markDirty(cx + 1, cz);
    markDirty(cx - 1, cz);
    markDirty(cx, cz + 1);
    markDirty(cx, cz - 1);
    return true;
}

void World::markDirty(int cx, int cz) {
    scheduleMesh(cx, cz);
}

void World::loadChunkFromDisk(int cx, int cz, std::istream& f) {
    uint64_t key = chunkKey(cx, cz);
    std::shared_ptr<Chunk> c;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        auto it = chunks_.find(key);
        if (it == chunks_.end()) { c = std::make_shared<Chunk>(); chunks_[key] = c; }
        else c = it->second;
    }
    f.read((char*)c->blocks.data(), CHUNK_VOL);
    c->state.store(1);
    c->dirty.store(true);
}

void World::forceMeshChunk(int cx, int cz) {
    uint64_t key = chunkKey(cx, cz);
    std::shared_ptr<Chunk> c;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        auto it = chunks_.find(key);
        if (it == chunks_.end()) return;
        c = it->second;
    }
    if (c->state.load() < 1) return;
    c->dirty.store(true);
    {
        std::lock_guard<std::mutex> lk(queueLock_);
        queuedMesh_.erase(key);
        meshing_.erase(key);
    }
    float wx = cx * 16.0f + 8.0f, wz = cz * 16.0f + 8.0f;
    float dx = wx - lastPx_, dz = wz - lastPz_;
    uint64_t prio = (uint64_t)(dx * dx + dz * dz);
    std::lock_guard<std::mutex> lk(queueLock_);
    queuedMesh_.insert(key);
    queue_.push(WorldTask{true, cx, cz, prio, c});
    queueCV_.notify_one();
}

void World::forceGenerateChunk(int cx, int cz) {
    uint64_t key = chunkKey(cx, cz);
    std::shared_ptr<Chunk> c;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        auto it = chunks_.find(key);
        if (it == chunks_.end()) { c = std::make_shared<Chunk>(); chunks_[key] = c; }
        else c = it->second;
    }
    if (c->state.load() >= 1) return;
    gen::generateColumn(seed, cx, cz, c->blocks.data());
    c->state.store(1);
    c->dirty.store(true);
    scheduleMesh(cx, cz);
}

void World::scheduleMesh(int cx, int cz) {
    std::shared_ptr<Chunk> c;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return;
        c = it->second;
    }
    if (c->state.load() < 1) return;
    c->dirty.store(true);
    uint64_t key = chunkKey(cx, cz);
    {
        std::lock_guard<std::mutex> lk(queueLock_);
        if (queuedMesh_.count(key) || meshing_.count(key)) return;
        queuedMesh_.insert(key);
        float wx = cx * 16.0f + 8.0f, wz = cz * 16.0f + 8.0f;
        float dx = wx - lastPx_, dz = wz - lastPz_;
        uint64_t prio = (uint64_t)(dx * dx + dz * dz);
        queue_.push(WorldTask{true, cx, cz, prio, c});
        queueCV_.notify_one();
    }
}

void World::enqueue(bool isMesh, int cx, int cz, uint64_t prio) {
    std::shared_ptr<Chunk> c;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return;
        c = it->second;
    }
    std::lock_guard<std::mutex> lk(queueLock_);
    if (isMesh) {
        if (queuedMesh_.count(chunkKey(cx, cz)) || meshing_.count(chunkKey(cx, cz))) return;
        queuedMesh_.insert(chunkKey(cx, cz));
    } else {
        if (queuedGen_.count(chunkKey(cx, cz)) || generating_.count(chunkKey(cx, cz))) return;
        queuedGen_.insert(chunkKey(cx, cz));
    }
    queue_.push(WorldTask{isMesh, cx, cz, prio, c});
    queueCV_.notify_one();
}

bool World::popTask(WorldTask& out) {
    std::unique_lock<std::mutex> lk(queueLock_);
    while (true) {
        if (!running_.load() && queue_.empty()) return false;
        if (queue_.empty()) {
            queueCV_.wait_for(lk, std::chrono::milliseconds(20));
            continue;
        }
        out = queue_.top();
        queue_.pop();
        uint64_t key = chunkKey(out.cx, out.cz);
        if (out.isMesh) {
            queuedMesh_.erase(key);
            meshing_.insert(key);
        } else {
            queuedGen_.erase(key);
            generating_.insert(key);
        }
        return true;
    }
}

void World::generateChunk(int cx, int cz, const std::shared_ptr<Chunk>& c) {
    gen::generateColumn(seed, cx, cz, c->blocks.data());
    c->state.store(1);
    scheduleMesh(cx, cz);
    scheduleMesh(cx + 1, cz);
    scheduleMesh(cx - 1, cz);
    scheduleMesh(cx, cz + 1);
    scheduleMesh(cx, cz - 1);
    {
        std::lock_guard<std::mutex> lk(queueLock_);
        generating_.erase(chunkKey(cx, cz));
    }
}

void World::meshChunk(int cx, int cz, const std::shared_ptr<Chunk>& c) {
    MeshView view;
    view.blocks.fill(B_AIR);

    // Collect all neighbor chunks under a single mapLock_ acquisition.
    std::shared_ptr<Chunk> neighbors[4]; // +x, -x, +z, -z
    std::shared_ptr<Chunk> corners[4];   // ++, -+, +-, --
    {
        std::lock_guard<std::mutex> mlk(mapLock_);
        auto get = [&](int ncx, int ncz) -> std::shared_ptr<Chunk> {
            auto it = chunks_.find(chunkKey(ncx, ncz));
            return (it != chunks_.end() && it->second->state.load() >= 1) ? it->second : nullptr;
        };
        neighbors[0] = get(cx + 1, cz);
        neighbors[1] = get(cx - 1, cz);
        neighbors[2] = get(cx, cz + 1);
        neighbors[3] = get(cx, cz - 1);
        corners[0] = get(cx + 1, cz + 1);
        corners[1] = get(cx - 1, cz + 1);
        corners[2] = get(cx + 1, cz - 1);
        corners[3] = get(cx - 1, cz - 1);
    }

    {
        std::shared_lock<std::shared_mutex> lk(blocksMutex_);

        // self
        for (int y = 0; y < WORLD_HEIGHT; y++)
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++)
                    view.set(x, y, z, c->blocks[chunkIndex(x, y, z)]);

        // +x neighbor
        if (neighbors[0]) {
            auto& nc = neighbors[0];
            for (int y = 0; y < WORLD_HEIGHT; y++)
                for (int z = 0; z < 16; z++)
                    view.set(16, y, z, nc->blocks[chunkIndex(0, y, z)]);
        }
        // -x neighbor
        if (neighbors[1]) {
            auto& nc = neighbors[1];
            for (int y = 0; y < WORLD_HEIGHT; y++)
                for (int z = 0; z < 16; z++)
                    view.set(-1, y, z, nc->blocks[chunkIndex(15, y, z)]);
        }
        // +z neighbor
        if (neighbors[2]) {
            auto& nc = neighbors[2];
            for (int y = 0; y < WORLD_HEIGHT; y++)
                for (int x = 0; x < 16; x++)
                    view.set(x, y, 16, nc->blocks[chunkIndex(x, y, 0)]);
        }
        // -z neighbor
        if (neighbors[3]) {
            auto& nc = neighbors[3];
            for (int y = 0; y < WORLD_HEIGHT; y++)
                for (int x = 0; x < 16; x++)
                    view.set(x, y, -1, nc->blocks[chunkIndex(x, y, 15)]);
        }
        // corners
        auto copyCorner = [&](const std::shared_ptr<Chunk>& nc, int vx, int vz) {
            if (!nc) return;
            int nx = vx == 16 ? 0 : 15;
            int nz = vz == 16 ? 0 : 15;
            for (int y = 0; y < WORLD_HEIGHT; y++)
                view.set(vx, y, vz, nc->blocks[chunkIndex(nx, y, nz)]);
        };
        copyCorner(corners[0], 16, 16);
        copyCorner(corners[1], -1, 16);
        copyCorner(corners[2], 16, -1);
        copyCorner(corners[3], -1, -1);
    }

    ChunkMeshData mesh = buildChunkMesh(view);
    {
        std::lock_guard<std::mutex> lk(c->meshLock);
        c->mesh = std::move(mesh);
        c->needsUpload.store(true);
        c->dirty.store(false);
        c->state.store(2);
    }
    {
        std::lock_guard<std::mutex> lk(queueLock_);
        meshing_.erase(chunkKey(cx, cz));
    }
}

void World::workerLoop() {
    WorldTask t;
    while (popTask(t)) {
        if (t.isMesh)
            meshChunk(t.cx, t.cz, t.chunk);
        else
            generateChunk(t.cx, t.cz, t.chunk);
    }
}

void World::update(float px, float pz, int renderDist) {
    lastPx_ = px;
    lastPz_ = pz;
    int ccx = floordiv((int)std::floor(px), CHUNK_SIZE);
    int ccz = floordiv((int)std::floor(pz), CHUNK_SIZE);

    // ensure chunks exist + queue generation
    for (int r = 0; r <= renderDist; r++) {
        int x0 = ccx - r, x1 = ccx + r;
        int z0 = ccz - r, z1 = ccz + r;
        for (int cx = x0; cx <= x1; cx++) {
            for (int cz = z0; cz <= z1; cz++) {
                if (r > 0 && cx > x0 && cx < x1 && cz > z0 && cz < z1) continue;
                uint64_t key = chunkKey(cx, cz);
                bool needGen = false;
                {
                    std::lock_guard<std::mutex> lk(mapLock_);
                    auto it = chunks_.find(key);
                    if (it == chunks_.end()) {
                        auto c = std::make_shared<Chunk>();
                        chunks_[key] = c;
                        needGen = true;
                    } else if (it->second->state.load() == 0) {
                        std::lock_guard<std::mutex> qlk(queueLock_);
                        if (!queuedGen_.count(key) && !generating_.count(key))
                            needGen = true;
                    }
                }
                if (needGen) {
                    float wx = cx * 16.0f + 8.0f, wz = cz * 16.0f + 8.0f;
                    float dx = wx - px, dz = wz - pz;
                    uint64_t prio = (uint64_t)(dx * dx + dz * dz);
                    enqueue(false, cx, cz, prio);
                }
            }
        }
    }

    // unload far chunks
    std::vector<uint64_t> toErase;
    {
        std::lock_guard<std::mutex> lk(mapLock_);
        for (auto& kv : chunks_) {
            int cx = (int)(int32_t)(kv.first >> 32);
            int cz = (int)(int32_t)kv.first;
            int dist = std::max(std::abs(cx - ccx), std::abs(cz - ccz));
            auto& c = kv.second;
            if (dist > renderDist + 2 && c->state.load() >= 2 && !c->dirty.load()) {
                uint64_t key = kv.first;
                {
                    std::lock_guard<std::mutex> qlk(queueLock_);
                    if (generating_.count(key) || meshing_.count(key) || queuedMesh_.count(key) || queuedGen_.count(key))
                        continue;
                }
                toErase.push_back(key);
            }
        }
        for (uint64_t key : toErase) {
            auto it = chunks_.find(key);
            if (it != chunks_.end()) {
                if (onDestroyChunk) onDestroyChunk(*it->second);
                chunks_.erase(it);
            }
        }
    }
}
