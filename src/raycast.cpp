#include "raycast.hpp"
#include "world.hpp"
#include <algorithm>
#include <cmath>

RayHit raycastWorld(const World& w, Vec3 origin, Vec3 dir, float maxDist) {
    RayHit out;
    float dx = dir.x, dy = dir.y, dz = dir.z;

    int x = (int)std::floor(origin.x);
    int y = (int)std::floor(origin.y);
    int z = (int)std::floor(origin.z);

    int stepX = dx > 0 ? 1 : -1;
    int stepY = dy > 0 ? 1 : -1;
    int stepZ = dz > 0 ? 1 : -1;

    float tDeltaX = std::abs(dx) > 1e-9f ? std::abs(1.0f / dx) : 1e30f;
    float tDeltaY = std::abs(dy) > 1e-9f ? std::abs(1.0f / dy) : 1e30f;
    float tDeltaZ = std::abs(dz) > 1e-9f ? std::abs(1.0f / dz) : 1e30f;

    float tMaxX = dx != 0 ? (dx > 0 ? (x + 1 - origin.x) / dx : (origin.x - x) / -dx) : 1e30f;
    float tMaxY = dy != 0 ? (dy > 0 ? (y + 1 - origin.y) / dy : (origin.y - y) / -dy) : 1e30f;
    float tMaxZ = dz != 0 ? (dz > 0 ? (z + 1 - origin.z) / dz : (origin.z - z) / -dz) : 1e30f;

    int lastX = x, lastY = y, lastZ = z;
    float t = 0;
    while (t <= maxDist) {
        uint8_t id = w.getBlock(x, y, z);
        if (id != B_AIR) {
            out.hit = true;
            out.x = x; out.y = y; out.z = z;
            out.px = lastX; out.py = lastY; out.pz = lastZ;
            // determine face
            if (tMaxX < tMaxY && tMaxX < tMaxZ) out.face = dx > 0 ? F_NX : F_PX;
            else if (tMaxY < tMaxZ) out.face = dy > 0 ? F_NY : F_PY;
            else out.face = dz > 0 ? F_NZ : F_PZ;
            return out;
        }
        lastX = x; lastY = y; lastZ = z;
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
        }
    }
    return out;
}
