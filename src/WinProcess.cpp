#include "WinProcess.hpp"

namespace gs {

unsigned quote_command_line_argument(const wchar_t* arg, unsigned length, wchar_t* out) noexcept
{
    unsigned n = 0;
    auto put = [&](wchar_t c) {
        if (out)
            out[n] = c;
        ++n;
    };

    bool plain = length > 0;
    for (unsigned i = 0; i < length; ++i)
        if (arg[i] == L' ' || arg[i] == L'\t' || arg[i] == L'\n' || arg[i] == L'\v' || arg[i] == L'"')
            plain = false;
    if (plain) {
        for (unsigned i = 0; i < length; ++i)
            put(arg[i]);
        return n;
    }

    put(L'"');
    for (unsigned i = 0; i < length; ++i) {
        unsigned backslashes = 0;
        while (i < length && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == length) {
            // Backslashes before the closing quote would escape it.
            for (unsigned k = 0; k < backslashes * 2; ++k)
                put(L'\\');
            break;
        }
        if (arg[i] == L'"') {
            for (unsigned k = 0; k < backslashes * 2 + 1; ++k)
                put(L'\\');
            put(L'"');
        } else {
            for (unsigned k = 0; k < backslashes; ++k)
                put(L'\\');
            put(arg[i]);
        }
    }
    put(L'"');
    return n;
}

} // namespace gs

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gs {

namespace {

// Closes a handle when it goes out of scope.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void reset(HANDLE h = nullptr) noexcept
    {
        if (h_ && h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
        h_ = h;
    }
    HANDLE get() const noexcept { return h_; }
    bool valid() const noexcept { return h_ && h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_ = nullptr;
};

// Hands over whatever is waiting in the pipe, without ever blocking: a program
// the compiler started may still hold the pipe open after the compiler is gone.
bool drain(HANDLE pipe, const ProcessCallbacks& cb)
{
    char buffer[4096];
    bool any = false;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            return any;
        DWORD got = 0;
        const DWORD want = available < sizeof buffer ? available : static_cast<DWORD>(sizeof buffer);
        if (!ReadFile(pipe, buffer, want, &got, nullptr) || got == 0)
            return any;
        if (cb.output)
            cb.output(cb.user, buffer, got);
        any = true;
    }
}

} // namespace

ProcessResult run_process(wchar_t* command_line, const wchar_t* work_dir, unsigned long timeout_ms,
                          const ProcessCallbacks& cb)
{
    SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};

    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &inheritable, 0))
        return {ProcessStatus::StartFailed, GetLastError()};
    Handle pipe_read(read), pipe_write(write);
    SetHandleInformation(pipe_read.get(), HANDLE_FLAG_INHERIT, 0);   // only the child's end is inherited

    // An empty stdin: a program that waits for a key gets end-of-file instead of a hang.
    Handle empty_stdin(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = empty_stdin.valid() ? empty_stdin.get() : INVALID_HANDLE_VALUE;
    startup.hStdOutput = pipe_write.get();
    startup.hStdError = pipe_write.get();

    // The job ends everything the compiler started when we stop it, or when we exit.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        work_dir, &startup, &info)) {
        const DWORD error = GetLastError();
        const bool missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_ACCESS_DENIED;
        return {missing ? ProcessStatus::NotFound : ProcessStatus::StartFailed, error};
    }
    Handle process(info.hProcess), thread(info.hThread);
    pipe_write.reset();   // ours is closed, so the pipe ends with the program

    bool in_job = job.valid() && AssignProcessToJobObject(job.get(), process.get());
    ResumeThread(thread.get());

    auto stop = [&] {
        if (in_job)
            TerminateJobObject(job.get(), 1);
        else
            TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 5000);
        drain(pipe_read.get(), cb);
    };

    const ULONGLONG started = GetTickCount64();
    for (;;) {
        const bool busy = drain(pipe_read.get(), cb);
        if (WaitForSingleObject(process.get(), busy ? 0 : 20) == WAIT_OBJECT_0) {
            drain(pipe_read.get(), cb);
            break;
        }
        if (cb.stop_requested && cb.stop_requested(cb.user)) {
            stop();
            return {ProcessStatus::Cancelled, 0};
        }
        if (timeout_ms != kNoTimeout && GetTickCount64() - started >= timeout_ms) {
            stop();
            return {ProcessStatus::TimedOut, 0};
        }
    }

    DWORD code = 0;
    GetExitCodeProcess(process.get(), &code);
    return {ProcessStatus::Exited, code};
}

} // namespace gs

#endif
