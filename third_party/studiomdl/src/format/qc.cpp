#include "qc.hpp"

#include "utils/cmdlib.hpp"

void QcTokenizer::load(std::string_view text)
{
    qc_script_buffer.assign(text.begin(), text.end());
    qc_script_buffer.push_back('\0'); // keeps lookahead reads in bounds
    qc_stream_p = qc_script_buffer.data();
    qc_stream_end_p = qc_stream_p + text.size();
    qc_line_number = 1;
    end_of_qc_file = false;
    token_ready = false;
}

bool QcTokenizer::get_token(bool crossline, std::string &token)
{
    if (token_ready)
    {
        token_ready = false;
        return true;
    }

    if (qc_stream_p >= qc_stream_end_p)
    {
        end_of_qc_file = true;
        return false;
    }

    while (*qc_stream_p <= 32 || *qc_stream_p == ';' || *qc_stream_p == '#' || (*qc_stream_p == '/' && *(qc_stream_p + 1) == '/'))
    {
        if (qc_stream_p >= qc_stream_end_p)
        {
            end_of_qc_file = true;
            return false;
        }
        if (*qc_stream_p++ == '\n')
        {
            if (!crossline)
                error("Line " + std::to_string(qc_line_number) + " is incomplete");
            qc_line_number++;
        }
        if (*qc_stream_p == ';' || *qc_stream_p == '#' || (*qc_stream_p == '/' && *(qc_stream_p + 1) == '/'))
        {
            while (*qc_stream_p != '\n' && qc_stream_p < qc_stream_end_p)
                qc_stream_p++;
        }
    }

    token.clear();

    if (*qc_stream_p == '"')
    {
        qc_stream_p++;
        while (*qc_stream_p != '"' && qc_stream_p < qc_stream_end_p)
            token.push_back(*qc_stream_p++);
        qc_stream_p++;
    }
    else
    {
        while (*qc_stream_p > 32 && *qc_stream_p != ';' && *qc_stream_p != '\n' && qc_stream_p < qc_stream_end_p)
            token.push_back(*qc_stream_p++);
    }

    return true;
}

bool QcTokenizer::token_available() const
{
    for (const char *search_p = qc_stream_p; search_p < qc_stream_end_p; search_p++)
    {
        if (*search_p == '\n')
            return false;
        if (*search_p > 32)
            return *search_p != ';';
    }
    return false;
}
