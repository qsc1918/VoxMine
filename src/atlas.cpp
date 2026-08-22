#include "atlas.hpp"
#include "png.hpp"
#include <cstring>

static const char* kTileFiles[T_COUNT] = {
    "grass_block_top.png", "grass_block_side.png", "dirt.png",          "stone.png",
    "bedrock.png",         "cobblestone.png",      "oak_planks.png",    "oak_log.png",
    "oak_log_top.png",     "oak_leaves.png",       "sand.png",          "gravel.png",
    "coal_ore.png",        "iron_ore.png",         "gold_ore.png",      "diamond_ore.png",
    "redstone_ore.png",    "water_still.png",      "snow.png",          "glass.png",
    "white_concrete.png",
};

// Modern Minecraft uses grayscale tintable textures for these; we bake a tint in.
struct Tint { float r, g, b; };
const Tint kTileTint[T_COUNT] = {
    {0.62f, 1.02f, 0.40f}, // grass top  -> green
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {0.34f, 0.76f, 0.26f}, // leaves -> foliage green
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
    {0.40f, 0.62f, 1.10f}, // water -> blue
    {1, 1, 1},
    {1, 1, 1},
    {1, 1, 1},
};

float Atlas::tileU(int tile, int corner) const {
    int tx = tile % tilesX;
    float px = (float)(tx * tileSize);
    switch (corner) {
        case 0: return (px + 0.5f) / width;
        case 1: return (px + tileSize - 0.5f) / width;
        case 2: return (px + tileSize - 0.5f) / width;
        default: return (px + 0.5f) / width;
    }
}
float Atlas::tileV(int tile, int corner) const {
    int ty = tile / tilesX;
    float py = (float)(ty * tileSize);
    switch (corner) {
        case 0: return (py + 0.5f) / height;
        case 1: return (py + 0.5f) / height;
        case 2: return (py + tileSize - 0.5f) / height;
        default: return (py + tileSize - 0.5f) / height;
    }
}

Atlas buildAtlas(const std::string& dir) {
    Atlas a;
    a.width = a.tileSize * a.tilesX;
    a.height = a.tileSize * a.tilesY;
    a.rgba.assign((size_t)a.width * a.height * 4, 255);

    for (int t = 0; t < T_COUNT; t++) {
        std::string path = dir + "\\" + kTileFiles[t];
        std::vector<uint8_t> img;
        int w = 0, h = 0;
        bool ok = loadPNG(path.c_str(), img, w, h);
        int tx = t % a.tilesX, ty = t / a.tilesX;
        int ox = tx * a.tileSize, oy = ty * a.tileSize;
        if (ok && w >= a.tileSize && h >= a.tileSize) {
            const Tint& tint = kTileTint[t];
            for (int y = 0; y < a.tileSize; y++) {
                for (int x = 0; x < a.tileSize; x++) {
                    int sx = x, sy = y;
                    size_t src = ((size_t)sy * w + sx) * 4;
                    size_t dst = ((size_t)(oy + y) * a.width + (ox + x)) * 4;
                    float r = img[src + 0] * tint.r;
                    float g = img[src + 1] * tint.g;
                    float b = img[src + 2] * tint.b;
                    a.rgba[dst + 0] = (uint8_t)(r > 255.0f ? 255.0f : r);
                    a.rgba[dst + 1] = (uint8_t)(g > 255.0f ? 255.0f : g);
                    a.rgba[dst + 2] = (uint8_t)(b > 255.0f ? 255.0f : b);
                    a.rgba[dst + 3] = img[src + 3];
                }
            }
        }
    }
    a.built = true;
    return a;
}
