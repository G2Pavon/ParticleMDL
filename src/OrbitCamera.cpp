#include "OrbitCamera.hpp"

#include <algorithm>
#include <cmath>

namespace gs {

void OrbitCamera::reset() noexcept
{
    yaw_ = 3 * kPi / 4.0;   // 135 deg: camera in the +X, +Y quadrant
    pitch_ = kPi / 6.0;     // 30 deg look-down
    dist_ = 300.0;
}

void OrbitCamera::orbit(double dx, double dy) noexcept
{
    yaw_ += dx * 0.01;
    pitch_ = std::clamp(pitch_ + dy * 0.01, -kPi / 2 + 0.01, kPi / 2 - 0.01);
}

void OrbitCamera::zoom(double wheel) noexcept
{
    dist_ = std::clamp(dist_ - wheel * 50.0, 50.0, 3000.0);
}

CamPoint OrbitCamera::to_camera_space(Vec3 p) const noexcept
{
    const double cyaw = std::cos(yaw_), syaw = std::sin(yaw_);
    const double cpitch = std::cos(pitch_), spitch = std::sin(pitch_);

    const double x1 = p.x * cyaw - p.y * syaw;
    const double y1 = p.x * syaw + p.y * cyaw;
    const double z1 = p.z;

    const double x2 = x1 * cpitch - z1 * spitch;
    const double z2 = x1 * spitch + z1 * cpitch;

    return {y1, z2, x2 + dist_};
}

std::optional<Point2> OrbitCamera::project_camera_space(const CamPoint& c, Point2 center) const noexcept
{
    if (c.depth < kNear)
        return std::nullopt;
    return Point2{static_cast<float>(center.x - (c.right / c.depth) * focal_length_),
                  static_cast<float>(center.y - (c.up / c.depth) * focal_length_)};
}

std::optional<Projection> OrbitCamera::project(Vec3 p, Point2 center) const noexcept
{
    const CamPoint c = to_camera_space(p);
    if (const auto point = project_camera_space(c, center))
        return Projection{*point, c.depth};
    return std::nullopt;
}

} // namespace gs
