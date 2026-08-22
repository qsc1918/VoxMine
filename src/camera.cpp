#include "camera.hpp"
#include <cmath>

Vec3 Camera::forward() const {
    float cp = std::cos(pitch);
    return Vec3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp);
}

Vec3 Camera::right() const {
    Vec3 f = forward();
    Vec3 up(0, 1, 0);
    return normalize(cross(f, up));
}

Vec3 Camera::up() const {
    return normalize(cross(right(), forward()));
}

Mat4 Camera::view() const {
    return Mat4::lookAt(pos, pos + forward(), up());
}
