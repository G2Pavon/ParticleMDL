#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs {

// What can be read back from a compiled GoldSrc model (studio format, version 10): enough
// to check what studiomdl made of an export. Not a full model reader.

inline constexpr int kMdlTextureAdditive = 0x0020;   // the render flag ParticleMDL asks for
inline constexpr int kMdlSequenceLooping = 0x0001;

struct MdlBone {
    std::string name;
    int parent = -1;
};

struct MdlTexture {
    std::string name;
    int flags = 0;
    int width = 0, height = 0;
};

struct MdlSequence {
    std::string label;
    float fps = 0;
    int flags = 0;
    int frames = 0;
    int blends = 0;
    std::array<float, 3> bbmin{}, bbmax{};   // the box studiomdl computed around the sequence, in model space
    bool external = false;            // its animation is in another file ("name01.mdl"); not read
    std::size_t offset_bytes = 0;     // the table of channel offsets: 12 bytes for each bone
    std::size_t value_bytes = 0;      // the run-length coded values after it
};

struct MdlModel {                     // one model of a body part: what studiomdl counted for it
    std::string name;
    int vertices = 0;
    int normals = 0;
};

struct MdlInfo {
    int version = 0;
    std::string name;
    std::uint32_t length = 0;         // file size as the header states it
    std::vector<MdlBone> bones;
    std::vector<MdlSequence> sequences;
    std::vector<MdlTexture> textures;
    int body_parts = 0;
    std::vector<MdlModel> models;     // of every body part, in order
    int skin_families = 0;
};

// Thrown for a file that is not a studio model or is cut off or inconsistent. The message
// says what was wrong and where.
class MdlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Every offset and count is checked against the size of `bytes`, so a damaged or hostile
// file ends in MdlError, never in a read outside the buffer or a loop that does not end.
MdlInfo read_mdl(std::span<const std::uint8_t> bytes);
MdlInfo read_mdl_file(const std::filesystem::path& path);   // also throws std::runtime_error if unreadable

// "idle: 31 frames at 15 fps, 21 bones, 1 texture (additive), 3.8 KB of animation".
std::string describe_mdl(const MdlInfo& info);

} // namespace gs
