#include "noise.hpp"
#include "util.hpp"
#include <cmath>

namespace {
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float hash2i(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = hash32(x ^ 0x9e3779b9U) ^ hash32(y * 0x85ebca6bU) ^ hash32(seed * 0xc2b2ae35U);
    h ^= h >> 13;
    return (h & 0xFFFF) * (1.0f / 65535.0f) * 2.0f - 1.0f;
}
inline float hash3i(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t h = hash32(x) ^ hash32(y * 0x85ebca6bU) ^ hash32(z * 0xc2b2ae35U) ^ hash32(seed * 0x27d4eb2dU);
    h ^= h >> 15;
    return (h & 0xFFFF) * (1.0f / 65535.0f) * 2.0f - 1.0f;
}
} // namespace

float Noise::value2(float x, float y) const {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = fade(xf), v = fade(yf);
    float n00 = hash2i((uint32_t)xi, (uint32_t)yi, seed);
    float n10 = hash2i((uint32_t)(xi + 1), (uint32_t)yi, seed);
    float n01 = hash2i((uint32_t)xi, (uint32_t)(yi + 1), seed);
    float n11 = hash2i((uint32_t)(xi + 1), (uint32_t)(yi + 1), seed);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v);
}

float Noise::value3(float x, float y, float z) const {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    float u = fade(xf), v = fade(yf), w = fade(zf);
    auto g = [&](int dx, int dy, int dz) {
        return hash3i((uint32_t)(xi + dx), (uint32_t)(yi + dy), (uint32_t)(zi + dz), seed);
    };
    float c000 = g(0, 0, 0), c100 = g(1, 0, 0);
    float c010 = g(0, 1, 0), c110 = g(1, 1, 0);
    float c001 = g(0, 0, 1), c101 = g(1, 0, 1);
    float c011 = g(0, 1, 1), c111 = g(1, 1, 1);
    float x00 = lerpf(c000, c100, u);
    float x10 = lerpf(c010, c110, u);
    float x01 = lerpf(c001, c101, u);
    float x11 = lerpf(c011, c111, u);
    float y0 = lerpf(x00, x10, v);
    float y1 = lerpf(x01, x11, v);
    return lerpf(y0, y1, w);
}

float Noise::fbm2(float x, float y, int octaves, float lac, float gain) const {
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * value2(x * freq, y * freq);
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return sum / norm;
}

float Noise::fbm3(float x, float y, float z, int octaves, float lac, float gain) const {
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * value3(x * freq, y * freq, z * freq);
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return sum / norm;
}
