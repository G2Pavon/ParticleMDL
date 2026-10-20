#include "PreviewScene.hpp"

#include <algorithm>
#include <optional>

namespace gs {

namespace {

constexpr Rgba8 kGridColor{80, 80, 80, 80};
constexpr Rgba8 kBoundsColor{80, 80, 80, 255};
constexpr Rgba8 kAxisX{255, 50, 50, 200};
constexpr Rgba8 kAxisY{50, 255, 50, 200};
constexpr Rgba8 kAxisZ{50, 150, 255, 200};

// Line clipped against the camera's near plane.
void add_clipped_line(std::vector<SceneLine>& out, const OrbitCamera& cam, Vec3 p0, Vec3 p1, Point2 center,
                      Rgba8 color)
{
    CamPoint a = cam.to_camera_space(p0), b = cam.to_camera_space(p1);
    constexpr double near = OrbitCamera::kNear;
    if (a.depth < near && b.depth < near)
        return;
    if (a.depth < near || b.depth < near) {
        const double t = (near - a.depth) / (b.depth - a.depth);
        const CamPoint clip{a.right + (b.right - a.right) * t, a.up + (b.up - a.up) * t, near};
        (a.depth < near ? a : b) = clip;
    }
    const auto pa = cam.project_camera_space(a, center), pb = cam.project_camera_space(b, center);
    if (pa && pb)
        out.push_back({*pa, *pb, color, 1.0f});
}

void add_grid(Scene& out, const OrbitCamera& cam, Point2 center)
{
    constexpr int grid_size = 300, step = 50;
    for (int i = -grid_size; i <= grid_size; i += step) {
        add_clipped_line(out.lines, cam, {static_cast<double>(i), -grid_size, 0}, {static_cast<double>(i), grid_size, 0}, center, kGridColor);
        add_clipped_line(out.lines, cam, {-grid_size, static_cast<double>(i), 0}, {grid_size, static_cast<double>(i), 0}, center, kGridColor);
    }
}

void add_axes(Scene& out, const OrbitCamera& cam, Point2 center)
{
    constexpr double len = 50;
    const auto o = cam.project({0, 0, 0}, center);
    if (!o)
        return;
    const std::pair<Vec3, Rgba8> axes[] = {{{len, 0, 0}, kAxisX}, {{0, len, 0}, kAxisY}, {{0, 0, len}, kAxisZ}};
    for (const auto& [tip, color] : axes)
        if (const auto t = cam.project(tip, center))
            out.lines.push_back({o->point, t->point, color, 2.0f});
}

void add_bounds(Scene& out, const ParticleSystem& sys, const OrbitCamera& cam, double time, Point2 center)
{
    const std::array<Vec3, 8> corners = bounds_corners(sys, time);
    std::optional<Point2> proj[8];
    for (int i = 0; i < 8; ++i)
        if (const auto p = cam.project(corners[i], center))
            proj[i] = p->point;
    auto edge = [&](int a, int b) {
        if (proj[a] && proj[b])
            out.lines.push_back({*proj[a], *proj[b], kBoundsColor, 1.0f});
    };
    for (int i = 0; i < 4; ++i) {
        edge(i, (i + 1) % 4);
        edge(i + 4, (i + 1) % 4 + 4);
        edge(i, i + 4);
    }
}

constexpr Rgba8 kEmitterColor{120, 220, 255, 255};

// A point of the emitter's shape placed the way particles are: tilted by the
// emitter, scaled to the volume, then turned by the system spin.
Vec3 emitter_point(const ParticleSystem& sys, Vec3 p, double time)
{
    const Vec3 er = sys.emitter_rotation();
    p = ParticleSystem::apply_static_rotation(p, er.x, er.y, er.z);
    const Vec3 scale = sys.volume_scale();
    p = Vec3{p.x * scale.x, p.y * scale.y, p.z * scale.z};
    return sys.apply_macro_spin(p, time);
}

// The emission shape: the spawn disk or ring, the fan particles leave in, and
// the spawn height. Particles start on the disk and fly out within the fan.
void add_emitter(Scene& out, const ParticleSystem& sys, const OrbitCamera& cam, double time, Point2 center)
{
    constexpr int kSegments = 48;
    const Settings& s = sys.settings();
    const double fan_length = std::max(s.spawn_radius + 25.0, 40.0);   // outside the disk, so the fan stays visible
    auto line = [&](Vec3 a, Vec3 b) {
        add_clipped_line(out.overlay, cam, emitter_point(sys, a, time), emitter_point(sys, b, time), center,
                         kEmitterColor);
    };
    auto arc = [&](double radius, double from, double to) {
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = from + (to - from) * i / kSegments, a1 = from + (to - from) * (i + 1) / kSegments;
            line({radius * std::cos(a0), radius * std::sin(a0), 0}, {radius * std::cos(a1), radius * std::sin(a1), 0});
        }
    };

