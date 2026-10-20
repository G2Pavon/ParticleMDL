#include "TextureJob.hpp"

#include <chrono>
#include <exception>

namespace gs {

TextureJob::~TextureJob()
{
    if (future_.valid())
        future_.wait();
}

void TextureJob::start(TextureSettings settings)
{
    future_ = std::async(std::launch::async, [settings = std::move(settings)]() mutable {
        Result result;
        try {
            result.image = export_texture(settings);
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        result.settings = std::move(settings);
        return result;
    });
}

void TextureJob::request(TextureSettings settings)
{
    if (future_.valid())
        pending_ = std::move(settings);
    else
        start(std::move(settings));
}

std::optional<TextureJob::Result> TextureJob::poll()
{
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    Result result = future_.get();
    if (pending_) {
        // A newer request arrived while this one ran: its result is out of date.
        TextureSettings next = std::move(*pending_);
        pending_.reset();
        start(std::move(next));
        return std::nullopt;
    }
    return result;
}

} // namespace gs
