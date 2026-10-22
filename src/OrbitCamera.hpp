#pragma once

#include <optional>

#include "MathUtil.hpp"

namespace gs {

// Screen position in pixels (same layout and precision as ImVec2).
struct Point2 {
    float x = 0.0f, y = 0.0f;
};

struct CamPoint {
    double right, up, depth;
};

struct Projection {
    Point2 point;
    double depth;   // distance in front of the camera
};

// Orbit camera of the preview: yaw/pitch around the origin (the emitter, on
// the grid), so the grid stays where it is when the effect changes size;
// perspective projection with a fixed focal length.
class OrbitCamera {
public:
    static constexpr double kNear = 0.1;
    static constexpr double kFocalLength = 500.0;   // pixels, at UI scale 1
    // Farthest: half the default size, where the largest volume (256 on each axis) still fits with
    // room around it, its far corner about 314 units from the origin.
    static constexpr double kDefaultDistance = 300.0, kMinDistance = 50.0, kMaxDistance = 600.0;

    OrbitCamera() noexcept { reset(); }

    void reset() noexcept;
    // Mouse drag in pixels; pitch is clamped just short of straight up/down.
    void orbit(double dx, double dy) noexcept;
    // Mouse wheel notches; distance is clamped to [kMinDistance, kMaxDistance].
    void zoom(double wheel) noexcept;
    // Focal length in pixels (grows with the UI scale on HiDPI screens).
    void set_focal_length(double f) noexcept { focal_length_ = f; }

    double yaw() const noexcept { return yaw_; }
    double pitch() const noexcept { return pitch_; }
    double distance() const noexcept { return dist_; }
    // How much larger than in the default view things look: 1 after reset(), 6 closest, 0.5 farthest.
    double zoom_factor() const noexcept { return kDefaultDistance / dist_; }

    CamPoint to_camera_space(Vec3 p) const noexcept;
    // A direction seen from the camera: turned like a point, but not moved, as a point infinitely
    // far away (the sky) would be.
    CamPoint direction_to_camera_space(Vec3 d) const noexcept;
    // nullopt when the point is behind the near plane.
    std::optional<Point2> project_camera_space(const CamPoint& c, Point2 center) const noexcept;
    std::optional<Projection> project(Vec3 p, Point2 center) const noexcept;

private:
    double yaw_ = 0, pitch_ = 0, dist_ = 0;
    double focal_length_ = kFocalLength;
};

} // namespace gs
