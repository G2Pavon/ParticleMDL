#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "studiomdl/filesystem.hpp"

// A file's contents: a view into the file system's storage when it offers one,
// otherwise an owned copy. Copies and moves keep `bytes()` valid.
class FileBytes
{
public:
    FileBytes() = default;
    explicit FileBytes(std::span<const std::uint8_t> view) : bytes_(view) {}
    explicit FileBytes(std::vector<std::uint8_t> data) : owned_(std::move(data)), owns_(true) { bytes_ = owned_; }

    FileBytes(const FileBytes &other) : owned_(other.owned_), bytes_(other.bytes_), owns_(other.owns_)
    {
        if (owns_)
            bytes_ = owned_;
    }
    FileBytes(FileBytes &&other) noexcept
        : owned_(std::move(other.owned_)), bytes_(other.bytes_), owns_(other.owns_)
    {
        if (owns_)
            bytes_ = owned_;
        other.bytes_ = {};
    }
    FileBytes &operator=(FileBytes other) noexcept
    {
        owned_ = std::move(other.owned_);
        owns_ = other.owns_;
        bytes_ = owns_ ? std::span<const std::uint8_t>(owned_) : other.bytes_;
        return *this;
    }

    std::span<const std::uint8_t> bytes() const { return bytes_; }
    std::string_view text() const { return {reinterpret_cast<const char *>(bytes_.data()), bytes_.size()}; }

    // Reads `path` without copying when `fs` supports views.
    static std::optional<FileBytes> load(const studiomdl::FileSystem &fs, std::string_view path)
    {
        if (auto view = fs.view(path))
            return FileBytes{*view};
        if (auto data = fs.read(path))
            return FileBytes{std::move(*data)};
        return std::nullopt;
    }

private:
    std::vector<std::uint8_t> owned_;
    std::span<const std::uint8_t> bytes_;
    bool owns_ = false;
};
