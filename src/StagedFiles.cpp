#include "StagedFiles.hpp"

#include <fstream>
#include <stdexcept>

#include "PathUtil.hpp"

namespace gs {

namespace fs = std::filesystem;

StagedFiles::StagedFiles(fs::path dir, Rename rename) : dir_(std::move(dir)), rename_(std::move(rename))
{
    if (!rename_)
        rename_ = [](const fs::path& from, const fs::path& to, std::error_code& ec) { fs::rename(from, to, ec); };
}

StagedFiles::~StagedFiles()
{
    // Only the temporary files go. A backup is never removed here: if a rollback could
    // not put an old file back, the backup is the only copy of it.
    std::error_code ec;
    for (const Entry& e : files_)
        fs::remove(e.temp, ec);
}

void StagedFiles::add(const fs::path& name, std::span<const char> bytes)
{
    Entry e;
    e.target = dir_ / name;
    e.temp = e.target;
    e.temp += ".tmp";
    e.backup = e.target;
    e.backup += ".bak";
    files_.push_back(e);   // registered first: cleaned up even if the write fails

    std::ofstream f(e.temp, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    f.close();
    if (!f)
        throw std::runtime_error("cannot write " + to_utf8(e.target));
}

void StagedFiles::roll_back() noexcept
{
    std::error_code ec;
    for (auto it = files_.rbegin(); it != files_.rend(); ++it) {
        if (it->placed) {
            fs::remove(it->target, ec);
            it->placed = false;
        }
        if (it->backed_up) {
            rename_(it->backup, it->target, ec);
            if (!ec)
                it->backed_up = false;
        }
    }
}

void StagedFiles::commit()
{
    std::error_code ec;
    for (const Entry& e : files_)
        if (fs::exists(e.target, ec) && !fs::is_regular_file(e.target, ec))
            throw std::runtime_error(to_utf8(e.target) + " exists and is not a regular file");

    for (Entry& e : files_) {
        if (fs::exists(e.target, ec)) {
            fs::remove(e.backup, ec);   // left behind by a crash
            rename_(e.target, e.backup, ec);
            if (ec) {
                roll_back();
                throw std::runtime_error("cannot replace " + to_utf8(e.target) + ": " + ec.message() +
                                         " (the previous files were kept)");
            }
            e.backed_up = true;
        }
        rename_(e.temp, e.target, ec);
        if (ec) {
            roll_back();
            throw std::runtime_error("cannot replace " + to_utf8(e.target) + ": " + ec.message() +
                                     " (the previous files were kept)");
        }
        e.placed = true;
    }

    for (const Entry& e : files_)
        if (e.backed_up)
            fs::remove(e.backup, ec);
    files_.clear();
}

void write_file_atomically(const fs::path& path, std::string_view text)
{
    fs::path dir = path.parent_path();
    if (dir.empty())
        dir = ".";
    StagedFiles files(dir);
    files.add(path.filename(), {text.data(), text.size()});
    files.commit();
}

} // namespace gs
