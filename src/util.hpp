#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Small math library (column-major matrices, GLSL compatible)
// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(const Vec3& a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 1e-9f ? a / l : Vec3(0, 1, 0); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

struct Mat4 {
    float m[16] = {0}; // column-major

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    static Mat4 perspective(float fovy, float aspect, float zn, float zf) {
        Mat4 r;
        float f = 1.0f / std::tan(fovy * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = -f; // Vulkan NDC has +y pointing down; flip so world +y is screen-up
        r.m[10] = (zf + zn) / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }
    static Mat4 mul(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                float v = 0;
                for (int k = 0; k < 4; k++) v += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = v;
            }
        }
        return r;
    }
    static Mat4 translate(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
};

struct Frustum {
    float planes[6][4];
    void extract(const Mat4& vp) {
        // Correct Gribb-Hartmann extraction for a column-major matrix.
        // For point p=(x,y,z,1): cx = m[0]x+m[4]y+m[8]z+m[12], etc.
        // Visible when: -cw<=cx<=cw, -cw<=cy<=cw, 0<=cz<=cw.
        const float* m = vp.m;
        // left:  cx + cw >= 0
        planes[0][0] = m[0] + m[3];  planes[0][1] = m[4] + m[7];
        planes[0][2] = m[8] + m[11]; planes[0][3] = m[12] + m[15];
        // right: cw - cx >= 0
        planes[1][0] = m[3] - m[0];  planes[1][1] = m[7] - m[4];
        planes[1][2] = m[11] - m[8]; planes[1][3] = m[15] - m[12];
        // bottom: cy + cw >= 0
        planes[2][0] = m[1] + m[3];  planes[2][1] = m[5] + m[7];
        planes[2][2] = m[9] + m[11]; planes[2][3] = m[13] + m[15];
        // top: cw - cy >= 0
        planes[3][0] = m[3] - m[1];  planes[3][1] = m[7] - m[5];
        planes[3][2] = m[11] - m[9]; planes[3][3] = m[15] - m[13];
        // near: cz >= 0
        planes[4][0] = m[2]; planes[4][1] = m[6];
        planes[4][2] = m[10]; planes[4][3] = m[14];
        // far: cw - cz >= 0
        planes[5][0] = m[3] - m[2];  planes[5][1] = m[7] - m[6];
        planes[5][2] = m[11] - m[10]; planes[5][3] = m[15] - m[14];
    }
    bool testAABB(float minx, float miny, float minz, float maxx, float maxy, float maxz) const {
        for (int i = 0; i < 6; i++) {
            const float* p = planes[i];
            float px = p[0] > 0 ? maxx : minx;
            float py = p[1] > 0 ? maxy : miny;
            float pz = p[2] > 0 ? maxz : minz;
            if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Fast deterministic random
// ---------------------------------------------------------------------------
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s += 0x9e3779b97f4a7c15ULL;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return (uint32_t)(z ^ (z >> 31));
    }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
    int irange(int a, int b) { return a + (int)(unit() * (b - a + 1)); }
};
