#include "Exporter.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
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
#include "StagedFiles.hpp"
#include "TextureGenerator.hpp"

namespace gs {

namespace {

namespace fs = std::filesystem;

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

    // How the game draws the texture: one line for each flag.
    const TextureSettings& texture = sys.settings().texture;
    bool any_flag = false;
    auto render_mode = [&](const char* mode) {
        emit(out, "$texrendermode \"{}\" {}\n", tex_ref, mode);
        any_flag = true;
    };
    if (texture.blend == BlendMode::Additive)
        render_mode("additive");
    else if (texture.blend == BlendMode::Masked)
        render_mode("masked");
    if (texture.fullbright)
        render_mode("fullbright");
    if (any_flag)
        out += "\n";
    emit(out, "$body \"studio\" \"{}_ref\"\n\n", safe);
    emit(out, "$sequence \"idle\" \"{}_anim\"{} fps {}\n", safe, sys.looping() ? " loop" : "", static_cast<int>(sys.export_fps()));
    return out;
}

std::string make_ref_smd(const ParticleSystem& sys, const std::string& tex_ref)
{
    // Up to 4 triangles per quad, each a material line and three ~80-byte vertex lines.
    std::size_t quads = 0;
    for (int i = 0; i < sys.num_particles(); ++i)
        quads += sys.quads_of(i).size();
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
    // Static quads have their size in the mesh; with vertex bones the mesh is at scale 1 and the bones scale it.
    double size = 1.0;
    auto write_vertex = [&](int bone, const QuadVertex& v) {
        emit(out, "  {} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {} {}\n", bone, v.pos.y * size, -v.pos.x * size,
             v.pos.z * size, v.normal.y, -v.normal.x, v.normal.z, v.u, v.v);
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
    for (int pi = 0; pi < sys.num_particles(); ++pi) {
        const Particle& p = sys.particles()[static_cast<std::size_t>(pi)];
        const std::vector<Quad>& quads_of_p = sys.quads_of(pi);
        size = vertex_bones ? 1.0 : p.size;
        for (int qi = 0; qi < static_cast<int>(quads_of_p.size()); ++qi) {
            const Quad& q = quads_of_p[static_cast<std::size_t>(qi)];
            // Each corner hangs on a bone of its own with vertex bones (quads that meet share it),
            // else on the particle's.
            auto bone = [&](int k) {
                return vertex_bones ? ParticleSystem::corner_bone(p, sys.corner_of(qi, k)) : p.base_bone;
            };
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

float as_read_by_studiomdl(double v)
{
    if (std::abs(v) < 5e-7)   // prints as (-)0.000000; the sign does not matter here
        return 0.0f;

    // Printing "%.6f" rounds v to a multiple of 1e-6, and reading that back gives the float
    // nearest to it. Unless v * 1e6 is almost exactly half way between two integers, that
    // multiple is nearbyint(v * 1e6): the product is off by far less than the distance to the
    // tie (|v| < 1e5 keeps it below 1.2e-5), and r / 1e6 is then the double nearest the
    // decimal value, whose rounding to float is the same as the decimal's (no 6-digit decimal
    // is near enough to a float's midpoint for the two roundings to differ).
    if (std::abs(v) < 1e5) {
        const double scaled = v * 1e6;
        const double r = std::nearbyint(scaled);
        if (0.5 - std::abs(scaled - r) > 1e-3)
            return static_cast<float>(r / 1e6);
    }

    std::array<char, 64> buf;
    const auto written = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, 6);
    float f = 0;
    std::from_chars(buf.data(), written.ptr, f);
    return f;
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
            for (int corner = 0; corner < sys.corners_per_particle(); ++corner) {
                const Vec3 offset = sys.corners()[static_cast<std::size_t>(corner)] * (pose.scale - 1.0);
                const int bone = ParticleSystem::corner_bone(p, corner);
                t.values[static_cast<std::size_t>(frame) * t.bones + bone] = {offset.y, -offset.x, offset.z, 0.0, 0.0, 0.0};
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

    const std::size_t frames = static_cast<std::size_t>(table.frames);
    std::array<std::vector<float>, 6> channel;   // one bone's six channels over all frames
    for (auto& c : channel)
        c.resize(frames);
    std::vector<short> quantized(frames);

    std::size_t data_bytes = 0, worst = 0;
    for (int bone = 0; bone < table.bones; ++bone) {
        // One pass over the frames for all six channels: the table is frame-major, so reading a
        // channel at a time would stride through it six times.
        for (std::size_t f = 0; f < frames; ++f) {
            const std::array<double, 6>& row = table.at(static_cast<int>(f), bone);
            for (std::size_t k = 0; k < 6; ++k) {
                float x = as_read_by_studiomdl(row[k]);
                if (bone == 0 && k == 5)
                    x += kRootRotate;   // $rotate turns the root bone
                if (k >= 3) {           // rotations wrapped to [-pi, pi)
                    // studiomdl compares the float with a double pi, so this does too.
                    if (static_cast<double>(x) >= kPiD)
                        x = static_cast<float>(static_cast<double>(x) - kPiD * 2);
                    if (static_cast<double>(x) < -kPiD)
                        x = static_cast<float>(static_cast<double>(x) + kPiD * 2);
                }
                channel[k][f] = x;
            }
        }
        for (std::size_t k = 0; k < 6; ++k) {
            // Per-channel 16-bit scale from the range, which starts at +-128
            // units or +-pi/8 radians.
            float minv = k < 3 ? -128.0f : static_cast<float>(-kPiD / 8.0);
            float maxv = k < 3 ? 128.0f : static_cast<float>(kPiD / 8.0);
            for (const float x : channel[k]) {
                minv = std::min(minv, x);
                maxv = std::max(maxv, x);
            }
            const float scale = (-minv > maxv) ? minv / -32768.0f : maxv / 32767.0f;
            std::ranges::transform(channel[k], quantized.begin(), [scale](float x) { return static_cast<short>(x / scale); });
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
    std::string safe = model_name;
    std::ranges::replace(safe, ' ', '_');
    // Only a trailing ".mdl" (in any case) is the extension; "fx.mdl_v2" keeps what it says.
    auto is_mdl = [&] {
        return safe.size() >= 4 && std::ranges::equal(std::string_view(safe).substr(safe.size() - 4), std::string_view(".mdl"),
                                                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
    };
    while (is_mdl())
        safe.resize(safe.size() - 4);
    return safe;
}

std::optional<std::string> model_name_problem(std::string_view name)
{
    if (name.find_first_not_of(' ') == std::string_view::npos)
        return "Model name is empty";
    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string_view::npos)
        return "Model name must not contain / \\ : or be . or ..";
    if (std::ranges::any_of(name, [](char c) { return c == '"' || static_cast<unsigned char>(c) < 0x20; }))
        return "Model name must not contain quotes or control characters";
    // Characters Windows does not allow in a file name.
    if (name.find_first_of("<>|?*") != std::string_view::npos)
        return "Model name must not contain < > | ? or *";
    // The name becomes the .mdl and texture file names, which studiomdl and GoldSrc read as plain ANSI bytes.
    if (std::ranges::any_of(name, [](char c) { return static_cast<unsigned char>(c) >= 0x80; }))
        return "Model name must use plain ASCII characters (GoldSrc cannot read other model names)";
    if (safe_model_name(std::string(name)).empty())
        return "Model name is empty (\".mdl\" is only the extension)";
    // Windows keeps these names for devices, with or without an extension: "nul.qc" cannot be created.
    {
        std::string base = safe_model_name(std::string(name)).substr(0, safe_model_name(std::string(name)).find('.'));
        std::ranges::transform(base, base.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        static constexpr std::string_view devices[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
                                                       "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
                                                       "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        if (std::ranges::find(devices, base) != std::end(devices))
            return "Model name \"" + base + "\" is reserved by Windows";
    }
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
    r.vertices = sys.vertex_count();

    if (r.frames > kMaxStudioFrames) {
        // Frames are lround(duration * fps) + 1, so this many steps fit.
        const double longest = std::floor((kMaxStudioFrames - 1) / static_cast<double>(s.fps) * 10.0) / 10.0;
        r.blockers.push_back({ExportBlocker::Kind::TooManyFrames,
                              std::format("{} frames: studiomdl keeps only the first {} of a sequence, which would cut the "
                                          "effect short. At {} fps the longest Duration is {:.1f} s (or lower the FPS).",
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
    if (r.vertices > kMaxStudioVertices) {
        std::string message = std::format("{} vertices: studiomdl accepts at most {} in a model. Lower Particle Count",
                                          r.vertices, kMaxStudioVertices);
        message += s.cluster_size > 1 ? ", Cluster Size or use geometry with fewer quads." : " or use geometry with fewer quads.";
        r.blockers.push_back({ExportBlocker::Kind::TooManyVertices, std::move(message)});
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

std::vector<std::string> compare_with_export(const MdlInfo& mdl, const ParticleSystem& sys)
{
    std::vector<std::string> problems;
    const int bones = 1 + sys.num_particles() * (1 + sys.corners_per_particle());
    if (static_cast<int>(mdl.bones.size()) != bones)
        problems.push_back(std::format("the model has {} bones, the export has {}", mdl.bones.size(), bones));

    if (mdl.sequences.size() != 1) {
        problems.push_back(std::format("the model has {} sequences, the export has 1", mdl.sequences.size()));
    } else {
        const MdlSequence& seq = mdl.sequences.front();
        if (seq.frames != sys.export_frames())
            problems.push_back(std::format("the sequence has {} frames, the export has {}", seq.frames, sys.export_frames()));
        if (static_cast<double>(seq.fps) != sys.export_fps())
            problems.push_back(std::format("the sequence runs at {:g} fps, the export says {:g}", static_cast<double>(seq.fps),
                                           sys.export_fps()));
        if (((seq.flags & kMdlSequenceLooping) != 0) != sys.looping())
            problems.push_back(sys.looping() ? "the sequence does not loop" : "the sequence loops, the export does not");
        if (!seq.external) {
            const std::size_t measured = seq.value_bytes + 12;   // studiomdl's measure: from the last offset entry
            const std::size_t predicted = sequence_bytes(sys);
            if (measured != predicted)
                problems.push_back(std::format("studiomdl wrote {} bytes of animation values where the editor predicted {}",
                                               measured, predicted));
        }
    }

    int vertices = 0;
    for (const MdlModel& model : mdl.models)
        vertices += model.vertices;
    if (vertices != sys.vertex_count())
        problems.push_back(std::format("the model has {} vertices, the export has {}", vertices, sys.vertex_count()));

    if (mdl.textures.size() != 1) {
        problems.push_back(std::format("the model has {} textures, the export has 1", mdl.textures.size()));
    } else {
        const MdlTexture& tex = mdl.textures.front();
        const TextureSettings& texture = sys.settings().texture;
        const int wanted = (texture.blend == BlendMode::Additive ? kMdlTextureAdditive : 0) |
                           (texture.blend == BlendMode::Masked ? kMdlTextureMasked : 0) |
                           (texture.fullbright ? kMdlTextureFullbright : 0);
        const int mine = kMdlTextureAdditive | kMdlTextureMasked | kMdlTextureFullbright;
        if ((tex.flags & mine) != wanted)
            problems.push_back(std::format("the texture has the render flags {:#x}, the export asks for {:#x}", tex.flags & mine, wanted));
        const int size = sys.settings().texture.size;
        if (tex.width != size || tex.height != size)
            problems.push_back(std::format("the texture is {} x {}, the export has {} x {}", tex.width, tex.height, size, size));
    }
    return problems;
}

void export_model(const ParticleSystem& sys, const fs::path& out_dir)
{
    const BakedPoses baked = bake(sys);
    export_model(sys, baked, animation_table(sys, baked), out_dir);
}

void export_model(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table, const fs::path& out_dir)
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

    if (ExportReport report = analyze_export(sys, table, baked); !report.blockers.empty())
        throw ExportBlocked(std::move(report.blockers));

    StagedFiles files(out_dir);
    files.add(from_utf8(tex_ref), as_chars(encode_indexed_bmp(export_texture(sys.settings().texture))));
    files.add(from_utf8(safe + ".qc"), make_qc(sys, baked, safe, tex_ref));
    files.add(from_utf8(safe + "_ref.smd"), make_ref_smd(sys, tex_ref));
    files.add(from_utf8(safe + "_anim.smd"), make_anim_smd(sys, table));
    files.commit();
}

} // namespace gs
