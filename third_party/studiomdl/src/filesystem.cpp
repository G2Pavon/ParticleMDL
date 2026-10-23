#include "studiomdl/filesystem.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace studiomdl
{

DiskFileSystem::DiskFileSystem(std::filesystem::path root) : root_(std::move(root)) {}

std::optional<std::vector<std::uint8_t>> DiskFileSystem::read(std::string_view path) const
{
    const std::filesystem::path full = root_ / std::filesystem::path(path);

    std::error_code ec;
    if (!std::filesystem::is_regular_file(full, ec))
        return std::nullopt;

    std::ifstream file(full, std::ios::binary | std::ios::ate);
    if (!file)
        return std::nullopt;
    const std::streamoff size = file.tellg();
    if (size < 0)
        return std::nullopt;
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(data.data()), size))
        return std::nullopt;
    return data;
}

void MemoryFileSystem::add(std::string_view path, std::vector<std::uint8_t> data)
{
    files_[normalize(path)] = std::move(data);
}

void MemoryFileSystem::add(std::string_view path, std::string_view text)
{
    add(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::optional<std::vector<std::uint8_t>> MemoryFileSystem::read(std::string_view path) const
{
    auto it = files_.find(normalize(path));
    if (it == files_.end())
        return std::nullopt;
    return it->second;
}

std::optional<std::span<const std::uint8_t>> MemoryFileSystem::view(std::string_view path) const
{
    auto it = files_.find(normalize(path));
    if (it == files_.end())
        return std::nullopt;
    return std::span<const std::uint8_t>(it->second);
}

std::string MemoryFileSystem::normalize(std::string_view path)
{
    std::string key{path};
    std::replace(key.begin(), key.end(), '\\', '/');
    key = std::filesystem::path(key).lexically_normal().generic_string();
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return key;
}

} // namespace studiomdl
