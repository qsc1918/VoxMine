#pragma once
#include "util.hpp"
#include "blocks.hpp"

class World;

struct RayHit {
    int x = 0, y = 0, z = 0; // hit block coords
    int px = 0, py = 0, pz = 0; // previous (empty) cell coords
    int face = 0;             // face index of the hit
    bool hit = false;
};

// DDA voxel raycast. origin/pos, dir normalized, maxDist in blocks.
RayHit raycastWorld(const World& w, Vec3 origin, Vec3 dir, float maxDist);
