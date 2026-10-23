#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "Exporter.hpp"

namespace gs {

struct CompiledModel {
    std::vector<std::uint8_t> mdl;       // the whole .mdl file
    std::vector<std::string> warnings;   // what the compiler adjusted or ignored in the export
    std::vector<std::string> notes;      // its progress and sizes, such as "sequences   4176 bytes (31 frames) ..."
};

// The compiler refused the export. what() is its message, with the file and line at fault
// when it names one ("box_anim.smd:12: ERROR: ...").
class CompileFailed : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Compiles an export into a .mdl with the built-in studiomdl (studiomdl++, under
// third_party/studiomdl), all in memory: nothing is read from or written to disk. Throws
// CompileFailed when the compiler refuses the files.
CompiledModel compile_model(const ExportFiles& files);

// Exports `sys`, compiles it and writes <safe name>.mdl into `out_dir`; an older model there is
// replaced only by a complete new one. `baked` and `table` must be those of `sys` (see
// export_files). Throws std::invalid_argument for an unusable model name, ExportBlocked for an
// export over studiomdl's limits, CompileFailed, and std::runtime_error when `out_dir` does not
// exist or the model cannot be written.
CompiledModel export_mdl(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table,
                         const std::filesystem::path& out_dir);
// The same, baking `sys` first.
CompiledModel export_mdl(const ParticleSystem& sys, const std::filesystem::path& out_dir);

// Compiles a still of exported frame `frame` (see export_frame_files) and writes it as
// frame_model_name().mdl into `out_dir`, like export_mdl. Throws std::invalid_argument for an
// unusable model name, a frame the animation does not have or a frame showing no particle,
// CompileFailed, and std::runtime_error when `out_dir` does not exist or the model cannot be written.
CompiledModel export_frame_mdl(const ParticleSystem& sys, const BakedPoses& baked, int frame,
                               const std::filesystem::path& out_dir);

} // namespace gs
