#include "save.hpp"
#include "world.hpp"
#include "player.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

std::string savesDir() { return exeDir() + "\\saves"; }

static fs::path u8(const std::string& s) {
    static_assert(sizeof(char) == sizeof(char8_t), "char8_t must be same size as char");
    return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
}

static fs::path levelPath(const std::string& name, const std::string& dir) {
    return u8(dir) / u8(name) / "level.dat";
}

// ---- Binary format ----
// Header: magic(4) version(4) seed(4) spawnX(4) spawnY(4) spawnZ(4)
//         yaw(4) pitch(4) flying(4) numChunks(4) = 44 bytes
// Per chunk: cx(4) cz(4) blocks[65536] = 65544 bytes
static constexpr uint32_t SAVE_MAGIC = 0x564D5356; // "VMSV"
static constexpr uint32_t SAVE_VERSION = 2;

std::vector<WorldSave> listSaves(const std::string& dir) {
    std::vector<WorldSave> out;
    fs::path base = u8(dir);
    if (!fs::exists(base)) return out;
    for (const auto& e : fs::directory_iterator(base)) {
        if (!e.is_directory()) continue;
        std::string name = e.path().filename().string();
        WorldSave s = saveInfo(name, dir);
        s.name = name;
        out.push_back(s);
    }
    return out;
}

WorldSave saveInfo(const std::string& name, const std::string& dir) {
    WorldSave s;
    std::ifstream f(levelPath(name, dir), std::ios::binary);
    if (!f) return s;
    uint32_t magic = 0, version = 0;
    f.read((char*)&magic, 4);
    f.read((char*)&version, 4);
    if (magic != SAVE_MAGIC) return s;
    f.read((char*)&s.seed, 4);
    f.read((char*)&s.spawnX, 4);
    f.read((char*)&s.spawnY, 4);
    f.read((char*)&s.spawnZ, 4);
    return s;
}

bool saveWorld(World& world, const Player& player, const std::string& name,
               const std::string& dir) {
    try {
        fs::path base = u8(dir);
        fs::create_directories(base / u8(name));
        std::ofstream f(levelPath(name, dir), std::ios::binary | std::ios::trunc);
        if (!f) return false;

        // header
        uint32_t magic = SAVE_MAGIC;
        uint32_t version = SAVE_VERSION;
        uint32_t seed = world.seed;
        float sx = player.cam.pos.x, sy = player.cam.pos.y, sz = player.cam.pos.z;
        float yaw = player.cam.yaw, pitch = player.cam.pitch;
        uint32_t flying = player.flying ? 1 : 0;
        f.write((char*)&magic, 4);
        f.write((char*)&version, 4);
        f.write((char*)&seed, 4);
        f.write((char*)&sx, 4);
        f.write((char*)&sy, 4);
        f.write((char*)&sz, 4);
        f.write((char*)&yaw, 4);
        f.write((char*)&pitch, 4);
        f.write((char*)&flying, 4);

        // collect all chunks
        struct ChunkEntry { int32_t cx, cz; const uint8_t* data; };
        std::vector<ChunkEntry> entries;
        world.forEachChunk([&](std::shared_ptr<Chunk>& c, int cx, int cz) {
            if (c->state.load() >= 1)
                entries.push_back({cx, cz, c->blocks.data()});
        });
        uint32_t numChunks = (uint32_t)entries.size();
        f.write((char*)&numChunks, 4);

        for (auto& e : entries) {
            f.write((char*)&e.cx, 4);
            f.write((char*)&e.cz, 4);
            f.write((const char*)e.data, CHUNK_VOL);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool loadWorld(World& world, uint32_t& seed, float& spawnX, float& spawnY, float& spawnZ,
               float& yaw, float& pitch, bool& flying,
               const std::string& name, const std::string& dir) {
    std::ifstream f(levelPath(name, dir), std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    f.read((char*)&magic, 4);
    f.read((char*)&version, 4);
    if (magic != SAVE_MAGIC) return false;

    f.read((char*)&seed, 4);
    f.read((char*)&spawnX, 4);
    f.read((char*)&spawnY, 4);
    f.read((char*)&spawnZ, 4);

    // v2 added player view/fly state. Old v1 saves omit the 12 bytes.
    yaw = 0.0f;
    pitch = -0.1f;
    flying = false;
    if (version >= 2) {
        f.read((char*)&yaw, 4);
        f.read((char*)&pitch, 4);
        uint32_t fly = 0;
        f.read((char*)&fly, 4);
        flying = fly != 0;
    }

    uint32_t numChunks = 0;
    f.read((char*)&numChunks, 4);

    for (uint32_t i = 0; i < numChunks; i++) {
        int32_t cx, cz;
        f.read((char*)&cx, 4);
        f.read((char*)&cz, 4);
        world.loadChunkFromDisk(cx, cz, f);
    }
    return true;
}
