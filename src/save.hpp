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

// Bases path of the saves/ directory next to the .exe.
std::string savesDir();

// Scans the saves/ directory for existing worlds.
std::vector<WorldSave> listSaves(const std::string& dir);

// Saves the current world+player state under saves/<name>/level.dat.
bool saveWorld(World& world, const Player& player, const std::string& name,
               const std::string& dir);

// Reads the seed + spawn of a saved world (for metadata / to construct a fresh World).
WorldSave saveInfo(const std::string& name, const std::string& dir);

// Re-applies all saved block edits onto a fresh World (created with the right seed).
void applyEdits(World& world, const std::string& name, const std::string& dir);
