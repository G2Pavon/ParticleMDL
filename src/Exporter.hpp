#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Baking.hpp"
#include "MdlReader.hpp"
#include "ParticleSystem.hpp"

namespace gs {

// studiomdl limits (format/mdl.hpp of the compiler).
// studiomdl does not refuse a longer sequence: it keeps the first 2048 frames and says nothing, which
// cuts the effect short and breaks its loop (seen with Sven Co-op's studiomdl, Nov 30 2018).
inline constexpr int kMaxStudioFrames = 2048;
inline constexpr int kMaxStudioVertices = 2048;   // MAXSTUDIOVERTS: "too many vertices in model" above this
inline constexpr int kMaxTextureNameLength = 63;  // MAXTEXTURENAMELENGTH minus the terminator
// The texture is "<model name>.bmp", so the name leaves room for the extension.
inline constexpr int kMaxModelNameLength = kMaxTextureNameLength - 4;

// SMD bone angles that make a particle bone end up with rotation `r` in game
// (r as applied by ParticleSystem::apply_static_rotation, R = Rx * Ry * Rz),
// including the correction for studiomdl's +90 deg root rotation.
Vec3 studio_angles(Vec3 r);

// model_name with spaces -> '_' and a trailing ".mdl" (any case) removed.
std::string safe_model_name(const std::string& model_name);

// Why a model name cannot be exported (empty, path separators, quotes,
// control characters, characters Windows forbids, device names such as "nul",
// non-ASCII, too long), or nullopt when it is fine.
std::optional<std::string> model_name_problem(std::string_view name);

// The six values written for every bone in every exported frame - position
// x, y, z and rotation x, y, z, after the -90 deg correction. Bone 0 is the root.
struct AnimationTable {
    int frames = 0;
    int bones = 0;
    std::vector<std::array<double, 6>> values;   // frame-major

    const std::array<double, 6>& at(int frame, int bone) const noexcept
    {
        return values[static_cast<std::size_t>(frame) * bones + bone];
    }
};
AnimationTable animation_table(const ParticleSystem& sys);                          // bakes first
AnimationTable animation_table(const ParticleSystem& sys, const BakedPoses& baked);

// studiomdl refuses a sequence when, after writing any bone, the distance from
// that bone's offset entry to the end of the compressed values exceeds this.
inline constexpr std::size_t kMaxSequenceBytes = 65535;

// Number of 2-byte entries studiomdl's run-length coder emits for one channel
// of quantized values (at least one value), or 0 when the channel is dropped
// because every value is zero. Exposed for inspection and tests.
std::size_t encoded_entries(std::span<const short> value);

// The float studiomdl reads back from a value written to the SMD as "%.6f". Exact: the
// same result as printing and reading the text, without the text when the value is not
// close to a rounding tie.
float as_read_by_studiomdl(double v);

// That distance, largest over all bones: studiomdl's own measure, computed with
// its quantization and run-length coding of the values as written to the SMD.
std::size_t sequence_bytes(const AnimationTable& table);
std::size_t sequence_bytes(const ParticleSystem& sys);

// A limit that studiomdl enforces. An export that hits one cannot be compiled,
// so it is refused instead of written.
struct ExportBlocker {
    enum class Kind { TooManyFrames, SequenceTooLarge, TooManyVertices };
    Kind kind;
    std::string message;   // what is over, by how much, and what to lower
};

// What the editor and the CLI need to know about an export, from one pass.
struct ExportReport {
    int frames = 0;
    std::size_t sequence_bytes = 0;        // as studiomdl will measure it
    int vertices = 0;                      // as studiomdl will count them
    std::vector<ExportBlocker> blockers;   // fatal, frame limit first
    std::vector<std::string> warnings;     // advisory
};
ExportReport analyze_export(const ParticleSystem& sys);
ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table);   // reuses a table
ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table, const BakedPoses& baked);

// Thrown by export_model() when the export has blockers. Nothing is written.
class ExportBlocked : public std::runtime_error {
public:
    explicit ExportBlocked(std::vector<ExportBlocker> blockers);
    const std::vector<ExportBlocker>& blockers() const noexcept { return blockers_; }

private:
    std::vector<ExportBlocker> blockers_;
};

// What differs between a model studiomdl compiled and the export it was made from: bones,
// frames, frame rate, loop flag, vertices, texture and the size of the animation. Empty when they agree.
// A difference means the compiler read the files differently than expected; the one about
// the animation size also says the 64 KB check may be off for this compiler.
std::vector<std::string> compare_with_export(const MdlInfo& mdl, const ParticleSystem& sys);

// Writes <safe>.bmp, <safe>.qc, <safe>_ref.smd and <safe>_anim.smd into
// out_dir. Geometry and animation are pre-rotated by -90 deg around Z to cancel
// the +90 deg studiomdl applies on import. Throws std::invalid_argument for an
// unusable model name, ExportBlocked for an export over studiomdl's limits and
// std::runtime_error on I/O errors. Files are staged
// and renamed into place together: a failed export leaves no partial files.
void export_model(const ParticleSystem& sys, const std::filesystem::path& out_dir = ".");
// The same for a caller that has the baked frames and the animation of `sys` already (the
// editor builds them for every change of a setting): they must be the ones of this `sys`.
void export_model(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table,
                  const std::filesystem::path& out_dir);

} // namespace gs
