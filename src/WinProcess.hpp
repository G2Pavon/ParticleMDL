#pragma once

// Running a program on Windows: the part of the compiler launcher that is plain
// Win32. It uses no standard library types so it can be built and tried on its
// own. Compiler.cpp wraps it.

namespace gs {

// Appends the arguments of one command line the way CommandLineToArgvW and the
// C runtime read them back: quoted when they hold a space, tab, newline or
// quote, with the backslashes before a quote doubled. Writes into `out` when it
// is not null; returns the number of wchar_t written (or needed).
unsigned quote_command_line_argument(const wchar_t* arg, unsigned length, wchar_t* out) noexcept;

enum class ProcessStatus {
    Exited,       // ran to the end; see exit_code
    NotFound,     // no such program (or no access to it)
    StartFailed,  // anything else that kept it from starting
    TimedOut,     // killed after the time limit
    Cancelled,    // killed because stop_requested() said so
};

struct ProcessResult {
    ProcessStatus status = ProcessStatus::StartFailed;
    unsigned long exit_code = 0;   // of the program, or the Win32 error for StartFailed
};

struct ProcessCallbacks {
    void* user = nullptr;
    // Gets what the program writes to stdout and stderr, in order, as it arrives.
    void (*output)(void* user, const char* data, unsigned long size) = nullptr;
    // Asked every few milliseconds; true kills the program. May be null.
    bool (*stop_requested)(void* user) = nullptr;
};

inline constexpr unsigned long kNoTimeout = 0xFFFFFFFFul;

#ifdef _WIN32
// Starts `command_line` (writable: Windows may edit it) with `work_dir` as its
// current folder (null: ours), without a console window and with an empty
// stdin, and waits for it. Anything it started is stopped with it.
ProcessResult run_process(wchar_t* command_line, const wchar_t* work_dir, unsigned long timeout_ms,
                          const ProcessCallbacks& callbacks);

// Text in the ANSI code page (what a program that prints char strings writes) as UTF-8, written
// into `out` when it is not null; returns the bytes written (or needed), 0 if it cannot be converted.
unsigned long ansi_to_utf8(const char* text, unsigned long size, char* out, unsigned long capacity) noexcept;

// Windows' own description of an error code, as UTF-8 without the trailing line break and full
// stop, and zero-terminated; returns its length, 0 (an empty text) when there is none or it does
// not fit in `capacity` - 1 bytes. It may hold "%1" where Windows would name the program.
unsigned long describe_error(unsigned long code, char* out, unsigned long capacity) noexcept;
#endif

} // namespace gs
