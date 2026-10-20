#pragma once

#include <future>
#include <optional>
#include <string>

#include "TextureGenerator.hpp"

namespace gs {

// Makes the export texture on a worker thread, so that decoding and reducing a large picture
// does not stop the editor. Only the newest request counts: asking again while one is running
// waits for it to end, drops its result and runs the new one.
class TextureJob {
public:
    struct Result {
        TextureSettings settings;           // what the texture was made from
        std::optional<IndexedImage> image;  // empty if it could not be made
        std::string error;                  // then why
    };

    TextureJob() = default;
    ~TextureJob();   // waits for the running one
    TextureJob(const TextureJob&) = delete;
    TextureJob& operator=(const TextureJob&) = delete;

    void request(TextureSettings settings);
    bool busy() const { return future_.valid(); }
    // The result of the newest request once it is ready; nothing before, and nothing for a
    // request that a newer one has replaced.
    std::optional<Result> poll();

private:
    void start(TextureSettings settings);

    std::future<Result> future_;
    std::optional<TextureSettings> pending_;
};

} // namespace gs
