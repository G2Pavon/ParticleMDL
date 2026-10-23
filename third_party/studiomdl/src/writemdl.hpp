#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "format/qc.hpp"
#include "studiomdl.hpp"

// File name stored in the header and used for the output file: <$modelname stem>.mdl
std::string mdl_file_name(const QC &qc);

// Serializes a finished compilation into a complete .mdl image.
std::vector<std::uint8_t> write_mdl(CompileState &state);
