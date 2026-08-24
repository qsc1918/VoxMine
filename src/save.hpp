#pragma once
#include <cstdint>
#include <string>
#include <vector>

class World;
struct Player;

struct WorldSave {
    std::string name;
    uint32_t seed = 0;
    float spawnX = 0, spawnY = 80, spawnZ = 0;
};

std::string savesDir();
std::vector<WorldSave> listSaves(const std::string& dir);

// Save: binary format, writes all loaded chunks.
bool saveWorld(World& world, const Player& player, const std::string& name,
               const std::string& dir);

// Read header only (seed + spawn) for menu listing.
WorldSave saveInfo(const std::string& name, const std::string& dir);

// Load full world from disk (chunks + seed + spawn). Returns false on I/O error.
bool loadWorld(World& world, uint32_t& seed, float& spawnX, float& spawnY, float& spawnZ,
               const std::string& name, const std::string& dir);
