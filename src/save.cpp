#include "save.hpp"
#include "world.hpp"
#include "player.hpp"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// floor division that works for negative coords (modulo semantics for voxel coords).
static inline int floordivCoord(int a, int b) {
    int q = a / b, r = a % b;
    if (r < 0) q--;
    return q;
}

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

std::string savesDir() { return exeDir() + "\\saves"; }

static std::string levelPath(const std::string& name, const std::string& dir) {
    return dir + "\\" + name + "\\level.dat";
}

std::vector<WorldSave> listSaves(const std::string& dir) {
    std::vector<WorldSave> out;
    if (!fs::exists(dir)) return out;
    for (const auto& e : fs::directory_iterator(dir)) {
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
    std::ifstream f(levelPath(name, dir));
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "seed") ls >> s.seed;
        else if (key == "spawn") ls >> s.spawnX >> s.spawnY >> s.spawnZ;
    }
    return s;
}

bool saveWorld(World& world, const Player& player, const std::string& name,
               const std::string& dir) {
    try {
        fs::create_directories(dir + "\\" + name);
        std::ofstream f(levelPath(name, dir), std::ios::trunc);
        if (!f) return false;
        f << "seed " << world.seed << "\n";
        f << "spawn " << player.cam.pos.x << " " << player.cam.pos.y << " "
          << player.cam.pos.z << "\n";
        const auto& edits = world.editLog();
        f << "edits " << edits.size() << "\n";
        for (const auto& e : edits)
            f << e[0] << " " << e[1] << " " << e[2] << " " << e[3] << "\n";
        return true;
    } catch (...) {
        return false;
    }
}

void applyEdits(World& world, const std::string& name, const std::string& dir) {
    std::ifstream f(levelPath(name, dir));
    std::string line;
    int n = 0;
    // find the "edits N" marker
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "edits") { ls >> n; break; }
    }
    int applied = 0;
    while (applied < n && std::getline(f, line)) {
        int x, y, z, id;
        std::istringstream ls(line);
        if (ls >> x >> y >> z >> id) {
            world.forceGenerateChunk(floordivCoord(x, 16), floordivCoord(z, 16));
            world.setBlock(x, y, z, (uint8_t)id);
            applied++;
        }
    }
    world.clearEditLog();
}
