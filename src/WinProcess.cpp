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
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600   // Vista: PROC_THREAD_ATTRIBUTE_HANDLE_LIST
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

// The attribute that limits what a child inherits to the handles named in it. Without it, the
// child gets every inheritable handle of this process: files the C runtime opened on another
// thread (inheritable by default) would stay open in the compiler, and could not be renamed.
class InheritOnly {
public:
    InheritOnly(HANDLE* handles, int count)
    {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        list_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, size));
        if (!list_)
            return;
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &size)) {
            HeapFree(GetProcessHeap(), 0, list_);
            list_ = nullptr;
            return;
        }
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                       static_cast<SIZE_T>(count) * sizeof(HANDLE), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(list_);
            HeapFree(GetProcessHeap(), 0, list_);
            list_ = nullptr;
        }
    }
    ~InheritOnly()
    {
        if (list_) {
            DeleteProcThreadAttributeList(list_);
            HeapFree(GetProcessHeap(), 0, list_);
        }
    }
    InheritOnly(const InheritOnly&) = delete;
    InheritOnly& operator=(const InheritOnly&) = delete;

    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }   // null if it could not be made

private:
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
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
    // 64 KB rather than the default page: a compiler that prints a lot waits less for us to read it.
    if (!CreatePipe(&read, &write, &inheritable, 64 * 1024))
        return {ProcessStatus::StartFailed, GetLastError()};
    Handle pipe_read(read), pipe_write(write);
    SetHandleInformation(pipe_read.get(), HANDLE_FLAG_INHERIT, 0);   // only the child's end is inherited

    // An empty stdin: a program that waits for a key gets end-of-file instead of a hang.
    Handle empty_stdin(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = empty_stdin.valid() ? empty_stdin.get() : INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = pipe_write.get();
    startup.StartupInfo.hStdError = pipe_write.get();

    // The child inherits its standard handles and nothing else (each handle once in the list).
    HANDLE inherited[2] = {pipe_write.get(), empty_stdin.get()};
    const InheritOnly inherit_only(inherited, empty_stdin.valid() ? 2 : 1);
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (inherit_only.get()) {
        startup.lpAttributeList = inherit_only.get();
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    } else {
        startup.StartupInfo.cb = sizeof startup.StartupInfo;   // as before: everything inheritable is inherited
    }

    // The job ends everything the compiler started when we stop it, or when we exit.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, TRUE, flags, nullptr, work_dir, &startup.StartupInfo,
                        &info)) {
        const DWORD error = GetLastError();
        const bool missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_ACCESS_DENIED;
        return {missing ? ProcessStatus::NotFound : ProcessStatus::StartFailed, error};
    }
    Handle process(info.hProcess), thread(info.hThread);
    pipe_write.reset();   // ours is closed, so the pipe ends with the program

    bool in_job = job.valid() && AssignProcessToJobObject(job.get(), process.get());
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.get(), 1);
        return {ProcessStatus::StartFailed, error};
    }

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
    if (!GetExitCodeProcess(process.get(), &code))
        return {ProcessStatus::StartFailed, GetLastError()};
    return {ProcessStatus::Exited, code};
}

unsigned long ansi_to_utf8(const char* text, unsigned long size, char* out, unsigned long capacity) noexcept
{
    if (size == 0)
        return 0;
    const int wide = MultiByteToWideChar(CP_ACP, 0, text, static_cast<int>(size), nullptr, 0);
    if (wide <= 0)
        return 0;
    wchar_t* buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(wide) * sizeof(wchar_t)));
    if (!buffer)
        return 0;
    MultiByteToWideChar(CP_ACP, 0, text, static_cast<int>(size), buffer, wide);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, wide, out, out ? static_cast<int>(capacity) : 0, nullptr, nullptr);
    HeapFree(GetProcessHeap(), 0, buffer);
    return bytes > 0 ? static_cast<unsigned long>(bytes) : 0;
}

unsigned long describe_error(unsigned long code, char* out, unsigned long capacity) noexcept
{
    if (capacity == 0)
        return 0;
    wchar_t text[512];
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text,
                                  sizeof text / sizeof text[0], nullptr);
    while (length > 0 && (text[length - 1] == L'\n' || text[length - 1] == L'\r' || text[length - 1] == L' ' ||
                          text[length - 1] == L'.'))
        --length;
    int bytes = length > 0 ? WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out,
                                                 static_cast<int>(capacity - 1), nullptr, nullptr)
                           : 0;
    if (bytes < 0)
        bytes = 0;
    out[bytes] = '\0';
    return static_cast<unsigned long>(bytes);
}

} // namespace gs

#endif
