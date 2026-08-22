#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Block ids (early-Minecraft style palette)
// ---------------------------------------------------------------------------
enum Block : uint8_t {
    B_AIR = 0,
    B_STONE = 1,
    B_GRASS = 2,
    B_DIRT = 3,
    B_BEDROCK = 4,
    B_COBBLE = 5,
    B_PLANKS = 6,
    B_LOG = 7,
    B_LEAVES = 8,
    B_SAND = 9,
    B_GRAVEL = 10,
    B_COAL = 11,
    B_IRON = 12,
    B_GOLD = 13,
    B_DIAMOND = 14,
    B_REDSTONE = 15,
    B_WATER = 16,
    B_SNOW = 17,
    B_GLASS = 18,
    B_COUNT = 19,
};

// Tile ids into the texture atlas (assigned when atlas is built)
enum Tile : uint8_t {
    T_GRASS_TOP = 0,   T_GRASS_SIDE = 1,  T_DIRT = 2,      T_STONE = 3,
    T_BEDROCK = 4,     T_COBBLE = 5,      T_PLANKS = 6,    T_LOG_SIDE = 7,
    T_LOG_TOP = 8,     T_LEAVES = 9,      T_SAND = 10,     T_GRAVEL = 11,
    T_COAL = 12,       T_IRON = 13,       T_GOLD = 14,     T_DIAMOND = 15,
    T_REDSTONE = 16,   T_WATER = 17,      T_SNOW = 18,     T_GLASS = 19,
    T_WHITE = 20,
    T_COUNT = 21,
};

// Face indices
enum Face : int { F_PX = 0, F_NX = 1, F_PY = 2, F_NY = 3, F_PZ = 4, F_NZ = 5 };

inline bool blockIsOpaque(uint8_t id) {
    switch (id) {
        case B_AIR: case B_WATER: case B_LEAVES: case B_GLASS:
            return false;
        default:
            return true;
    }
}

inline bool blockIsSolid(uint8_t id) {
    switch (id) {
        case B_AIR: case B_WATER:
            return false;
        default:
            return true;
    }
}

inline bool blockIsRenderable(uint8_t id) {
    return id != B_AIR;
}

// Texture tile used by a block for a given face.
// Returns false for non-renderable / water handled separately.
inline uint8_t blockTile(uint8_t id, int face) {
    switch (id) {
        case B_GRASS:  return face == F_PY ? T_GRASS_TOP : (face == F_NY ? T_DIRT : T_GRASS_SIDE);
        case B_DIRT:   return T_DIRT;
        case B_STONE:  return T_STONE;
        case B_BEDROCK: return T_BEDROCK;
        case B_COBBLE: return T_COBBLE;
        case B_PLANKS: return T_PLANKS;
        case B_LOG:    return (face == F_PY || face == F_NY) ? T_LOG_TOP : T_LOG_SIDE;
        case B_LEAVES: return T_LEAVES;
        case B_SAND:   return T_SAND;
        case B_GRAVEL: return T_GRAVEL;
        case B_COAL:   return T_COAL;
        case B_IRON:   return T_IRON;
        case B_GOLD:   return T_GOLD;
        case B_DIAMOND: return T_DIAMOND;
        case B_REDSTONE: return T_REDSTONE;
        case B_WATER:  return T_WATER;
        case B_SNOW:   return T_SNOW;
        case B_GLASS:  return T_GLASS;
        default:       return T_WHITE;
    }
}
