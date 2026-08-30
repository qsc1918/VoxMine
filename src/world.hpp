#pragma once
#include "blocks.hpp"
#include "noise.hpp"
#include "util.hpp"
#include <atomic>
#include <array>
#include <condition_variable>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int CHUNK_SIZE = 16;
constexpr int WORLD_HEIGHT = 128;
constexpr int SEA_LEVEL = 62;
constexpr int CHUNK_VOL = CHUNK_SIZE * WORLD_HEIGHT * CHUNK_SIZE;

inline int chunkIndex(int x, int y, int z) { return x + (z << 4) + (y << 8); }

// 8-byte packed vertex for terrain/water meshes.
struct TerrainVertex {
    int8_t  x, y, z;
    uint8_t pad;
    uint8_t u, v;     // 0..15 within tile
    uint8_t tex;      // atlas tile index
    uint8_t shade;    // 0..255 baked brightness (face light * AO)
};
static_assert(sizeof(TerrainVertex) == 8, "vertex must be 8 bytes");

inline bool operator==(const TerrainVertex& a, const TerrainVertex& b) {
    return std::memcmp(&a, &b, sizeof(TerrainVertex)) == 0;
}

struct ChunkMeshData {
    std::vector<TerrainVertex> opaqueVerts;
    std::vector<uint32_t>      opaqueIdx;
    std::vector<TerrainVertex> waterVerts;
    std::vector<uint32_t>      waterIdx;
};

struct Chunk {
    std::array<uint8_t, CHUNK_VOL> blocks{};
    std::atomic<int> state{0};        // 0 empty, 1 generated, 2 meshed
    std::atomic<bool> dirty{false};
    std::atomic<bool> needsUpload{false};
    std::mutex meshLock;
    ChunkMeshData mesh;

    uint64_t opaqueBuf = 0;   // VkBuffer handles stored as raw
    uint64_t opaqueMem = 0;
    uint64_t waterBuf = 0;
    uint64_t waterMem = 0;
    uint32_t opaqueCount = 0; // index count
    uint32_t waterCount = 0;
    uint64_t opaqueAlloc = 0; // bytes allocated
    uint64_t waterAlloc = 0;
    uint64_t opaqueVertBytes = 0; // vertex data byte size (index buffer offset)
    uint64_t waterVertBytes = 0;
};

inline uint64_t chunkKey(int cx, int cz) {
    return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}

struct WorldTask {
    bool isMesh = false;
    int32_t cx = 0, cz = 0;
    uint64_t prio = 0; // lower = earlier
    std::shared_ptr<Chunk> chunk;
};

inline bool worldTaskLess(const WorldTask& a, const WorldTask& b) {
    if (a.prio != b.prio) return a.prio > b.prio;
    return a.cx != b.cx ? a.cx > b.cx : a.cz > b.cz;
}

// Stores a local 18x18x128 copy of a chunk plus its 4 neighbors for meshing.
// Indexed by LOCAL block coordinates x,z in [-1,16] and y in [0,WORLD_HEIGHT).
// The array is 18 wide per axis; index = (x+1) + (z+1)*18 + y*18*18 so the
// -1..16 local range maps cleanly onto 0..17 without out-of-bounds access.
struct MeshView {
    std::array<uint8_t, 18 * 18 * WORLD_HEIGHT> blocks{};
    inline uint8_t at(int x, int y, int z) const {
        if (x < -1 || x > 16 || y < 0 || y >= WORLD_HEIGHT || z < -1 || z > 16) return B_AIR;
        return blocks[(x + 1) + (z + 1) * 18 + y * 18 * 18];
    }
    inline void set(int x, int y, int z, uint8_t v) {
        blocks[(x + 1) + (z + 1) * 18 + y * 18 * 18] = v;
    }
};

class World {
public:
    explicit World(uint32_t seed);
    ~World();

    void startWorkers(int n);
    void stopWorkers();

    // Main-thread scheduling: ensure chunks around (px,pz) exist & are queued.
    void update(float px, float pz, int renderDist);

    // Block read for gameplay code (main thread). Safe & returns air for ungenerated chunks.
    uint8_t getBlock(int x, int y, int z) const;

    // Player edit. Marks affected chunks dirty. Returns true if changed.
    bool setBlock(int x, int y, int z, uint8_t id);

    std::shared_ptr<Chunk> chunkAt(int cx, int cz) const;

    // Iterate all chunks (main thread only).
    void forEachChunk(const std::function<void(std::shared_ptr<Chunk>&, int, int)>& fn);

    // Raw-pointer snapshot (avoids shared_ptr atomic ops per frame).
    struct ChunkInfo { Chunk* c; int cx; int cz; };
    void snapshotChunks(std::vector<ChunkInfo>& out);

    void markDirty(int cx, int cz);

    // Recorded block edits (x,y,z,blockId) for saving; x,y,z absolute coords.
    const std::vector<std::array<int, 4>>& editLog() const { return editLog_; }
    void clearEditLog() { editLog_.clear(); }

    // Synchronously create + generate a chunk (used when loading a save, where the
    // worker pool has no tasks yet, so there is no race).
    void forceGenerateChunk(int cx, int cz);

    // Load a single chunk's block data from a binary stream (save loading).
    void loadChunkFromDisk(int cx, int cz, std::istream& f);

    // Force re-mesh of a chunk (used after loading to fix boundary faces).
    void forceMeshChunk(int cx, int cz);

    size_t chunkCount() const {
        std::lock_guard<std::mutex> lk(mapLock_);
        return chunks_.size();
    }

    // Number of worker threads active.
    int workerCount() const { return (int)workers_.size(); }

    uint32_t seed;

    // Called on the main thread right before a chunk is erased from the map.
    std::function<void(Chunk&)> onDestroyChunk;

private:
    void workerLoop();
    void generateChunk(int cx, int cz, const std::shared_ptr<Chunk>& c);
    void meshChunk(int cx, int cz, const std::shared_ptr<Chunk>& c);
    void scheduleMesh(int cx, int cz);
    void enqueue(bool isMesh, int cx, int cz, uint64_t prio);
    bool popTask(WorldTask& out);

    mutable std::mutex mapLock_;
    std::unordered_map<uint64_t, std::shared_ptr<Chunk>> chunks_;
    mutable std::shared_mutex blocksMutex_;

    // Small direct-mapped cache to avoid mapLock_ for repeated chunkAt() lookups.
    static constexpr int kChunkCacheN = 8;
    mutable std::array<std::pair<uint64_t, std::shared_ptr<Chunk>>, kChunkCacheN> chunkCache_{};
    mutable uint32_t chunkCacheIdx_ = 0;

    std::priority_queue<WorldTask, std::vector<WorldTask>, std::function<bool(const WorldTask&, const WorldTask&)>> queue_;
    std::mutex queueLock_;
    std::condition_variable queueCV_;
    std::unordered_set<uint64_t> queuedGen_;
    std::unordered_set<uint64_t> queuedMesh_;
    std::unordered_set<uint64_t> generating_;
    std::unordered_set<uint64_t> meshing_;

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    float lastPx_ = 0.0f, lastPz_ = 0.0f;
    int lastCCX_ = INT_MIN, lastCCZ_ = INT_MIN, lastRenderDist_ = -1;
    uint32_t unloadCounter_ = 0;

    std::vector<std::array<int, 4>> editLog_;
};
