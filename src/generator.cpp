#include "generator.hpp"
#include "blocks.hpp"
#include "noise.hpp"
#include "util.hpp"
#include "world.hpp"
#include <algorithm>
#include <cmath>

namespace {

struct Ore {
    uint8_t id;
    int tries;
    int size;
    int minY, maxY;
};

const Ore kOres[] = {
    {B_COAL, 20, 9, 0, 96},
    {B_IRON, 20, 9, 0, 64},
    {B_GOLD, 2, 9, 0, 32},
    {B_REDSTONE, 8, 8, 0, 16},
    {B_DIAMOND, 1, 7, 0, 16},
};

inline uint8_t& ref(uint8_t* b, int x, int y, int z) { return b[x + (z << 4) + (y << 8)]; }

struct SurfaceInfo {
    int height;      // topmost terrain block y
    bool ocean;      // height < SEA_LEVEL
    bool desert;     // hot
    bool cold;       // snowy
};

SurfaceInfo surfaceFor(const Noise& temp, const Noise& moist, int wx, int wz, int height) {
    SurfaceInfo si;
    si.height = height;
    si.ocean = height < SEA_LEVEL;
    si.desert = temp.value2(wx * 0.008f + 100.0f, wz * 0.008f + 100.0f) > 0.62f;
    si.cold = temp.value2(wx * 0.008f + 100.0f, wz * 0.008f + 100.0f) < 0.22f;
    return si;
}

void placeTree(uint8_t* b, int wx, int wz, int rootY, Rng& rng) {
    int cxBase = (wx & 15);
    int czBase = (wz & 15);
    int trunk = 4 + rng.irange(0, 2);
    // trunk first (so leaves never replace it)
    for (int i = 0; i < trunk; i++) {
        int y = rootY + 1 + i;
        if (y < 2 || y >= WORLD_HEIGHT) continue;
        uint8_t& t = ref(b, cxBase, y, czBase);
        if (t == B_AIR || t == B_WATER) t = B_LOG;
    }
    // leaves
    for (int dy = -2; dy <= 1; dy++) {
        int ly = rootY + trunk + dy;
        if (ly < 2 || ly >= WORLD_HEIGHT) continue;
        int r = dy <= 0 ? 2 : 1;
        for (int dx = -r; dx <= r; dx++) {
            for (int dz = -r; dz <= r; dz++) {
                int x = cxBase + dx, z = czBase + dz;
                if (x < 0 || x >= 16 || z < 0 || z >= 16) continue;
                if (dx == 0 && dz == 0 && dy <= 0) continue; // trunk space (dy=1 caps the trunk top)
                if (dx * dx + dz * dz > r * r + 1) continue;
                uint8_t cur = ref(b, x, ly, z);
                if (cur == B_AIR || cur == B_WATER) ref(b, x, ly, z) = B_LEAVES;
            }
        }
    }
}

} // namespace

