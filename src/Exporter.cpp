#include "Exporter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <charconv>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <stdexcept>
#include <vector>

#include "Baking.hpp"
#include "TextureGenerator.hpp"

namespace gs {

namespace {

template <typename... Args>
void emit(std::string& out, std::format_string<Args...> fmt, Args&&... args)
{
    std::format_to(std::back_inserter(out), fmt, std::forward<Args>(args)...);
}

void write_nodes(std::string& out, const ParticleSystem& sys)
{
    const BoneLayout& bones = sys.bones();
    out += "version 1\nnodes\n";
    for (int bone = 0; bone < bones.count(); ++bone)
        emit(out, "  {} \"{}\" {}\n", bone, bones.name(bone), bones.parent(bone));
    out += "end\n";
}

// studiomdl turns the animation of every root bone by its Z rotation, 90 deg unless $origin's
// fourth value says otherwise (the angle given plus 90), and never the mesh. -90 makes it 0: the
// model is written as the preview has it, and no root bone carries a constant turn.
const char kNoTurn[] = "$origin 0 0 0 -90\n\n";

// A body part's name in the QC and the suffix of its reference SMD: the first is "studio" and
// "_ref", as a model of one body part has always been, the next "studio2" and "_ref2" and so on.
std::string body_name(std::size_t part)
{
    return part == 0 ? "studio" : "studio" + std::to_string(part + 1);
}

std::string ref_suffix(std::size_t part)
{
    return part == 0 ? "_ref" : "_ref" + std::to_string(part + 1);
}

void write_bodies(std::string& out, const std::string& stem, std::size_t parts)
{
    for (std::size_t k = 0; k < parts; ++k)
        emit(out, "$body \"{}\" \"{}{}\"\n", body_name(k), stem, ref_suffix(k));
    out += '\n';
}

// The particles of each body part, from body_part_starts(), as [first, end) ranges.
std::vector<std::pair<int, int>> part_ranges(const std::vector<int>& starts, int particles)
{
    std::vector<std::pair<int, int>> out;
    for (std::size_t k = 0; k < starts.size(); ++k)
        out.emplace_back(starts[k], k + 1 < starts.size() ? starts[k + 1] : particles);
    return out;
}

// Deals particles to body parts (see body_part_starts) from their vertex and normal counts.
std::vector<int> deal_body_parts(const std::vector<int>& vertices, const std::vector<int>& normals)
{
    std::vector<int> starts;
    int in_part_v = 0, in_part_n = 0;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (starts.empty() || in_part_v + vertices[i] > kMaxStudioVertices || in_part_n + normals[i] > kMaxStudioNormals) {
            starts.push_back(static_cast<int>(i));
            in_part_v = in_part_n = 0;
        }
        in_part_v += vertices[i];
        in_part_n += normals[i];
    }
    return starts;
}

// A number for the .qc: as few digits as it needs, with at least one decimal ("1.0", "0.25").
std::string qc_number(double v)
{
    std::string text = std::format("{:.6f}", v);
    while (text.back() == '0' && text[text.size() - 2] != '.')
        text.pop_back();
    return text;
}

std::string make_qc(const ParticleSystem& sys, const BakedPoses& baked, const std::string& safe,
                    const std::string& tex_ref, std::size_t parts)
{
    std::string out;
    emit(out, "$modelname \"{}.mdl\"\n$cd \".\"\n$cdtexture \".\"\n$scale {}\n", safe, qc_number(sys.settings().model_scale));
    out += kNoTurn;

    // The exact extent of the animation with a unit of margin. studiomdl
    // computes the sequence box (used for culling) itself, scaled; this is the header box, which
    // studiomdl takes as it is written (Sven Co-op's: unscaled by $scale), so it is scaled here.
    ParticleSystem::Bounds b = animation_bounds(sys, baked);
    b.min = b.min * sys.settings().model_scale;
    b.max = b.max * sys.settings().model_scale;
    emit(out, "$bbox {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}\n\n", std::floor(b.min.x) - 1, std::floor(b.min.y) - 1,
         std::floor(b.min.z) - 1, std::ceil(b.max.x) + 1, std::ceil(b.max.y) + 1, std::ceil(b.max.z) + 1);

    // The game adds the texture's colour to what is behind it.
    emit(out, "$texrendermode \"{}\" additive\n\n", tex_ref);
    write_bodies(out, safe, parts);
    emit(out, "$sequence \"idle\" \"{}_anim\"{} fps {}\n", safe, sys.looping() ? " loop" : "", static_cast<int>(sys.export_fps()));
    return out;
}

// The reference mesh of particles [first, end): one body part. Every part names every bone.
std::string make_ref_smd(const ParticleSystem& sys, const std::string& tex_ref, int first, int end)
{
    // Up to 4 triangles per quad, each a material line and three ~80-byte vertex lines.
    std::size_t quads = 0;
    for (int i = first; i < end; ++i)
        quads += sys.quads_of(i).size();
    std::string out;
    out.reserve(quads * 4 * (tex_ref.size() + 1 + 3 * 80) + 4096);
    write_nodes(out, sys);
    out += "skeleton\n  time 0\n";
    for (int bone = 0; bone < sys.bones().count(); ++bone)
        emit(out, "    {} 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n", bone);
    out += "end\ntriangles\n";

    // Static quads have their size in the mesh; with vertex bones the mesh is at scale 1 and the bones scale it.
    double size = 1.0;
    auto write_vertex = [&](int bone, const QuadVertex& v) {
        emit(out, "  {} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {} {}\n", bone, v.pos.x * size, v.pos.y * size,
             v.pos.z * size, v.normal.x, v.normal.y, v.normal.z, v.u, v.v);
    };
    auto write_tri = [&](int b0, const QuadVertex& v0, int b1, const QuadVertex& v1, int b2, const QuadVertex& v2) {
        out += tex_ref;
        out += '\n';
        write_vertex(b0, v0);
        write_vertex(b1, v1);
        write_vertex(b2, v2);
    };

    const bool vertex_bones = sys.bones().vertex_bones();
    const bool back_faces = sys.settings().double_sided && sys.settings().geometry != Geometry::VolumetricBox;
    for (int pi = first; pi < end; ++pi) {
        const Particle& p = sys.particles()[static_cast<std::size_t>(pi)];
        const std::vector<Quad>& quads_of_p = sys.quads_of(pi);
        size = vertex_bones ? 1.0 : p.size;
        for (int qi = 0; qi < static_cast<int>(quads_of_p.size()); ++qi) {
            const Quad& q = quads_of_p[static_cast<std::size_t>(qi)];
            // Each corner hangs on a bone of its own with vertex bones (quads that meet share it),
            // else on the particle's.
            const int b0 = sys.vertex_bone(pi, qi, 0), b1 = sys.vertex_bone(pi, qi, 1), b2 = sys.vertex_bone(pi, qi, 2),
                      b3 = sys.vertex_bone(pi, qi, 3);

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

// --- A still of one frame ---------------------------------------------------

// A face of the still: its corners where the preview draws them on that frame, in model space.
struct StillVertex {
    Vec3 pos, normal;
    int u, v;
};
using StillQuad = std::array<StillVertex, 4>;

std::vector<StillQuad> still_quads(const ParticleSystem& sys, const BakedPoses& baked, int frame)
{
    std::vector<StillQuad> out;
    const double time = sys.frame_time(frame);
    for (int i = 0; i < sys.num_particles(); ++i) {
        const SampledPose pose = baked.sample(i, time);
        if (!pose.visible || !ParticleSystem::has_area(pose.scale, pose.fold))   // hidden, collapsed or folded flat
            continue;
        for (const Quad& q : sys.quads_of(i)) {
            StillQuad still{};
            for (int k = 0; k < 4; ++k)
                still[static_cast<std::size_t>(k)] = {pose.pos + rotate(pose.rot, (q[k].pos + sys.fold_offset(k, pose.fold)) * pose.scale),
                                                      rotate(pose.rot, q[k].normal), q[k].u, q[k].v};
            out.push_back(still);
        }
    }
    return out;
}

const char kStillNodes[] = "version 1\nnodes\n  0 \"particles\" -1\nend\nskeleton\n  time 0\n"
                           "    0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\nend\n";

// A still's faces per body part: 4 vertices and at most 4 normals each, so 512 fill a model.
constexpr std::size_t kStillQuadsPerPart = kMaxStudioVertices / 4;

std::string make_still_qc(const ParticleSystem& sys, const std::vector<StillQuad>& quads, const std::string& stem,
                          const std::string& tex_ref)
{
    constexpr double inf = std::numeric_limits<double>::infinity();
    Vec3 lo{inf, inf, inf}, hi{-inf, -inf, -inf};
    for (const StillQuad& q : quads)
        for (const StillVertex& v : q) {
            lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
            hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
        }
    const double scale = sys.settings().model_scale;
    lo = lo * scale;
    hi = hi * scale;

    std::string out;
    emit(out, "$modelname \"{}.mdl\"\n$cd \".\"\n$cdtexture \".\"\n$scale {}\n", stem, qc_number(scale));
    out += kNoTurn;
    emit(out, "$bbox {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}\n\n", std::floor(lo.x) - 1, std::floor(lo.y) - 1,
         std::floor(lo.z) - 1, std::ceil(hi.x) + 1, std::ceil(hi.y) + 1, std::ceil(hi.z) + 1);
    emit(out, "$texrendermode \"{}\" additive\n\n", tex_ref);
    write_bodies(out, stem, (quads.size() + kStillQuadsPerPart - 1) / kStillQuadsPerPart);
    emit(out, "$sequence \"idle\" \"{}_anim\" fps 1\n", stem);
    return out;
}

std::string make_still_ref_smd(const ParticleSystem& sys, std::span<const StillQuad> quads, const std::string& tex_ref)
{
    std::string out;
    out.reserve(quads.size() * 4 * (tex_ref.size() + 1 + 3 * 80) + 256);
    out += kStillNodes;
    out += "triangles\n";
    auto write_tri = [&](const StillVertex& a, const StillVertex& b, const StillVertex& c) {
        out += tex_ref;
        out += '\n';
        for (const StillVertex* v : {&a, &b, &c})
            emit(out, "  0 {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {} {}\n", v->pos.x, v->pos.y, v->pos.z, v->normal.x,
                 v->normal.y, v->normal.z, v->u, v->v);
    };
    const bool back_faces = sys.settings().double_sided && sys.settings().geometry != Geometry::VolumetricBox;
    for (const StillQuad& q : quads) {
        write_tri(q[0], q[1], q[2]);
        write_tri(q[0], q[2], q[3]);
        if (back_faces) {
            write_tri(q[2], q[1], q[0]);
            write_tri(q[3], q[2], q[0]);
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
    const BoneLayout& bones = sys.bones();
    t.bones = bones.count();
    t.values.assign(static_cast<std::size_t>(t.frames) * t.bones, {});
    for (int frame = 0; frame < t.frames; ++frame) {
        for (int i = 0; i < sys.num_particles(); ++i) {
            // Every bone in every frame: studiomdl puts a missing bone at the origin.
            const ParticlePose& pose = baked.at(frame, i);
            if (!bones.particle_bones()) {
                // Each corner bone carries the whole motion: it moves its corner from where the mesh
                // has it to where the particle puts it, without turning.
                for (int corner = 0; corner < bones.corners_per_particle(); ++corner) {
                    const Vec3 rest = sys.corners()[static_cast<std::size_t>(corner)];
                    const Vec3 at = pose.pos + ParticleSystem::apply_static_rotation(rest * pose.scale, pose.rot.x,
                                                                                     pose.rot.y, pose.rot.z);
                    const Vec3 move = at - rest;
                    t.values[static_cast<std::size_t>(frame) * t.bones + bones.corner_bone(i, corner)] = {move.x, move.y, move.z,
                                                                                                         0.0, 0.0, 0.0};
                }
                continue;
            }
            const Vec3 rot = sys.has_micro_rotation() ? studio_angles(pose.rot) : Vec3{};
            Vec3 at = pose.pos;
            if (bones.fold_bones()) {
                // The particle's bone carries the bottom corners and the fold bone, hung on it, the top
                // ones: the bone moves the bottom corners their share of the fold, and the fold bone
                // the top corners the rest of it, in the particle's own axes.
                const Vec3 bottom = sys.fold_offset(0, pose.fold) * pose.scale;
                at = at + ParticleSystem::apply_static_rotation(bottom, pose.rot.x, pose.rot.y, pose.rot.z);
                const Vec3 fold = (sys.fold_offset(2, pose.fold) * pose.scale) - bottom;
                t.values[static_cast<std::size_t>(frame) * t.bones + bones.fold_bone(i)] = {fold.x, fold.y, fold.z, 0.0, 0.0, 0.0};
            }
            t.values[static_cast<std::size_t>(frame) * t.bones + bones.particle_bone(i)] = {at.x, at.y, at.z, rot.x, rot.y, rot.z};
            // Corner bones shift each corner so the quad appears scaled.
            for (int corner = 0; corner < bones.corners_per_particle(); ++corner) {
                const Vec3 offset = sys.corners()[static_cast<std::size_t>(corner)] * (pose.scale - 1.0);
                const int bone = bones.corner_bone(i, corner);
                t.values[static_cast<std::size_t>(frame) * t.bones + bone] = {offset.x, offset.y, offset.z, 0.0, 0.0, 0.0};
            }
        }
    }
    return t;
}

namespace {

struct SequenceSize {
    std::size_t measure = 0;   // sequence_bytes()
    std::size_t values = 0;    // sequence_value_bytes()
};

SequenceSize sequence_size(const AnimationTable& table)
{
    constexpr double kPiD = 3.14159265358979323846;   // studiomdl's Q_PI
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
                float x = as_read_by_studiomdl(row[k]);   // the QC turns no root bone ($origin)
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
    return {worst, data_bytes};
}

} // namespace

std::size_t sequence_bytes(const AnimationTable& table)
{
    return sequence_size(table).measure;
}

std::size_t sequence_value_bytes(const AnimationTable& table)
{
    return sequence_size(table).values;
}

std::size_t sequence_bytes(const ParticleSystem& sys)
{
    return sequence_bytes(animation_table(sys));
}

// The particle's rotation is R = Rx * Ry * Rz (apply_static_rotation) and the engine reads SMD
// angles (x, y, z) as Rz(z) * Ry(y) * Rx(x): the same angles would turn the other way round, so
// this goes through the matrix. (No +90 deg correction: the QC turns no root bone.)
Vec3 studio_angles(Vec3 r)
{
    auto R = [&](Vec3 v) { return ParticleSystem::apply_static_rotation(v, r.x, r.y, r.z); };
    // Columns of M = R.
    const Vec3 c0 = R({1, 0, 0});
    const Vec3 c1 = R({0, 1, 0});
    const Vec3 c2 = R({0, 0, 1});
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
    const BakedPoses baked = bake(sys);
    return analyze_export(sys, animation_table(sys, baked), baked);
}

namespace {

// The baked frames and the table are built from a system, and given back with one: a cheap check that
// they have its shape, so frames of an older system are not exported (or measured) with a new one.
void require_built_from(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table)
{
    if (baked.particles() != sys.num_particles() || baked.steps() != sys.loop_steps() ||
        baked.looping() != sys.looping() || table.frames != sys.export_frames() || table.bones != sys.bones().count() ||
        table.values.size() != static_cast<std::size_t>(table.frames) * static_cast<std::size_t>(table.bones))
        throw std::invalid_argument("the baked frames or the animation table were not built from this particle system");
}

} // namespace

ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table, const BakedPoses& baked)
{
    require_built_from(sys, baked, table);
    const Settings& s = sys.settings();
    ExportReport r;
    r.frames = table.frames;
    r.sequence_bytes = sequence_bytes(table);
    std::vector<int> vertices(static_cast<std::size_t>(sys.num_particles())), normals(vertices.size());
    for (int i = 0; i < sys.num_particles(); ++i) {
        vertices[static_cast<std::size_t>(i)] = sys.vertices_of(i);
        normals[static_cast<std::size_t>(i)] = sys.normals_of(i);
        r.vertices += vertices[static_cast<std::size_t>(i)];
        r.normals += normals[static_cast<std::size_t>(i)];
    }
    r.body_parts = static_cast<int>(deal_body_parts(vertices, normals).size());

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
        if (sys.bones().vertex_bones())
            message += std::format(" Vertex-bone scaling uses {} bones per particle: turning it off or using fewer "
                                   "quads saves the most.",
                                   sys.bones().per_particle());
        r.blockers.push_back({ExportBlocker::Kind::SequenceTooLarge, std::move(message)});
    }
    if (r.body_parts > kMaxBodyParts) {
        // Which runs out: the vertices alone already need too many body parts, or the normals do.
        // studiomdl keeps a normal for each direction on each bone: with vertex bones a corner shared
        // by several faces has one for each of them.
        const bool by_vertices =
            deal_body_parts(vertices, std::vector<int>(vertices.size(), 0)).size() > static_cast<std::size_t>(kMaxBodyParts);
        std::string message =
            std::format("{} {}: studiomdl takes at most {} in a body part, and a model of {} body parts holds {}. "
                        "Lower Particle Count",
                        by_vertices ? r.vertices : r.normals, by_vertices ? "vertices" : "normals",
                        by_vertices ? kMaxStudioVertices : kMaxStudioNormals, kMaxBodyParts,
                        kMaxBodyParts * (by_vertices ? kMaxStudioVertices : kMaxStudioNormals));
        message += s.cluster_size > 1 ? ", Cluster Size or use geometry with fewer quads." : " or use geometry with fewer quads.";
        r.blockers.push_back({by_vertices ? ExportBlocker::Kind::TooManyVertices : ExportBlocker::Kind::TooManyNormals,
                              std::move(message)});
    }

    // A particle drawn in none of the frames: with vertex bones the frames around a birth and a death
    // are collapsed, so a life shorter than about two frames disappears.
    int never_drawn = 0;
    for (int i = 0; i < baked.particles(); ++i) {
        bool drawn = false;
        for (int f = 0; f < baked.stored_frames() && !drawn; ++f)
            drawn = baked.at(f, i).visible && ParticleSystem::has_area(baked.at(f, i).scale, baked.at(f, i).fold);
        never_drawn += drawn ? 0 : 1;
    }
    if (never_drawn > 0) {
        std::string message = std::format("{} of {} particles are never drawn", never_drawn, baked.particles());
        message += s.scale_mode == ScaleMode::VertexBone ? ": their lives are too short for the frame rate (Vertex Bone "
                                                           "scaling collapses a quad on the frames around its birth and its "
                                                           "death). Raise Lifespan or FPS."
                   : s.scale_mode == ScaleMode::Folding ? ": their lives are too short for the frame rate (Folding folds a "
                                                          "quad flat on the frames around its birth and its death). Raise "
                                                          "Lifespan or FPS."
                                                        : ". Raise Lifespan or FPS.";
        r.warnings.push_back(std::move(message));
    }

    if (s.end_behavior == EndBehavior::HideInCore && s.scale_mode == ScaleMode::Static)
        r.warnings.push_back("Hide in Core needs Vertex Bone scaling: static particles stay visible at the center");
    else if (s.scale_mode == ScaleMode::Static && baked.unhidden_jump() > std::max(s.quad_width, s.quad_height) &&
             (s.end_behavior == EndBehavior::EndlessLoop || s.end_behavior == EndBehavior::StopAndStay))
        r.warnings.push_back("Static quads cannot hide a respawn: each particle visibly jumps back to its start for one "
                             "frame. Vertex Bone scaling removes the jump.");
    if (baked.slanted_rise() > std::max(s.quad_width, s.quad_height))
        r.warnings.push_back("Some particles are hidden for only one frame between lives, so the quad comes up at a slant "
                             "and slides along the floor as it reappears. Lower Lifespan or raise FPS to give it a "
                             "second hidden frame.");
    return r;
}

ExportBlocked::ExportBlocked(std::vector<ExportBlocker> blockers)
    : std::runtime_error(blockers.empty() ? "export blocked" : blockers.front().message), blockers_(std::move(blockers))
{
}

std::vector<std::string> compare_with_export(const MdlInfo& mdl, const ParticleSystem& sys)
{
    std::vector<std::string> problems;
    if (static_cast<int>(mdl.bones.size()) != sys.bones().count())
        problems.push_back(std::format("the model has {} bones, the export has {}", mdl.bones.size(), sys.bones().count()));

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
            const std::size_t measured = seq.value_bytes;
            const std::size_t predicted = sequence_value_bytes(animation_table(sys));
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
    int normals = 0;
    for (const MdlModel& model : mdl.models)
        normals += model.normals;
    if (normals != sys.normal_count())
        problems.push_back(std::format("the model has {} normals, the export has {}", normals, sys.normal_count()));

    if (mdl.textures.size() != 1) {
        problems.push_back(std::format("the model has {} textures, the export has 1", mdl.textures.size()));
    } else {
        const MdlTexture& tex = mdl.textures.front();
        if ((tex.flags & kMdlTextureAdditive) == 0)
            problems.push_back(std::format("the texture is not additive (render flags {:#x})", tex.flags));
        const int size = sys.settings().texture.size;
        if (tex.width != size || tex.height != size)
            problems.push_back(std::format("the texture is {} x {}, the export has {} x {}", tex.width, tex.height, size, size));
    }
    return problems;
}

ExportFiles export_files(const ParticleSystem& sys)
{
    const BakedPoses baked = bake(sys);
    return export_files(sys, baked, animation_table(sys, baked));
}

ExportFiles export_files(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table)
{
    if (const auto problem = model_name_problem(sys.settings().model_name))
        throw std::invalid_argument(*problem);
    if (ExportReport report = analyze_export(sys, table, baked); !report.blockers.empty())
        throw ExportBlocked(std::move(report.blockers));
    return export_files_over_limits(sys, baked, table);
}

ExportFiles export_files_over_limits(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table)
{
    const std::string& model_name = sys.settings().model_name;
    if (const auto problem = model_name_problem(model_name))
        throw std::invalid_argument(*problem);
    require_built_from(sys, baked, table);
    // Every file, the texture included, uses the sanitized name, so the QC, the SMDs and the
    // .mdl all agree.
    const std::string safe = safe_model_name(model_name);
    const std::string tex_ref = safe + ".bmp";

    const std::vector<std::uint8_t> bmp = encode_indexed_bmp(export_texture(sys.settings().texture));
    ExportFiles out{safe, {}};
    out.files.push_back({tex_ref, std::string(bmp.begin(), bmp.end())});
    const auto parts = part_ranges(body_part_starts(sys), sys.num_particles());
    out.files.push_back({safe + ".qc", make_qc(sys, baked, safe, tex_ref, parts.size())});
    for (std::size_t k = 0; k < parts.size(); ++k)
        out.files.push_back({safe + ref_suffix(k) + ".smd", make_ref_smd(sys, tex_ref, parts[k].first, parts[k].second)});
    out.files.push_back({safe + "_anim.smd", make_anim_smd(sys, table)});
    return out;
}

const std::string& ExportFiles::bytes_of(std::string_view suffix) const
{
    for (const ExportFile& file : files)
        if (file.name.size() == safe_name.size() + suffix.size() && file.name.starts_with(safe_name) &&
            file.name.ends_with(suffix))
            return file.bytes;
    throw std::out_of_range("the export has no " + safe_name + std::string(suffix));
}

std::string frame_model_name(const std::string& model_name, int frame)
{
    const std::string suffix = "_f" + std::to_string(frame);
    std::string safe = safe_model_name(model_name);
    const auto room = static_cast<std::size_t>(kMaxModelNameLength) - suffix.size();
    if (safe.size() > room)
        safe.resize(room);
    return safe + suffix;
}

int particles_on_frame(const ParticleSystem& sys, const BakedPoses& baked, int frame)
{
    int shown = 0;
    for (int i = 0; i < sys.num_particles(); ++i) {
        const SampledPose pose = baked.sample(i, sys.frame_time(frame));
        if (pose.visible && ParticleSystem::has_area(pose.scale, pose.fold))
            ++shown;
    }
    return shown;
}

ExportFiles export_frame_files(const ParticleSystem& sys, const BakedPoses& baked, int frame)
{
    if (const auto problem = model_name_problem(sys.settings().model_name))
        throw std::invalid_argument(*problem);
    const int last = sys.looping() ? sys.loop_steps() - 1 : sys.loop_steps();
    if (frame < 0 || frame > last)
        throw std::invalid_argument("there is no frame " + std::to_string(frame) + " (the frames are 0 to " +
                                    std::to_string(last) + ")");
    if (baked.particles() != sys.num_particles() || baked.steps() != sys.loop_steps() || baked.looping() != sys.looping())
        throw std::invalid_argument("the baked frames were not built from this particle system");
    const std::vector<StillQuad> quads = still_quads(sys, baked, frame);
    if (quads.empty())
        throw std::invalid_argument("no particle is shown on frame " + std::to_string(frame));
    const std::size_t parts = (quads.size() + kStillQuadsPerPart - 1) / kStillQuadsPerPart;
    if (parts > static_cast<std::size_t>(kMaxBodyParts))
        throw std::invalid_argument(std::format("frame {} shows {} faces: a still holds at most {} ({} body parts of {})",
                                                frame, quads.size(), kMaxBodyParts * kStillQuadsPerPart, kMaxBodyParts,
                                                kStillQuadsPerPart));

    const std::string stem = frame_model_name(sys.settings().model_name, frame);
    const std::string tex_ref = stem + ".bmp";
    const std::vector<std::uint8_t> bmp = encode_indexed_bmp(export_texture(sys.settings().texture));
    ExportFiles out{stem, {}};
    out.files.push_back({tex_ref, std::string(bmp.begin(), bmp.end())});
    out.files.push_back({stem + ".qc", make_still_qc(sys, quads, stem, tex_ref)});
    for (std::size_t k = 0; k < parts; ++k) {
        const std::size_t first = k * kStillQuadsPerPart;
        const std::span<const StillQuad> part(quads.data() + first, std::min(kStillQuadsPerPart, quads.size() - first));
        out.files.push_back({stem + ref_suffix(k) + ".smd", make_still_ref_smd(sys, part, tex_ref)});
    }
    out.files.push_back({stem + "_anim.smd", std::string(kStillNodes)});
    return out;
}

std::vector<int> body_part_starts(const ParticleSystem& sys)
{
    std::vector<int> vertices, normals;
    for (int i = 0; i < sys.num_particles(); ++i) {
        vertices.push_back(sys.vertices_of(i));
        normals.push_back(sys.normals_of(i));
    }
    return deal_body_parts(vertices, normals);
}

} // namespace gs
