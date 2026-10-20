#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace gs {

// Writes a set of files so that either all of them replace the old ones or none
// does. Every file is written as "<target>.tmp" first; commit() then moves each
// old file aside as "<target>.bak" and each new one into place. If any step
// fails, everything is put back as it was and commit() throws.
class StagedFiles {
public:
    // How a file is renamed; replaceable so tests can make a chosen step fail.
    using Rename = std::function<void(const std::filesystem::path& from, const std::filesystem::path& to,
                                      std::error_code& ec)>;

    explicit StagedFiles(std::filesystem::path dir, Rename rename = {});
    StagedFiles(const StagedFiles&) = delete;
    StagedFiles& operator=(const StagedFiles&) = delete;
    ~StagedFiles();   // removes the temporary files that were not committed

    // `name` is relative to the folder. Throws std::runtime_error if it cannot be written.
    void add(const std::filesystem::path& name, std::span<const char> bytes);

    // Throws std::runtime_error, with the old files back in place, if a target
    // is not a regular file or a rename fails.
    void commit();

private:
    struct Entry {
        std::filesystem::path target, temp, backup;
        bool backed_up = false;   // the old file waits at `backup`
        bool placed = false;      // the new file is at `target`
    };
    void roll_back() noexcept;

    std::filesystem::path dir_;
    Rename rename_;
    std::vector<Entry> files_;
};

// Replaces `path` with `text`, keeping the old file if anything goes wrong.
void write_file_atomically(const std::filesystem::path& path, std::string_view text);

} // namespace gs
