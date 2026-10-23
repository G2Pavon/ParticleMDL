#pragma once

#include <charconv>
#include <cfloat>
#include <climits>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

// Reads whitespace-separated values from one line. For well-formed input it
// gives the same results as std::istringstream's operator>> in the "C" locale
// (correctly rounded floats, leading '+', no inf/nan spellings), without
// streams, locales or allocations.
class LineScanner
{
public:
    explicit LineScanner(std::string_view line) : p_(line.data()), end_(line.data() + line.size()) {}

    // On a malformed number stores 0, like the stream extractor; at the end of
    // the line it leaves `value` untouched, as the stream does.
    bool read(int &value)
    {
        skip_space();
        if (p_ == end_)
            return false;
        const char *start = p_;
        if (!skip_plus())
            return fail(value, start);
        const auto [ptr, ec] = std::from_chars(p_, end_, value);
        if (ec == std::errc::result_out_of_range)
        {
            value = *p_ == '-' ? INT_MIN : INT_MAX;
            p_ = ptr;
            return false;
        }
        if (ec != std::errc{})
            return fail(value, start);
        p_ = ptr;
        return true;
    }

    bool read(float &value)
    {
        skip_space();
        const char *start = p_;
        if (!skip_plus())
            return false;
        const char *digits = (p_ != end_ && *p_ == '-') ? p_ + 1 : p_;
        if (digits == end_ || !((*digits >= '0' && *digits <= '9') || *digits == '.'))
        {
            p_ = start;
            return false;
        }
        const auto [ptr, ec] = std::from_chars(p_, end_, value, std::chars_format::general);
        if (ec == std::errc::result_out_of_range)
        {
            // Overflow fails like the stream; underflow yields a tiny value like strtof.
            double wide = 0;
            std::from_chars(p_, end_, wide, std::chars_format::general);
            if (!(std::abs(wide) <= FLT_MAX))
                return false;
            value = static_cast<float>(wide);
        }
        else if (ec != std::errc{})
        {
            p_ = start;
            return false;
        }
        p_ = ptr;
        return true;
    }

    template <typename... T>
    bool read_all(T &...values)
    {
        return (read(values) && ...);
    }

    bool read_word(std::string &word)
    {
        skip_space();
        if (p_ == end_)
            return false;
        const char *start = p_;
        while (p_ != end_ && !is_space(*p_))
            ++p_;
        word.assign(start, p_);
        return true;
    }

    // Like `>> std::quoted(word)`: a "..." string with '\' escapes, or else a plain word.
    bool read_quoted(std::string &word)
    {
        skip_space();
        if (p_ == end_ || *p_ != '"')
            return read_word(word);
        word.clear();
        for (++p_; p_ != end_; ++p_)
        {
            if (*p_ == '\\' && p_ + 1 != end_)
                ++p_;
            else if (*p_ == '"')
            {
                ++p_;
                break;
            }
            word.push_back(*p_);
        }
        return true;
    }

private:
    static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

    void skip_space()
    {
        while (p_ != end_ && is_space(*p_))
            ++p_;
    }

    // Accepts one leading '+' (std::from_chars does not), but not "+-".
    bool skip_plus()
    {
        if (p_ != end_ && *p_ == '+')
        {
            ++p_;
            if (p_ != end_ && (*p_ == '-' || *p_ == '+'))
                return false;
        }
        return true;
    }

    bool fail(int &value, const char *start)
    {
        value = 0;
        p_ = start;
        return false;
    }

    const char *p_;
    const char *end_;
};
