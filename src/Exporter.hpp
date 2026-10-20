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

#include "ParticleSystem.hpp"

namespace gs {

// studiomdl limits (format/mdl.hpp of the compiler).
inline constexpr int kMaxStudioFrames = 2048;     // MAXSTUDIOANIMATIONS
inline constexpr int kMaxTextureNameLength = 63;  // MAXTEXTURENAMELENGTH minus the terminator
// The texture is "<model name>.bmp", so the name leaves room for the extension.
inline constexpr int kMaxModelNameLength = kMaxTextureNameLength - 4;

// SMD bone angles that make a particle bone end up with rotation `r` in game
// (r as applied by ParticleSystem::apply_static_rotation, R = Rx * Ry * Rz),
// including the correction for studiomdl's +90 deg root rotation.
Vec3 studio_angles(Vec3 r);

// model_name with spaces -> '_' and every ".mdl" removed.
std::string safe_model_name(const std::string& model_name);

// Why a model name cannot be exported (empty, path separators, quotes,
// control characters, too long), or nullopt when it is fine.
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
AnimationTable animation_table(const ParticleSystem& sys);

// studiomdl refuses a sequence when, after writing any bone, the distance from
// that bone's offset entry to the end of the compressed values exceeds this.
inline constexpr std::size_t kMaxSequenceBytes = 65535;

// Number of 2-byte entries studiomdl's run-length coder emits for one channel
// of quantized values (at least one value), or 0 when the channel is dropped
// because every value is zero. Exposed for inspection and tests.
std::size_t encoded_entries(std::span<const short> value);

// That distance, largest over all bones: studiomdl's own measure, computed with
// its quantization and run-length coding of the values as written to the SMD.
std::size_t sequence_bytes(const AnimationTable& table);
std::size_t sequence_bytes(const ParticleSystem& sys);

// A limit that studiomdl enforces. An export that hits one cannot be compiled,
// so it is refused instead of written.
struct ExportBlocker {
    enum class Kind { TooManyFrames, SequenceTooLarge };
    Kind kind;
    std::string message;   // what is over, by how much, and what to lower
};

// What the editor and the CLI need to know about an export, from one pass.
struct ExportReport {
    int frames = 0;
    std::size_t sequence_bytes = 0;        // as studiomdl will measure it
    std::vector<ExportBlocker> blockers;   // fatal, frame limit first
    std::vector<std::string> warnings;     // advisory
};
ExportReport analyze_export(const ParticleSystem& sys);
ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table);   // reuses a table

// Thrown by export_model() when the export has blockers. Nothing is written.
class ExportBlocked : public std::runtime_error {
public:
    explicit ExportBlocked(std::vector<ExportBlocker> blockers);
    const std::vector<ExportBlocker>& blockers() const noexcept { return blockers_; }

private:
    std::vector<ExportBlocker> blockers_;
};

// Writes <safe>.bmp, <safe>.qc, <safe>_ref.smd and <safe>_anim.smd into
// out_dir. Geometry and animation are pre-rotated by -90 deg around Z to cancel
// the +90 deg studiomdl applies on import. Throws std::invalid_argument for an
// unusable model name, ExportBlocked for an export over studiomdl's limits and
// std::runtime_error on I/O errors. Files are staged
// and renamed into place together: a failed export leaves no partial files.
void export_model(const ParticleSystem& sys, const std::filesystem::path& out_dir = ".");

} // namespace gs
