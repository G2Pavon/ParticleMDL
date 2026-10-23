#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace studiomdl
{

// Source of every file a compile reads: the QC script, SMDs and textures.
// Paths use '/' separators. Relative $cd/$cdtexture paths are resolved against
// the QC's directory, and SMD/texture names against $cd/$cdtexture. Without a
// $cd (or $cdtexture), SMDs (or textures) are looked up relative to the file
// system root, as the original tool did with the working directory.
// Implementations must be safe to call from several threads if the same
// instance is shared between concurrent compiles.
class FileSystem
{
public:
    virtual ~FileSystem() = default;

    // Returns the whole file, or std::nullopt if it does not exist.
    virtual std::optional<std::vector<std::uint8_t>> read(std::string_view path) const = 0;

    // Optional zero-copy access: the file's bytes, valid while this file system
    // is alive and unmodified. Returns std::nullopt when the file is missing or
    // the implementation does not support views, and read() is used instead.
    virtual std::optional<std::span<const std::uint8_t>> view(std::string_view path) const
    {
        (void)path;
        return std::nullopt;
    }
};

// Reads from disk. Relative paths are resolved against `root`; with an empty
// root they are resolved against the process working directory.
class DiskFileSystem final : public FileSystem
{
public:
    DiskFileSystem() = default;
    explicit DiskFileSystem(std::filesystem::path root);

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override;

private:
    std::filesystem::path root_;
};

// Serves files added by the caller. Lookups ignore ASCII case and treat '\'
// like '/', and "." / ".." segments are resolved, like GoldSrc tools on Windows.
class MemoryFileSystem final : public FileSystem
{
public:
    void add(std::string_view path, std::vector<std::uint8_t> data);
    void add(std::string_view path, std::string_view text);

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override;
    std::optional<std::span<const std::uint8_t>> view(std::string_view path) const override;

private:
    static std::string normalize(std::string_view path);

    std::unordered_map<std::string, std::vector<std::uint8_t>> files_;
};

} // namespace studiomdl
