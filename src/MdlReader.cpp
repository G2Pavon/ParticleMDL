#include "MdlReader.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>

#include "PathUtil.hpp"

namespace gs {

namespace {

// Layout of the studio format, version 10 (sizes in bytes).
constexpr std::size_t kHeaderSize = 244;
constexpr std::size_t kBoneSize = 112;
constexpr std::size_t kSeqDescSize = 176;
constexpr std::size_t kTextureSize = 80;
constexpr std::size_t kAnimOffsetsSize = 12;   // six 16-bit offsets per bone

// Header field offsets.
constexpr std::size_t kOffId = 0, kOffVersion = 4, kOffName = 8, kOffLength = 72;
constexpr std::size_t kOffNumBones = 140, kOffBoneIndex = 144;
constexpr std::size_t kOffNumSeq = 164, kOffSeqIndex = 168;
constexpr std::size_t kOffNumTextures = 180, kOffTextureIndex = 184;
constexpr std::size_t kOffNumSkinFamilies = 196;
constexpr std::size_t kOffNumBodyParts = 204;
// Sequence description field offsets.
constexpr std::size_t kSeqFps = 32, kSeqFlags = 36, kSeqNumFrames = 56, kSeqNumBlends = 120, kSeqAnimIndex = 124,
                      kSeqGroup = 156;

// Limits that keep a damaged file from making the reader slow or large. Far above anything the
// engine accepts (128 bones, 2048 frames).
constexpr int kMaxBones = 4096;
constexpr int kMaxSequences = 8192;
constexpr int kMaxTextures = 1024;
constexpr int kMaxFrames = 65536;
constexpr int kMaxBlends = 16;
constexpr std::size_t kMaxSteps = 100'000'000;   // run-length segments walked in one file

class Bytes {
public:
    explicit Bytes(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t size() const noexcept { return data_.size(); }

    void require(std::size_t offset, std::size_t length, const char* what) const
    {
        if (offset > data_.size() || length > data_.size() - offset)
            throw MdlError(std::format("the file ends inside the {} (needs bytes {} to {}, the file has {})", what, offset,
                                       offset + length, data_.size()));
    }

    // Little-endian, whatever the machine is.
    std::uint32_t u32(std::size_t offset, const char* what) const
    {
        require(offset, 4, what);
        return static_cast<std::uint32_t>(data_[offset]) | static_cast<std::uint32_t>(data_[offset + 1]) << 8 |
               static_cast<std::uint32_t>(data_[offset + 2]) << 16 | static_cast<std::uint32_t>(data_[offset + 3]) << 24;
    }
    std::int32_t i32(std::size_t offset, const char* what) const { return static_cast<std::int32_t>(u32(offset, what)); }
    std::uint16_t u16(std::size_t offset, const char* what) const
    {
        require(offset, 2, what);
        return static_cast<std::uint16_t>(data_[offset] | data_[offset + 1] << 8);
    }
    std::uint8_t u8(std::size_t offset, const char* what) const
    {
        require(offset, 1, what);
        return data_[offset];
    }
    float f32(std::size_t offset, const char* what) const
    {
        const std::uint32_t bits = u32(offset, what);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }

    // A fixed-size, zero-terminated name.
    std::string name(std::size_t offset, std::size_t capacity, const char* what) const
    {
        require(offset, capacity, what);
        std::size_t length = 0;
        while (length < capacity && data_[offset + length] != 0)
            ++length;
        return std::string(reinterpret_cast<const char*>(data_.data() + offset), length);
    }

private:
    std::span<const std::uint8_t> data_;
};

// A count read from the file: not negative, not past `limit`.
int checked_count(const Bytes& in, std::size_t offset, int limit, const char* what)
{
    const std::int32_t n = in.i32(offset, what);
    if (n < 0 || n > limit)
        throw MdlError(std::format("the number of {} is {}, which is not between 0 and {}", what, n, limit));
    return n;
}

// A position in the file read from it: not negative, so it can be used as an offset.
std::size_t checked_index(const Bytes& in, std::size_t offset, const char* what)
{
    const std::int32_t n = in.i32(offset, what);
    if (n < 0)
        throw MdlError(std::format("the offset of the {} is negative ({})", what, n));
    return static_cast<std::size_t>(n);
}

MdlSequence read_sequence(const Bytes& in, std::size_t at, int bones, std::size_t& steps)
{
    in.require(at, kSeqDescSize, "sequence description");
    MdlSequence seq;
    seq.label = in.name(at, 32, "sequence name");
    seq.fps = in.f32(at + kSeqFps, "sequence fps");
    seq.flags = in.i32(at + kSeqFlags, "sequence flags");
    seq.frames = checked_count(in, at + kSeqNumFrames, kMaxFrames, "frames");
    seq.blends = checked_count(in, at + kSeqNumBlends, kMaxBlends, "blends");
    seq.external = in.i32(at + kSeqGroup, "sequence group") != 0;
    if (seq.external || seq.blends == 0)
        return seq;

    const std::size_t anim = checked_index(in, at + kSeqAnimIndex, "animation");
    const std::size_t tables = static_cast<std::size_t>(bones) * static_cast<std::size_t>(seq.blends);
    seq.offset_bytes = tables * kAnimOffsetsSize;
    in.require(anim, seq.offset_bytes, "animation offsets");

    // Each channel is a chain of run-length segments: a count of values that are stored and a
    // count of frames they cover, then the stored values. The data ends where the last chain ends.
    std::size_t end = anim + seq.offset_bytes;
    for (std::size_t table = 0; table < tables; ++table) {
        const std::size_t table_at = anim + table * kAnimOffsetsSize;
        for (std::size_t channel = 0; channel < 6; ++channel) {
            const std::uint16_t offset = in.u16(table_at + channel * 2, "channel offset");
            if (offset == 0)
                continue;   // the channel is not animated
            std::size_t p = table_at + offset;
            int covered = 0;
            while (covered < seq.frames) {
                if (++steps > kMaxSteps)
                    throw MdlError("the animation data is too large to check");
                const std::uint8_t valid = in.u8(p, "animation values");
                const std::uint8_t total = in.u8(p + 1, "animation values");
                if (total == 0)
                    throw MdlError(std::format("an animation channel has a run of no frames (at byte {})", p));
                const std::size_t values = static_cast<std::size_t>(valid) * 2;
                in.require(p + 2, values, "animation values");
                p += 2 + values;
                covered += total;
            }
            end = std::max(end, p);
        }
    }
    seq.value_bytes = end - (anim + seq.offset_bytes);
    return seq;
}

} // namespace

MdlInfo read_mdl(std::span<const std::uint8_t> bytes)
{
    const Bytes in(bytes);
    if (in.size() < 8 || std::memcmp(bytes.data() + kOffId, "IDST", 4) != 0)
        throw MdlError("not a GoldSrc model (the file does not start with IDST)");
    MdlInfo info;
    info.version = in.i32(kOffVersion, "header");
    if (info.version != 10)
        throw MdlError(std::format("model version {} is not supported (only version 10)", info.version));
    in.require(0, kHeaderSize, "header");
    info.name = in.name(kOffName, 64, "model name");
    info.length = in.u32(kOffLength, "header");
    if (info.length != in.size())
        throw MdlError(std::format("the header says the file is {} bytes, it has {}", info.length, in.size()));

    const int bones = checked_count(in, kOffNumBones, kMaxBones, "bones");
    const std::size_t bone_at = checked_index(in, kOffBoneIndex, "bones");
    in.require(bone_at, static_cast<std::size_t>(bones) * kBoneSize, "bones");
    for (int i = 0; i < bones; ++i) {
        const std::size_t at = bone_at + static_cast<std::size_t>(i) * kBoneSize;
        MdlBone bone;
        bone.name = in.name(at, 32, "bone name");
        bone.parent = in.i32(at + 32, "bone parent");
        if (bone.parent < -1 || bone.parent >= bones)
            throw MdlError(std::format("bone {} has parent {}, which is not a bone", i, bone.parent));
        info.bones.push_back(std::move(bone));
    }

    const int sequences = checked_count(in, kOffNumSeq, kMaxSequences, "sequences");
    const std::size_t seq_at = checked_index(in, kOffSeqIndex, "sequences");
    in.require(seq_at, static_cast<std::size_t>(sequences) * kSeqDescSize, "sequences");
    std::size_t steps = 0;
    for (int i = 0; i < sequences; ++i)
        info.sequences.push_back(read_sequence(in, seq_at + static_cast<std::size_t>(i) * kSeqDescSize, bones, steps));

    const int textures = checked_count(in, kOffNumTextures, kMaxTextures, "textures");
    const std::size_t tex_at = checked_index(in, kOffTextureIndex, "textures");
    in.require(tex_at, static_cast<std::size_t>(textures) * kTextureSize, "textures");
    for (int i = 0; i < textures; ++i) {
        const std::size_t at = tex_at + static_cast<std::size_t>(i) * kTextureSize;
        MdlTexture tex;
        tex.name = in.name(at, 64, "texture name");
        tex.flags = in.i32(at + 64, "texture flags");
        tex.width = in.i32(at + 68, "texture width");
        tex.height = in.i32(at + 72, "texture height");
        info.textures.push_back(std::move(tex));
    }

    info.body_parts = checked_count(in, kOffNumBodyParts, kMaxSequences, "body parts");
    info.skin_families = checked_count(in, kOffNumSkinFamilies, kMaxSequences, "skin families");
    return info;
}

MdlInfo read_mdl_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot read " + to_utf8(path));
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return read_mdl(bytes);
}

std::string describe_mdl(const MdlInfo& info)
{
    std::string text;
    for (const MdlSequence& seq : info.sequences) {
        if (!text.empty())
            text += "; ";
        text += std::format("{}: {} frames at {:g} fps, {:.1f} KB of animation", seq.label, seq.frames, static_cast<double>(seq.fps),
                            static_cast<double>(seq.offset_bytes + seq.value_bytes) / 1024.0);
    }
    text += std::format("{}{} bones, {} texture{}", text.empty() ? "" : ", ", info.bones.size(), info.textures.size(),
                        info.textures.size() == 1 ? "" : "s");
    if (info.textures.size() == 1 && (info.textures[0].flags & kMdlTextureAdditive))
        text += " (additive)";
    return text;
}

} // namespace gs
