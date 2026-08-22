#pragma once
#include "util.hpp"

struct Camera {
    Vec3 pos{0, 70, 0};
    float yaw = 0.0f;    // radians
    float pitch = 0.0f;  // radians

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    Mat4 view() const;
    void addYaw(float d) { yaw += d; }
    void addPitch(float d) {
        pitch += d;
        if (pitch > 1.55f) pitch = 1.55f;
        if (pitch < -1.55f) pitch = -1.55f;
    }
};
