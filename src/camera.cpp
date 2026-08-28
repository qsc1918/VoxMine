#include "camera.hpp"
#include <cmath>

void Camera::updateBasis() const {
    if (!dirty_) return;
    dirty_ = false;
    float cp = std::cos(pitch);
    fwd_ = Vec3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp);
    Vec3 up0(0, 1, 0);
    rt_ = normalize(cross(fwd_, up0));
    up_ = normalize(cross(rt_, fwd_));
}

Vec3 Camera::forward() const { updateBasis(); return fwd_; }
Vec3 Camera::right() const { updateBasis(); return rt_; }
Vec3 Camera::up() const { updateBasis(); return up_; }

Mat4 Camera::view() const {
    updateBasis();
    return Mat4::lookAt(pos, pos + fwd_, up_);
}
