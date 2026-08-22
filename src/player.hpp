#pragma once
#include "camera.hpp"
#include "window.hpp"

class World;

struct Player {
    Camera cam;
    Vec3 vel{0, 0, 0};
    bool flying = false;
    bool onGround = false;
    bool inWater = false;
    float height = 1.8f;
    float halfWidth = 0.3f;
    float eyeHeight = 1.62f;

    void update(Input& in, World& world, float dt);
    bool solidAt(const World& w, int x, int y, int z) const;
    bool collides(const World& w, float x0, float y0, float z0, float x1, float y1, float z1) const;
    bool spaceHeld_ = false;
    float spaceTapTimer_ = 1.0f;
};
