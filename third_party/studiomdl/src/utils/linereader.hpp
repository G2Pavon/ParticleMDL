#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Reads text line by line from a buffer the caller keeps alive.
class LineReader
{
public:
    LineReader() = default;
    explicit LineReader(std::string_view text) : text_(text) {}

    // Stores the next line, without its "\n" or "\r\n" terminator, in `line`.
    // Returns false at the end of the input.
    bool next(std::string &line)
    {
        if (pos_ >= text_.size())
            return false;
        const std::size_t newline = text_.find('\n', pos_);
        const std::size_t stop = newline == std::string_view::npos ? text_.size() : newline;
        line.assign(text_.substr(pos_, stop - pos_));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        pos_ = newline == std::string_view::npos ? text_.size() : newline + 1;
        return true;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};
