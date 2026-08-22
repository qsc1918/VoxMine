#include "player.hpp"
#include "world.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>

bool Player::solidAt(const World& w, int x, int y, int z) const {
    return blockIsSolid(w.getBlock(x, y, z));
}

bool Player::collides(const World& w, float x0, float y0, float z0, float x1, float y1, float z1) const {
    int bx0 = (int)std::floor(x0), by0 = (int)std::floor(y0), bz0 = (int)std::floor(z0);
    int bx1 = (int)std::floor(x1), by1 = (int)std::floor(y1), bz1 = (int)std::floor(z1);
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++)
            for (int bz = bz0; bz <= bz1; bz++)
                if (solidAt(w, bx, by, bz)) return true;
    return false;
}

static const float EPS = 1e-4f;

void Player::update(Input& in, World& world, float dt) {
    if (dt > 0.05f) dt = 0.05f;

    // toggle fly: double-tap space (creative, like vanilla)
    bool space = in.keys[VK_SPACE];
    spaceTapTimer_ += dt;
    if (space && !spaceHeld_) {
        if (spaceTapTimer_ < 0.3f) {
            flying = !flying;
            vel = Vec3(0, 0, 0);
            spaceTapTimer_ = 1.0f;
        } else {
            spaceTapTimer_ = 0.0f;
        }
    }
    spaceHeld_ = space;

    Vec3 fwd = cam.forward();
    Vec3 rt = cam.right();
    Vec3 mv(0, 0, 0);
    if (in.keys['W']) mv = mv + fwd;
    if (in.keys['S']) mv = mv - fwd;
    if (in.keys['D']) mv = mv + rt;
    if (in.keys['A']) mv = mv - rt;
    mv.y = 0;
    float ml = std::sqrt(mv.x * mv.x + mv.z * mv.z);
    if (ml > 1e-5f) { mv.x /= ml; mv.z /= ml; }

    const float walkSpeed = 4.5f;
    const float flySpeed = 14.0f;

    if (flying) {
        float speed = flySpeed * (in.keys[VK_SHIFT] ? 0.35f : 1.0f);
        float k = 1.0f - std::exp(-14.0f * dt);
        vel.x += (mv.x * speed - vel.x) * k;
        vel.z += (mv.z * speed - vel.z) * k;
        float ty = 0;
        if (in.keys[VK_SPACE]) ty = 1;
        if (in.keys[VK_SHIFT]) ty = -1;
        vel.y += (ty * speed - vel.y) * k;
        onGround = false;
    } else {
        float k = 1.0f - std::exp(-12.0f * dt);
        vel.x += (mv.x * walkSpeed - vel.x) * k;
        vel.z += (mv.z * walkSpeed - vel.z) * k;
        vel.y -= 28.0f * dt;
        if (vel.y < -40.0f) vel.y = -40.0f;
        if (in.keys[VK_SPACE] && onGround) {
            vel.y = 8.5f;
            onGround = false;
        }
    }

    // integrate with collision (cam.pos is the eye position; feet = eye - eyeHeight)
    Vec3 p = cam.pos;
    float hw = halfWidth, hh = height;
    float feetY = p.y - eyeHeight;

    // X axis
    p.x += vel.x * dt;
    if (collides(world, p.x - hw, feetY, p.z - hw, p.x + hw, feetY + hh, p.z + hw)) {
        if (vel.x > 0) p.x = std::floor(p.x + hw) - hw - EPS;
        else if (vel.x < 0) p.x = std::ceil(p.x - hw) + hw + EPS;
        vel.x = 0;
    }
    // Z axis
    p.z += vel.z * dt;
    if (collides(world, p.x - hw, feetY, p.z - hw, p.x + hw, feetY + hh, p.z + hw)) {
        if (vel.z > 0) p.z = std::floor(p.z + hw) - hw - EPS;
        else if (vel.z < 0) p.z = std::ceil(p.z - hw) + hw + EPS;
        vel.z = 0;
    }
    // Y axis
    p.y += vel.y * dt;
    feetY = p.y - eyeHeight;
    bool grounded = false;
    if (collides(world, p.x - hw, feetY, p.z - hw, p.x + hw, feetY + hh, p.z + hw)) {
        if (vel.y < 0) {
            p.y = std::ceil(feetY) + eyeHeight + EPS;
            grounded = true;
        } else if (vel.y > 0) {
            p.y = std::floor(feetY + hh) - hh + eyeHeight - EPS;
        }
        vel.y = 0;
    }
    onGround = grounded;

    // clamp to world
    if (p.y - eyeHeight < 0) { p.y = eyeHeight; vel.y = 0; }

    cam.pos = p;
}
