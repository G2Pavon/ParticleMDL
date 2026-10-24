#include "PreviewScene.hpp"

#include <algorithm>
#include <optional>

#include "Skybox.hpp"

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


void add_particles(Scene& out, const ParticleSystem& sys, const BakedPoses& baked, const OrbitCamera& cam, double time,
                   Point2 center)
{
    for (int i = 0; i < sys.num_particles(); ++i) {
        const SampledPose pose = baked.sample(i, time);
        // A quad of no area (a collapsed vertex-bone quad, or one folded flat) draws nothing.
        if (!pose.visible || !ParticleSystem::has_area(pose.scale, pose.fold))
            continue;
        const Vec3 pos = pose.pos;
        // Vertex bones scale the quad; the texture itself never changes.
        const double scale_factor = pose.scale;

        for (const Quad& q : sys.quads_of(i)) {
            SceneFace face{};
            double depth_sum = 0;
            bool visible = true;
            for (int k = 0; k < 4; ++k) {
                const QuadVertex& v = q[k];
                const Vec3 local = rotate(pose.rot, (v.pos + sys.fold_offset(k, pose.fold)) * scale_factor);
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

void add_sky(std::vector<SkyQuad>& out, const OrbitCamera& camera, Point2 center)
{
    // Each corner projected once per face: (kSkyGrid + 1)^2 of them.
    constexpr int n = kSkyGrid + 1;
    std::array<std::optional<Point2>, n * n> pts;
    std::array<Point2, n * n> uvs;
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const SkyPoint p = sky_point(face, -1.0 + 2.0 * i / kSkyGrid, -1.0 + 2.0 * j / kSkyGrid);
                const CamPoint c = camera.direction_to_camera_space(p.dir);
                const std::size_t k = static_cast<std::size_t>(j * n + i);
                // In front of the camera by a margin, so no piece reaches across it.
                pts[k] = c.depth > 0.05 ? camera.project_camera_space(c, center) : std::nullopt;
                uvs[k] = {static_cast<float>(p.uv.x), static_cast<float>(p.uv.y)};
            }
        for (int j = 0; j < kSkyGrid; ++j)
            for (int i = 0; i < kSkyGrid; ++i) {
                const std::size_t k[4] = {static_cast<std::size_t>(j * n + i), static_cast<std::size_t>(j * n + i + 1),
                                          static_cast<std::size_t>((j + 1) * n + i + 1),
                                          static_cast<std::size_t>((j + 1) * n + i)};
                if (!pts[k[0]] || !pts[k[1]] || !pts[k[2]] || !pts[k[3]])
                    continue;
                out.push_back({sky_face_side(face), {*pts[k[0]], *pts[k[1]], *pts[k[2]], *pts[k[3]]},
                               {uvs[k[0]], uvs[k[1]], uvs[k[2]], uvs[k[3]]}});
            }
    }
}

void build_scene(const ParticleSystem& sys, const BakedPoses& baked, const OrbitCamera& camera, double time,
                 Point2 center, const SceneOptions& options, Scene& out)
{
    out.sky.clear();
    out.lines.clear();
    out.faces.clear();
    if (options.sky)
        add_sky(out.sky, camera, center);
    if (options.grid)
        add_grid(out, camera, center);
    if (options.axes)
        add_axes(out, camera, center);
    if (options.bounds)
        add_bounds(out, sys, camera, time, center);
    add_particles(out, sys, baked, camera, time, center);
}

} // namespace gs
