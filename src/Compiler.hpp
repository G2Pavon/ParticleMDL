#pragma once

#include <cstdint>
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

} // namespace gs
