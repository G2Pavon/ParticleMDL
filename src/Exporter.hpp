#pragma once

#include <array>
#include <cstddef>
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
inline constexpr int kMaxStudioNormals = 2048;    // MAXSTUDIOVERTS too: "too many normals in model" above this
inline constexpr int kMaxTextureNameLength = 63;  // MAXTEXTURENAMELENGTH minus the terminator
// The vertex and normal limits are per model, and every body part is a model of its own that the
// engine draws, so a mesh that needs more is dealt over several body parts. 32 is the SDK's
// MAXSTUDIOBODYPARTS; neither studiomdl nor the engine checks it, so it is kept to.
inline constexpr int kMaxBodyParts = 32;
// The texture is "<model name>.bmp", so the name leaves room for the extension.
inline constexpr int kMaxModelNameLength = kMaxTextureNameLength - 4;

// SMD bone angles that make a particle bone end up with rotation `r` in game
// (r as applied by ParticleSystem::apply_static_rotation, R = Rx * Ry * Rz),
// in the engine's order of angles.
Vec3 studio_angles(Vec3 r);

// model_name with spaces -> '_' and a trailing ".mdl" (any case) removed.
std::string safe_model_name(const std::string& model_name);

// Why a model name cannot be exported (empty, path separators, quotes,
// control characters, characters Windows forbids, device names such as "nul",
// non-ASCII, too long), or nullopt when it is fine.
std::optional<std::string> model_name_problem(std::string_view name);

// The six values written for every bone in every exported frame - position
// x, y, z and rotation x, y, z.
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
    enum class Kind { TooManyFrames, SequenceTooLarge, TooManyVertices, TooManyNormals };
    Kind kind;
    std::string message;   // what is over, by how much, and what to lower
};

// What the editor and the CLI need to know about an export, from one pass.
struct ExportReport {
    int frames = 0;
    std::size_t sequence_bytes = 0;        // as studiomdl will measure it
    int vertices = 0;                      // as studiomdl will count them
    int normals = 0;                       // likewise
    int body_parts = 0;                    // the models the mesh is dealt over (body_part_starts)
    std::vector<ExportBlocker> blockers;   // fatal, frame limit first
    std::vector<std::string> warnings;     // advisory
};
ExportReport analyze_export(const ParticleSystem& sys);
// The first particle of each body part: the particles are dealt in order, each part taking as many
// whole particles as fit in kMaxStudioVertices and kMaxStudioNormals. Empty without particles.
std::vector<int> body_part_starts(const ParticleSystem& sys);
// The same for the baked frames and the table of `sys` already at hand; throws std::invalid_argument
// if their shape shows they were built from another system.
ExportReport analyze_export(const ParticleSystem& sys, const AnimationTable& table, const BakedPoses& baked);

// Thrown by export_files() when the export has blockers. Nothing is written.
class ExportBlocked : public std::runtime_error {
public:
    explicit ExportBlocked(std::vector<ExportBlocker> blockers);
    const std::vector<ExportBlocker>& blockers() const noexcept { return blockers_; }

private:
    std::vector<ExportBlocker> blockers_;
};

// What differs between a model studiomdl compiled and the export it was made from: bones,
// frames, frame rate, loop flag, vertices, normals, texture and the size of the animation. Empty when they agree.
// A difference means the compiler read the files differently than expected; the one about
// the animation size also says the 64 KB check may be off for this compiler.
std::vector<std::string> compare_with_export(const MdlInfo& mdl, const ParticleSystem& sys);

// One file of an export.
struct ExportFile {
    std::string name;    // UTF-8, without a folder
    std::string bytes;
};

struct ExportFiles {
    std::string safe_name;           // safe_model_name() of the model name: <safe_name>.qc and .mdl
    std::vector<ExportFile> files;   // <safe>.bmp, <safe>.qc, <safe>_ref.smd and <safe>_anim.smd

    // The contents of <safe_name><suffix> (".qc", "_ref.smd", ...). Throws std::out_of_range when
    // there is no such file.
    const std::string& bytes_of(std::string_view suffix) const;
};

// The files of an export, in memory: the texture, the QC and the two SMDs, for the compiler.
// The QC sets studiomdl's root turn to 0, so geometry and animation are written as the preview
// has them. `baked` and `table` must be those of `sys` (the editor builds them for every change
// of a setting); std::invalid_argument is thrown when their shape shows they are not, and for an
// unusable model name. Throws ExportBlocked for an export over studiomdl's limits.
ExportFiles export_files(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table);
// The same, baking `sys` first.
ExportFiles export_files(const ParticleSystem& sys);
// A still model of one frame: every particle shown on exported frame `frame` of `sys`, where the
// preview shows it on that frame, all on one bone, with a sequence of that one frame.

// Its name: <safe model name>_f<frame>, the safe name shortened when both would be too long.
std::string frame_model_name(const std::string& model_name, int frame);
// The particles shown on exported frame `frame`.
int particles_on_frame(const ParticleSystem& sys, const BakedPoses& baked, int frame);
// Its files, named frame_model_name(). `baked` must be that of `sys`. Throws std::invalid_argument
// for an unusable model name, a frame the animation does not have, or a frame on which no
// particle is shown.
ExportFiles export_frame_files(const ParticleSystem& sys, const BakedPoses& baked, int frame);

// The files without the check of studiomdl's limits, for testing that the compiler refuses
// what is over them.
ExportFiles export_files_over_limits(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table);

} // namespace gs
