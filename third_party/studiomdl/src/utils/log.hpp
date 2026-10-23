#pragma once

#include <format>
#include <string_view>
#include <utility>

#include "studiomdl/studiomdl.hpp"

// Routes compiler diagnostics to the caller's studiomdl::LogCallback.
// Formatting is skipped entirely when no callback is set.
class Logger
{
public:
    explicit Logger(studiomdl::LogCallback callback) : callback_(std::move(callback)) {}

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args &&...args) const
    {
        if (callback_)
            callback_(studiomdl::LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args &&...args) const
    {
        if (callback_)
            callback_(studiomdl::LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    studiomdl::LogCallback callback_;
};
