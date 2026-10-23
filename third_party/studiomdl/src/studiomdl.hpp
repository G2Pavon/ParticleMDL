#pragma once

#include <array>
#include <vector>

#include "format/mdl.hpp"
#include "format/qc.hpp"
#include "modeldata.hpp"
#include "studiomdl/studiomdl.hpp"
#include "utils/log.hpp"

// Everything one compilation produces that the .mdl writer consumes.
// Each compile owns its own instance; nothing here is shared between compiles.
struct CompileState
{
    explicit CompileState(const studiomdl::CompileOptions &options) : logger(options.log) {}

    Logger logger;
    QC qc;

    std::array<std::array<int, 100>, 100> xnode{};
    int numxnodes = 0;
    std::vector<BoneTable> bonetable;
    std::vector<Texture> textures;
    std::array<std::array<int, MAXSTUDIOSKINS>, 256> skinref{}; // [skin][skinref], returns texture index
    int skinrefcount = 0;
    int skinfamiliescount = 0;
};
