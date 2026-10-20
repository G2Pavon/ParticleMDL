#include "PathInput.hpp"

#include <cstdlib>

#include "PathUtil.hpp"

namespace gs {

namespace {

namespace fs = std::filesystem;

std::string_view trim(std::string_view s)
{
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    return s;
}

fs::path expand_home(std::string_view input)
{
    if (input == "~" || input.starts_with("~/")) {
#ifdef _WIN32
        const wchar_t* home = _wgetenv(L"USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home && *home)
            return fs::path(home) / from_utf8(input.substr(input.size() > 1 ? 2 : 1));
    }
    return from_utf8(input);
}

} // namespace

DialogInput resolve_dialog_input(const fs::path& dir, std::string_view input, bool save, std::string_view extension)
{
    input = trim(input);
    if (input.empty())
        return {};

    const fs::path typed = expand_home(input);
    fs::path path = (typed.is_absolute() ? typed : dir / typed).lexically_normal();
    if (!path.has_filename() && path != path.root_path())   // "a/b/" -> "a/b", so "Up" works next
        path = path.parent_path();
    std::error_code ec;
    if (fs::is_directory(path, ec))
        return {DialogInput::Action::Navigate, path, {}};

    if (save && !extension.empty() && path.extension() != from_utf8(extension))
        path += from_utf8(extension);

    if (!fs::is_directory(path.parent_path(), ec))
        return {DialogInput::Action::Error, path, "Folder does not exist: " + to_utf8(path.parent_path())};

    if (!save)
        return fs::is_regular_file(path, ec)
                   ? DialogInput{DialogInput::Action::Choose, path, {}}
                   : DialogInput{DialogInput::Action::Error, path, "File not found: " + to_utf8(path.filename())};

    if (fs::exists(path, ec))
        return fs::is_regular_file(path, ec)
                   ? DialogInput{DialogInput::Action::Confirm, path, {}}
                   : DialogInput{DialogInput::Action::Error, path, "Not a file: " + to_utf8(path)};
    return {DialogInput::Action::Choose, path, {}};
}

} // namespace gs
