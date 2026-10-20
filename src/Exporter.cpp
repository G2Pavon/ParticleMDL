#include "Exporter.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <utility>
#include <stdexcept>
#include <vector>

#include "Baking.hpp"
#include "PathUtil.hpp"
#include "TextureGenerator.hpp"

namespace gs {

namespace {

namespace fs = std::filesystem;

std::string replace_all(std::string s, std::string_view from, std::string_view to)
{
    for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
        s.replace(pos, from.size(), to);
    return s;
}

// Writes every file as "<target>.tmp" first and renames them into place only
// once all of them were written, so a failed export leaves the folder as it was.
class StagedFiles {
public:
    explicit StagedFiles(fs::path dir) : dir_(std::move(dir)) {}
    StagedFiles(const StagedFiles&) = delete;
    StagedFiles& operator=(const StagedFiles&) = delete;

    ~StagedFiles()
    {
        std::error_code ec;
        for (const auto& [target, temp] : files_)
            fs::remove(temp, ec);
    }

    void add(const std::string& name, std::span<const char> bytes)
    {
        const fs::path target = dir_ / from_utf8(name);
        fs::path temp = target;
        temp += ".tmp";
        files_.emplace_back(target, temp);   // registered first: cleaned up even if the write fails
        std::ofstream f(temp, std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (!f)
            throw std::runtime_error("cannot write " + to_utf8(target));
    }

    void commit()
    {
        for (const auto& [target, temp] : files_) {
            std::error_code ec;
            if (fs::exists(target, ec) && !fs::is_regular_file(target, ec))
                throw std::runtime_error(to_utf8(target) + " exists and is not a regular file");
        }
        for (const auto& [target, temp] : files_)
            fs::rename(temp, target);
        files_.clear();
    }

private:
    fs::path dir_;
    std::vector<std::pair<fs::path, fs::path>> files_;   // target, temp
};

std::span<const char> as_chars(const std::vector<std::uint8_t>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

template <typename... Args>
void emit(std::string& out, std::format_string<Args...> fmt, Args&&... args)
{
    std::format_to(std::back_inserter(out), fmt, std::forward<Args>(args)...);
}

void write_nodes(std::string& out, const ParticleSystem& sys)
{
    out += "version 1\nnodes\n  0 \"root\" -1\n";
    for (const Particle& p : sys.particles()) {
        emit(out, "  {} \"p_{}\" 0\n", p.base_bone, p.base_bone);
        for (int c = 0; c < sys.corners_per_particle(); ++c) {
            const int cb = ParticleSystem::corner_bone(p, c);
            emit(out, "  {} \"c_{}\" {}\n", cb, cb, p.base_bone);
        }
    }
    out += "end\n";
}

std::string make_qc(const ParticleSystem& sys, const BakedPoses& baked, const std::string& safe,
                    const std::string& tex_ref)
{
    std::string out;
    emit(out, "$modelname \"{}.mdl\"\n$cd \".\"\n$cdtexture \".\"\n$scale 1.0\n\n", safe);

    // The exact extent of the animation with a unit of margin. studiomdl
    // computes the sequence box (used for culling) itself; this is the header box.
    const ParticleSystem::Bounds b = animation_bounds(sys, baked);
    emit(out, "$bbox {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}\n\n", std::floor(b.min.x) - 1, std::floor(b.min.y) - 1,
         std::floor(b.min.z) - 1, std::ceil(b.max.x) + 1, std::ceil(b.max.y) + 1, std::ceil(b.max.z) + 1);

    emit(out, "$texrendermode \"{}\" additive\n\n", tex_ref);
    emit(out, "$body \"studio\" \"{}_ref\"\n\n", safe);
    emit(out, "$sequence \"idle\" \"{}_anim\" loop fps {}\n", safe, static_cast<int>(sys.export_fps()));
    return out;
}

std::string make_ref_smd(const ParticleSystem& sys, const std::string& tex_ref)
{
    // Up to 4 triangles per quad, each a material line and three ~80-byte vertex lines.
    const std::size_t quads = static_cast<std::size_t>(sys.num_particles()) * sys.quads().size();
    std::string out;
    out.reserve(quads * 4 * (tex_ref.size() + 1 + 3 * 80) + 4096);
    write_nodes(out, sys);
    out += "skeleton\n  time 0\n    0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n";
    for (const Particle& p : sys.particles()) {
        emit(out, "    {} 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n", p.base_bone);
        for (int c = 0; c < sys.corners_per_particle(); ++c)
            emit(out, "    {} 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n",
                 ParticleSystem::corner_bone(p, c));
    }
    out += "end\ntriangles\n";

    // Pre-rotate by -90 deg around Z to cancel studiomdl's +90 deg: (x, y) -> (y, -x).
    auto write_vertex = [&](int bone, const QuadVertex& v) {
        emit(out, "  {} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {} {}\n", bone, v.pos.y, -v.pos.x, v.pos.z,
             v.normal.y, -v.normal.x, v.normal.z, v.u, v.v);
    };
    auto write_tri = [&](int b0, const QuadVertex& v0, int b1, const QuadVertex& v1, int b2, const QuadVertex& v2) {
        out += tex_ref;
        out += '\n';
        write_vertex(b0, v0);
        write_vertex(b1, v1);
        write_vertex(b2, v2);
    };

    const bool vertex_bones = sys.corners_per_particle() > 0;
    const bool back_faces = sys.settings().double_sided && sys.settings().geometry != Geometry::VolumetricBox;
    for (const Particle& p : sys.particles()) {
        for (int qi = 0; qi < static_cast<int>(sys.quads().size()); ++qi) {
            const Quad& q = sys.quads()[qi];
            // Each corner hangs on its own bone with vertex bones, else on the particle's.
            auto bone = [&](int k) { return vertex_bones ? ParticleSystem::corner_bone(p, qi * 4 + k) : p.base_bone; };
            const int b0 = bone(0), b1 = bone(1), b2 = bone(2), b3 = bone(3);

            write_tri(b0, q[0], b1, q[1], b2, q[2]);
            write_tri(b0, q[0], b2, q[2], b3, q[3]);
            if (back_faces) {
                write_tri(b2, q[2], b1, q[1], b0, q[0]);
                write_tri(b3, q[3], b2, q[2], b0, q[0]);
            }
        }
    }
    out += "end\n";
    return out;
}

std::string make_anim_smd(const ParticleSystem& sys, const AnimationTable& table)
{
    // One ~64-byte line per bone per frame.
    std::string out;
    out.reserve(static_cast<std::size_t>(table.frames) * (static_cast<std::size_t>(table.bones) * 64 + 16) +
                static_cast<std::size_t>(table.bones) * 32);
    write_nodes(out, sys);
    out += "skeleton\n";
    for (int frame = 0; frame < table.frames; ++frame) {
        emit(out, "  time {}\n", frame);
        for (int bone = 0; bone < table.bones; ++bone) {
            const auto& v = table.at(frame, bone);
            emit(out, "    {} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f}\n", bone, v[0], v[1], v[2], v[3], v[4], v[5]);
        }
    }
    out += "end\n";
    return out;
}

// --- studiomdl's animation encoding (studiomdl.cpp, writemdl.cpp) -----------

// The float studiomdl reads back from a value written as "%.6f".
float as_read_by_studiomdl(double v)
{
    if (std::abs(v) < 5e-7)   // prints as (-)0.000000; the sign does not matter here
        return 0.0f;
    std::array<char, 64> buf;
    const auto written = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, 6);
    float f = 0;
    std::from_chars(buf.data(), written.ptr, f);
    return f;
}

} // namespace

// Number of 2-byte entries the run-length coder emits for one channel, or 0
// when the channel is dropped (all zero). Mirrors studiomdl's loop exactly.
std::size_t encoded_entries(std::span<const short> value)
{
    const std::size_t n = value.size();
    std::size_t entries = 2;    // first count entry and first value
    int valid = 1, total = 1;   // of the current count entry
    for (std::size_t m = 1; m < n; ++m) {
        if (total == 255) {
            // run full: a new count entry with this value
            entries += 2;
            valid = 1;
            total = 0;
        } else if (value[m] != value[m - 1] ||
                   (total == valid && m < n - 1 && value[m] != value[m + 1])) {
            if (total != valid) {   // leaving a repeat run: new count entry
                entries += 1;
                valid = 0;
                total = 0;
            }
            ++valid;
            entries += 1;
        }
        ++total;
    }
    return (entries == 2 && value[0] == 0) ? 0 : entries;
}

AnimationTable animation_table(const ParticleSystem& sys)
{
    return animation_table(sys, bake(sys));
}

AnimationTable animation_table(const ParticleSystem& sys, const BakedPoses& baked)
{
    AnimationTable t;
    t.frames = sys.export_frames();
    t.bones = 1 + sys.num_particles() * (1 + sys.corners_per_particle());
    t.values.assign(static_cast<std::size_t>(t.frames) * t.bones, {});   // root stays at zero
    for (int frame = 0; frame < t.frames; ++frame) {
        for (int i = 0; i < sys.num_particles(); ++i) {
            const Particle& p = sys.particles()[static_cast<std::size_t>(i)];
            // Every bone in every frame: studiomdl puts a missing bone at the origin.
            const ParticlePose& pose = baked.at(frame, i);
            const Vec3 rot = sys.has_micro_rotation() ? studio_angles(pose.rot) : Vec3{};
            // Same -90 deg Z correction for translations as for the geometry.
            t.values[static_cast<std::size_t>(frame) * t.bones + p.base_bone] = {pose.pos.y, -pose.pos.x, pose.pos.z,
                                                                                rot.x, rot.y, rot.z};
            // Corner bones shift each corner so the quad appears scaled.
            int corner = 0;
            for (const Quad& q : sys.quads()) {
                if (sys.corners_per_particle() == 0)
                    break;
                for (const QuadVertex& v : q) {
                    const Vec3 offset = v.pos * (pose.scale - 1.0);
                    const int bone = ParticleSystem::corner_bone(p, corner++);
                    t.values[static_cast<std::size_t>(frame) * t.bones + bone] = {offset.y, -offset.x, offset.z,
                                                                                 0.0, 0.0, 0.0};
                }
            }
        }
    }
    return t;
}

std::size_t sequence_bytes(const AnimationTable& table)
{
    constexpr double kPiD = 3.14159265358979323846;   // studiomdl's Q_PI
    constexpr float kRootRotate = static_cast<float>(90.0 * kPiD / 180.0);   // default $rotate
    constexpr std::size_t kOffsetEntry = 6 * sizeof(std::uint16_t);          // per bone

    std::vector<float> v(static_cast<std::size_t>(table.frames));
    std::vector<short> quantized(v.size());
    std::size_t data_bytes = 0, worst = 0;
    for (int bone = 0; bone < table.bones; ++bone) {
        for (int k = 0; k < 6; ++k) {
            for (int f = 0; f < table.frames; ++f) {
                float x = as_read_by_studiomdl(table.at(f, bone)[k]);
                if (bone == 0 && k == 5)
                    x += kRootRotate;   // $rotate turns the root bone
                if (k >= 3) {           // rotations wrapped to [-pi, pi)
                    if (x >= kPiD)
                        x = static_cast<float>(x - kPiD * 2);
                    if (x < -kPiD)
                        x = static_cast<float>(x + kPiD * 2);
                }
                v[static_cast<std::size_t>(f)] = x;
            }
            // Per-channel 16-bit scale from the range, which starts at +-128
            // units or +-pi/8 radians.
            float minv = k < 3 ? -128.0f : static_cast<float>(-kPiD / 8.0);
            float maxv = k < 3 ? 128.0f : static_cast<float>(kPiD / 8.0);
            for (const float x : v) {
                minv = std::min(minv, x);
                maxv = std::max(maxv, x);
            }
            const float scale = (-minv > maxv) ? minv / -32768.0f : maxv / 32767.0f;
            std::ranges::transform(v, quantized.begin(), [scale](float x) { return static_cast<short>(x / scale); });
            data_bytes += encoded_entries(quantized) * sizeof(short);
        }
        // The limit is checked after each bone, from that bone's offset entry.
        worst = std::max(worst, kOffsetEntry * static_cast<std::size_t>(table.bones - bone) + data_bytes);
    }
    return worst;
}

std::size_t sequence_bytes(const ParticleSystem& sys)
{
    return sequence_bytes(animation_table(sys));
}

// studiomdl rotates the root bone by +90 deg about Z, so a child bone's
// rotation must be M = Rz(-90) * R * Rz(90) to end up as R in game, and the
// engine reads SMD angles (x, y, z) as Rz(z) * Ry(y) * Rx(x). Swapping single
// angles only works for rotations about one axis; this goes through the matrix.
Vec3 studio_angles(Vec3 r)
{
    auto R = [&](Vec3 v) { return ParticleSystem::apply_static_rotation(v, r.x, r.y, r.z); };
    auto rz_minus_90 = [](Vec3 v) { return Vec3{v.y, -v.x, v.z}; };
    // Columns of M: M * e = Rz(-90) * R * (Rz(90) * e).
    const Vec3 c0 = rz_minus_90(R({0, 1, 0}));    // Rz(90) * x = y
    const Vec3 c1 = rz_minus_90(R({-1, 0, 0}));   // Rz(90) * y = -x
    const Vec3 c2 = rz_minus_90(R({0, 0, 1}));
    // M = Rz(z) * Ry(y) * Rx(x): M[2][0] = -sin(y), M[2][1] = cos(y) sin(x),
    // M[2][2] = cos(y) cos(x), M[1][0] = cos(y) sin(z), M[0][0] = cos(y) cos(z).
    const double y = std::asin(std::clamp(-c0.z, -1.0, 1.0));
    if (std::abs(c0.z) < 1.0 - 1e-12)
        return {std::atan2(c1.z, c2.z), y, std::atan2(c0.y, c0.x)};
    // Gimbal lock (y = +-90 deg): only x - z or x + z is defined; put it all in x.
    return {std::atan2(-c2.y, c1.y), y, 0.0};
}

std::string safe_model_name(const std::string& model_name)
{
    return replace_all(replace_all(model_name, " ", "_"), ".mdl", "");
}

std::optional<std::string> model_name_problem(std::string_view name)
{
    if (name.find_first_not_of(' ') == std::string_view::npos)
        return "Model name is empty";
    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string_view::npos)
        return "Model name must not contain / \\ : or be . or ..";
    if (std::ranges::any_of(name, [](char c) { return c == '"' || static_cast<unsigned char>(c) < 0x20; }))
        return "Model name must not contain quotes or control characters";
    // The name becomes the .mdl and texture file names, which studiomdl and GoldSrc read as plain ANSI bytes.
    if (std::ranges::any_of(name, [](char c) { return static_cast<unsigned char>(c) >= 0x80; }))
        return "Model name must use plain ASCII characters (GoldSrc cannot read other model names)";
    if (name.size() > static_cast<std::size_t>(kMaxModelNameLength))
        return "Model name is too long (max " + std::to_string(kMaxModelNameLength) + " characters)";
    return std::nullopt;
}

namespace {

std::string kilobytes(std::size_t bytes)
{
    return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
}

} // namespace

ExportReport analyze_export(const ParticleSystem& sys)
{
    return analyze_export(sys, animation_table(sys), bake(sys));
}

ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table)
{
    return analyze_export(sys, table, bake(sys));
}

ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table, const BakedPoses& baked)
{
    const Settings& s = sys.settings();
    ExportReport r;
    r.frames = table.frames;
    r.sequence_bytes = sequence_bytes(table);

    if (r.frames > kMaxStudioFrames) {
        // Frames are lround(duration * fps) + 1, so this many steps fit.
        const double longest = std::floor((kMaxStudioFrames - 1) / static_cast<double>(s.fps) * 10.0) / 10.0;
        r.blockers.push_back({ExportBlocker::Kind::TooManyFrames,
                              std::format("{} frames: studiomdl accepts at most {} per sequence. At {} fps the longest "
                                          "Duration is {:.1f} s (or lower the FPS).",
                                          r.frames, kMaxStudioFrames, s.fps, longest)});
    }
    if (r.sequence_bytes > kMaxSequenceBytes) {
        std::string message = std::format(
            "Animation data is {}: studiomdl accepts at most {} per sequence ({} frames x {} bones). "
            "Lower Particle Count, Duration or FPS.",
            kilobytes(r.sequence_bytes), kilobytes(kMaxSequenceBytes), r.frames, table.bones);
        if (sys.corners_per_particle() > 0)
            message += std::format(" Vertex-bone scaling uses {} bones per particle: turning it off or using fewer "
                                   "quads saves the most.",
                                   1 + sys.corners_per_particle());
        r.blockers.push_back({ExportBlocker::Kind::SequenceTooLarge, std::move(message)});
    }

    if (s.end_behavior == EndBehavior::HideInCore && s.scale_mode == ScaleMode::Static)
        r.warnings.push_back("Hide in Core needs Vertex Bone scaling: static particles stay visible at the center");
    else if (s.scale_mode == ScaleMode::Static && baked.unhidden_jump() > std::max(s.quad_width, s.quad_height) &&
             (s.end_behavior == EndBehavior::EndlessLoop || s.end_behavior == EndBehavior::StopAndStay))
        r.warnings.push_back("Static quads cannot hide a respawn: each particle visibly jumps back to its start for one "
                             "frame. Vertex Bone scaling removes the jump.");
    return r;
}