    if (s.spawn_radius > 0)
        arc(s.spawn_radius, 0, 2 * kPi);   // the disk's edge, or the ring itself

    // Particles fan out around +X: a sector of emission_angle, or the full circle.
    const double half = radians(s.emission_angle) / 2.0;
    if (s.emission_angle >= 360) {
        arc(fan_length, 0, 2 * kPi);
    } else {
        arc(fan_length, -half, half);
        line({0, 0, 0}, {fan_length * std::cos(-half), fan_length * std::sin(-half), 0});
        line({0, 0, 0}, {fan_length * std::cos(half), fan_length * std::sin(half), 0});
    }

    if (s.spawn_height > 0)
        line({0, 0, -s.spawn_height}, {0, 0, s.spawn_height});
}

void add_particles(Scene& out, const ParticleSystem& sys, const BakedPoses& baked, bool game_accurate,
                   const OrbitCamera& cam, double time, Point2 center)
{
    for (int i = 0; i < sys.num_particles(); ++i) {
        const SampledPose pose = game_accurate ? baked.sample(i, time) : to_sampled(sys.pose_at(sys.particles()[i], time));
        // A quad of no size (a collapsed vertex-bone quad) draws nothing.
        if (!pose.visible || pose.scale <= 0.0)
            continue;
        const Vec3 pos = pose.pos;
        // Vertex bones scale the quad; the texture itself never changes.
        const double scale_factor = pose.scale;

        for (const Quad& q : sys.quads()) {
            SceneFace face{};
            double depth_sum = 0;
            bool visible = true;
            for (int k = 0; k < 4; ++k) {
                const QuadVertex& v = q[k];
                const Vec3 local = rotate(pose.rot, v.pos * scale_factor);
                const auto projected = cam.project(pos + local, center);
                if (!projected) {
                    visible = false;
                    break;
                }
                face.pts[k] = projected->point;
                face.uvs[k] = {static_cast<float>(v.u), static_cast<float>(v.v)};
                depth_sum += projected->depth;
            }
            if (!visible)
                continue;

            // Back-face culling from the screen-space winding.
            const double cross =
                static_cast<double>(face.pts[1].x - face.pts[0].x) * static_cast<double>(face.pts[2].y - face.pts[0].y) -
                static_cast<double>(face.pts[1].y - face.pts[0].y) * static_cast<double>(face.pts[2].x - face.pts[0].x);
            if (!sys.settings().double_sided && cross > 0)
                continue;

            face.depth = depth_sum / 4.0;
            out.faces.push_back(face);
        }
    }

    std::stable_sort(out.faces.begin(), out.faces.end(),
                     [](const SceneFace& a, const SceneFace& b) { return a.depth > b.depth; });
}

} // namespace

std::array<Vec3, 8> bounds_corners(const ParticleSystem& sys, double time)
{
    const double w = sys.settings().volume_width / 2.0;
    const double d = sys.settings().volume_depth / 2.0;
    const double z_min = sys.min_z_raw() * sys.volume_scale().z;
    const double z_max = sys.max_z_raw() * sys.volume_scale().z;
    std::array<Vec3, 8> c = {{{-w, -d, z_min}, {w, -d, z_min}, {w, d, z_min}, {-w, d, z_min},
                              {-w, -d, z_max}, {w, -d, z_max}, {w, d, z_max}, {-w, d, z_max}}};
    for (Vec3& p : c)
        p = sys.apply_macro_spin(p, time);
    return c;
}

void build_scene(const ParticleSystem& sys, const BakedPoses& baked, const OrbitCamera& camera, double time,
                 Point2 center, const SceneOptions& options, Scene& out)
{
    out.lines.clear();
    out.faces.clear();
    out.overlay.clear();
    if (options.grid)
        add_grid(out, camera, center);
    if (options.axes)
        add_axes(out, camera, center);
    if (options.bounds)
        add_bounds(out, sys, camera, time, center);
    if (options.emitter)
        add_emitter(out, sys, camera, time, center);
    add_particles(out, sys, baked, options.game_accurate, camera, time, center);
}

} // namespace gs
