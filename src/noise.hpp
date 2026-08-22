#pragma once
#include <cstdint>

struct Noise {
    uint32_t seed = 1337;
    float   value2(float x, float y) const;
    float   value3(float x, float y, float z) const;
    float   fbm2(float x, float y, int octaves, float lacunarity, float gain) const;
    float   fbm3(float x, float y, float z, int octaves, float lacunarity, float gain) const;
};
