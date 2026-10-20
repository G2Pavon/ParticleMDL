#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gs {

// Paths kept as text (the texture file in a settings file, the export folder, the
// compiler, what is typed in a dialog) are UTF-8 on every platform. std::filesystem
// reads a narrow string in the ANSI code page on Windows, so these two are the way
// between the text and a path.
inline std::filesystem::path from_utf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

inline std::string to_utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace gs