ExportBlocked::ExportBlocked(std::vector<ExportBlocker> blockers)
    : std::runtime_error(blockers.empty() ? "export blocked" : blockers.front().message), blockers_(std::move(blockers))
{
}

void export_model(const ParticleSystem& sys, const fs::path& out_dir)
{
    const std::string& model_name = sys.settings().model_name;
    if (const auto problem = model_name_problem(model_name))
        throw std::invalid_argument(*problem);
    // Every file, the texture included, uses the sanitized name, so the QC,
    // the SMDs and the cleanup after compiling all agree.
    const std::string safe = safe_model_name(model_name);
    const std::string tex_ref = safe + ".bmp";

    std::error_code ec;
    if (!fs::is_directory(out_dir, ec))
        throw std::runtime_error("export folder does not exist: " + to_utf8(out_dir));

    // The animation is built once: the limits are checked against it, and it
    // becomes the animation SMD.
    const BakedPoses baked = bake(sys);
    const AnimationTable table = animation_table(sys, baked);
    if (ExportReport report = analyze_export(sys, table, baked); !report.blockers.empty())
        throw ExportBlocked(std::move(report.blockers));

    StagedFiles files(out_dir);
    files.add(tex_ref, as_chars(encode_indexed_bmp(export_texture(sys.settings()))));
    files.add(safe + ".qc", make_qc(sys, baked, safe, tex_ref));
    files.add(safe + "_ref.smd", make_ref_smd(sys, tex_ref));
    files.add(safe + "_anim.smd", make_anim_smd(sys, table));
    files.commit();
}

} // namespace gs