namespace gen {

void generateColumn(uint32_t seed, int cx, int cz, uint8_t* out) {
    std::fill(out, out + CHUNK_VOL, (uint8_t)B_AIR);

    Noise n1(seed), n2(seed ^ 0x9e3779b9U), n3(seed ^ 0x51ed270bU);
    Noise tempN(seed ^ 0x85ebca6bU), moistN(seed ^ 0xc2b2ae35U);
    Noise caveN(seed ^ 0x27d4eb2dU), caveN2(seed ^ 0x165667b1U);
    Noise beachN(seed ^ 0x9e3779b9U ^ 0x1337U);

    int baseWX = cx * 16, baseWZ = cz * 16;

    for (int lx = 0; lx < 16; lx++) {
        for (int lz = 0; lz < 16; lz++) {
            int wx = baseWX + lx, wz = baseWZ + lz;

            // layered heightmap. Use a smooth value-noise pyramid so adjacent
            // columns step by at most 1-2 blocks each (no terraced canyons).
            float continents = n1.fbm2(wx * 0.004f, wz * 0.004f, 3, 2.0f, 0.5f);
            float hills = n2.fbm2(wx * 0.012f, wz * 0.012f, 4, 2.0f, 0.5f);
            float detail = n3.fbm2(wx * 0.045f, wz * 0.045f, 2, 2.0f, 0.5f);

            float h = SEA_LEVEL + continents * 22.0f + hills * 8.0f + detail * 2.0f;
            int height = (int)h;
            height = std::clamp(height, 3, WORLD_HEIGHT - 1);

            SurfaceInfo si = surfaceFor(tempN, moistN, wx, wz, height);

            // bedrock
            ref(out, lx, 0, lz) = B_BEDROCK;
            for (int y = 1; y <= 4; y++) {
                float chance = 0.9f - y * 0.18f;
                // deterministic per block
                uint32_t hsh = hash32((uint32_t)wx * 0x9e3779b9U ^ (uint32_t)wz ^ (uint32_t)(y * 97)) * 0x85ebca6bU;
                float r = (hsh & 0xFFFF) * (1.0f / 65535.0f);
                ref(out, lx, y, lz) = r < chance ? B_BEDROCK : B_STONE;
            }

            // base fill
            for (int y = 5; y <= height; y++)
                ref(out, lx, y, lz) = B_STONE;

            // surface materials
            if (si.ocean) {
                int floorY = height;
                uint8_t mat = si.cold ? B_GRAVEL : B_SAND;
                if (floorY >= SEA_LEVEL - 2) mat = si.cold ? B_GRAVEL : B_SAND;
                for (int y = floorY; y > floorY - 3 && y >= 5; y--)
                    ref(out, lx, y, lz) = y == floorY ? mat : (si.cold && y == floorY ? mat : B_DIRT);
                ref(out, lx, floorY, lz) = mat;
            } else {
                uint8_t top, under;
                if (si.desert) { top = B_SAND; under = B_SAND; }
                else if (si.cold) { top = B_SNOW; under = B_DIRT; }
                else { top = B_GRASS; under = B_DIRT; }
                ref(out, lx, height, lz) = top;
                for (int y = height - 1; y > height - 4 && y >= 5; y--)
                    ref(out, lx, y, lz) = under;
            }
        }
    }

    // caves (3D) �?keep them well below the terrain surface so they never breach
    // the ground and form crater-like pits across the landscape.
    for (int y = 3; y < 90; y++) {
        for (int lx = 0; lx < 16; lx++) {
            for (int lz = 0; lz < 16; lz++) {
                int wx = baseWX + lx, wz = baseWZ + lz;
                uint8_t cur = ref(out, lx, y, lz);
                if (cur == B_AIR || cur == B_BEDROCK) continue;
                // local surface height for this column (topmost solid block)
                int localTop = -1;
                for (int yy = WORLD_HEIGHT - 1; yy >= 0; yy--) {
                    uint8_t b = ref(out, lx, yy, lz);
                    if (b != B_AIR && b != B_WATER) { localTop = yy; break; }
                }
                if (localTop < 0) continue;
                // only carve if clearly below the surface (leave a solid crust)
                if (y >= localTop - 4) continue;
                float cn = caveN.fbm3(wx * 0.045f, y * 0.07f, wz * 0.045f, 3, 2.0f, 0.5f);
                float cn2 = caveN2.fbm3(wx * 0.09f, y * 0.14f, wz * 0.09f, 2, 2.0f, 0.5f);
                float v = cn * 0.72f + cn2 * 0.28f;
                if (v > 0.30f) {
                    ref(out, lx, y, lz) = B_AIR;
                }
            }
        }
    }

    // ores
    Rng rng((uint64_t)seed * 0x100000001ULL ^ ((uint64_t)(uint32_t)cx << 32) ^ (uint32_t)cz ^ 0x6a09e667ULL);
    for (const Ore& ore : kOres) {
        for (int t = 0; t < ore.tries; t++) {
            int ox = rng.irange(0, 15), oz = rng.irange(0, 15);
            int oy = rng.irange(ore.minY, ore.maxY);
            for (int s = 0; s < ore.size; s++) {
                int x = ox + rng.irange(-2, 2);
                int y = oy + rng.irange(-2, 2);
                int z = oz + rng.irange(-2, 2);
                if (x < 0 || x > 15 || z < 0 || z > 15 || y < 1 || y >= WORLD_HEIGHT) continue;
                if (ref(out, x, y, z) == B_STONE) ref(out, x, y, z) = ore.id;
            }
        }
    }

    // trees
    for (int lx = 0; lx < 16; lx++) {
        for (int lz = 0; lz < 16; lz++) {
            int wx = baseWX + lx, wz = baseWZ + lz;
            int height = 0;
            for (int y = WORLD_HEIGHT - 1; y >= 3; y--) {
                if (ref(out, lx, y, lz) != B_AIR) { height = y; break; }
            }
            if (height < SEA_LEVEL) continue;
            uint8_t top = ref(out, lx, height, lz);
            if (top != B_GRASS && top != B_SNOW) continue;
            // avoid chunk borders so trees never cross into neighbors
            if (lx < 2 || lx > 13 || lz < 2 || lz > 13) continue;
            SurfaceInfo si = surfaceFor(tempN, moistN, wx, wz, height);
            if (si.desert) continue;
            float moist = moistN.value2(wx * 0.008f + 50.0f, wz * 0.008f + 50.0f);
            float density = 0.035f + moist * 0.05f;
            if (si.cold) density *= 0.4f;
            uint32_t hsh = hash32((uint32_t)wx * 0x9e3779b9U ^ (uint32_t)wz * 0x85ebca6bU ^ seed);
            float r = (hsh & 0xFFFF) * (1.0f / 65535.0f);
            if (r < density) {
                placeTree(out, wx, wz, height, rng);
            }
        }
    }

    // water fill. Ocean/lake columns (terrain surface below sea level) flood fully;
    // on land columns the air below sea level is carved cave air, which we flood
    // only ~20% of the time (low-frequency noise -> wet cave patches, most caves dry)
    // so the underground isn't a giant lake like vanilla aquifers.
    Noise waterN(seed ^ 0xb5297a4dU);
    for (int lx = 0; lx < 16; lx++) {
        for (int lz = 0; lz < 16; lz++) {
            int wx = baseWX + lx, wz = baseWZ + lz;
            // topmost naturally-solid block = terrain surface (caves are air below it)
            int top = WORLD_HEIGHT - 1;
            while (top >= 0) {
                uint8_t b = ref(out, lx, top, lz);
                if (b != B_AIR && b != B_WATER) break;
                top--;
            }
            bool ocean = top >= 0 && top < SEA_LEVEL;
            for (int y = SEA_LEVEL - 1; y >= 0; y--) {
                if (ref(out, lx, y, lz) != B_AIR) continue;
                if (ocean) {
                    ref(out, lx, y, lz) = B_WATER;
                } else {
                    float wn = waterN.fbm3(wx * 0.06f, y * 0.08f, wz * 0.06f, 2, 2.0f, 0.5f);
                    if (wn > 0.42f) ref(out, lx, y, lz) = B_WATER;
                }
            }
        }
    }
}

} // namespace gen
