#include "Compiler.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <vector>

#include "PathUtil.hpp"
#include "WinProcess.hpp"

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <csignal>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gs {

std::wstring quote_windows_argument(std::wstring_view arg)
{
    std::wstring out(arg.size() * 2 + 2, L'\0');   // every character at most doubles, plus two quotes
    out.resize(quote_command_line_argument(arg.data(), static_cast<unsigned>(arg.size()), out.data()));
    return out;
}

namespace {

namespace fs = std::filesystem;

// "./studiomdl" must not be re-resolved inside work_dir; bare names go through PATH.
std::string resolve_compiler(const std::string& compiler)
{
    if (compiler.find('/') == std::string::npos && compiler.find('\\') == std::string::npos)
        return compiler;
    return to_utf8(fs::absolute(from_utf8(compiler)));
}

#ifndef _WIN32
bool same_directory(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}
#endif

void remove_intermediates(const fs::path& dir, const std::string& safe_name)
{
    std::error_code ec;
    for (const char* suffix : {".qc", "_ref.smd", "_anim.smd", ".bmp"})
        fs::remove(dir / from_utf8(safe_name + suffix), ec);
}

#ifdef _WIN32

// Collects what the compiler writes, and tells the runner when to give up.
struct WinRun {
    std::string* output;
    const std::stop_token* stop;
};

CompileResult run_compiler(const std::string& compiler, const std::string& qc, const CompileOptions& options,
                           bool& exit_ok)
{
    CompileResult result;
    exit_ok = false;

    // CreateProcess may edit the command line, so it gets a writable buffer.
    std::wstring command = quote_windows_argument(from_utf8(compiler).wstring()) + L" " +
                           quote_windows_argument(from_utf8(qc).wstring());
    const std::wstring work_dir = fs::absolute(options.work_dir).wstring();

    WinRun run{&result.output, &options.stop};
    ProcessCallbacks callbacks;
    callbacks.user = &run;
    callbacks.output = [](void* user, const char* data, unsigned long size) {
        static_cast<WinRun*>(user)->output->append(data, size);
    };
    callbacks.stop_requested = [](void* user) { return static_cast<WinRun*>(user)->stop->stop_requested(); };

    const auto millis = options.timeout.count();
    const unsigned long timeout = millis < 0 || millis >= static_cast<long long>(kNoTimeout)
                                      ? kNoTimeout - 1
                                      : static_cast<unsigned long>(millis);
    const ProcessResult run_result = run_process(command.data(), work_dir.c_str(), timeout, callbacks);
    switch (run_result.status) {
    case ProcessStatus::Exited:
        exit_ok = run_result.exit_code == 0;
        break;
    case ProcessStatus::NotFound:
        result.status = CompileResult::Status::CompilerNotFound;
        break;
    case ProcessStatus::StartFailed:
        result.status = CompileResult::Status::Failed;
        break;
    case ProcessStatus::TimedOut:
        result.status = CompileResult::Status::TimedOut;
        break;
    case ProcessStatus::Cancelled:
        result.status = CompileResult::Status::Cancelled;
        break;
    }
    return result;
}

#else

// Owns a posix_spawn_file_actions_t.
class SpawnActions {
public:
    SpawnActions()
    {
        if (posix_spawn_file_actions_init(&actions_) != 0)
            throw std::runtime_error("posix_spawn_file_actions_init failed");
    }
    ~SpawnActions() { posix_spawn_file_actions_destroy(&actions_); }
    SpawnActions(const SpawnActions&) = delete;
    SpawnActions& operator=(const SpawnActions&) = delete;
    posix_spawn_file_actions_t* get() noexcept { return &actions_; }

private:
    posix_spawn_file_actions_t actions_;
};

class Pipe {
public:
    Pipe()
    {
        if (pipe(fd_) != 0)
            throw std::runtime_error("pipe() failed");
    }
    ~Pipe()
    {
        close_read();
        close_write();
    }
    int read_end() const { return fd_[0]; }
    int write_end() const { return fd_[1]; }
    void close_read() { close_fd(fd_[0]); }
    void close_write() { close_fd(fd_[1]); }

private:
    static void close_fd(int& fd)
    {
        if (fd >= 0)
            close(fd);
        fd = -1;
    }
    int fd_[2] = {-1, -1};
};

CompileResult run_compiler(const std::string& compiler, const std::string& qc, const CompileOptions& options,
                           bool& exit_ok)
{
    CompileResult result;
    exit_ok = false;
    Pipe out;

    SpawnActions actions;
    // An empty stdin: a compiler that waits for a key gets end-of-file instead of a hang.
    posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(actions.get(), out.write_end(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(actions.get(), out.write_end(), STDERR_FILENO);
    posix_spawn_file_actions_addclose(actions.get(), out.read_end());
    posix_spawn_file_actions_addclose(actions.get(), out.write_end());
    if (!same_directory(options.work_dir, fs::current_path())) {
#ifdef GS_HAVE_SPAWN_ADDCHDIR
        posix_spawn_file_actions_addchdir_np(actions.get(), options.work_dir.c_str());
#else
        throw std::runtime_error("compiling outside the current folder is not supported on this platform");
#endif
    }

    // posix_spawn takes char* const[]; give it owned, writable copies.
    std::string program = compiler, argument = qc;
    std::array<char*, 3> argv = {program.data(), argument.data(), nullptr};
    pid_t pid = 0;
    const int err = posix_spawnp(&pid, program.c_str(), actions.get(), nullptr, argv.data(), environ);
    out.close_write();
    if (err != 0) {
        result.status = (err == ENOENT || err == EACCES) ? CompileResult::Status::CompilerNotFound
                                                         : CompileResult::Status::Failed;
        return result;
    }

    // Collect output until EOF, killing the compiler on timeout or cancel.
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    std::optional<CompileResult::Status> stopped;
    int wstatus = 0;
    bool reaped = false;
    char buf[4096];
    for (;;) {
        if (!stopped) {
            if (options.stop.stop_requested())
                stopped = CompileResult::Status::Cancelled;
            else if (std::chrono::steady_clock::now() >= deadline)
                stopped = CompileResult::Status::TimedOut;
            if (stopped)
                kill(pid, SIGKILL);
        }
        pollfd p{out.read_end(), POLLIN, 0};
        const int ready = poll(&p, 1, 100);
        if (ready < 0 && errno != EINTR)
            break;
        if (ready == 0) {
            // Idle: stop once the compiler is gone, even if a process it
            // started still holds the pipe open.
            if (waitpid(pid, &wstatus, WNOHANG) == pid) {
                reaped = true;
                break;
            }
            continue;
        }
        if (ready < 0)
            continue;
        const ssize_t n = read(out.read_end(), buf, sizeof buf);
        if (n == 0)
            break;   // EOF: the compiler (and anything it started) closed the pipe
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            break;
        }
        result.output.append(buf, static_cast<std::size_t>(n));
    }

    while (!reaped && waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
    }
    if (stopped) {
        result.status = *stopped;
        return result;
    }
    exit_ok = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
    return result;
}

#endif

} // namespace

CompileResult compile_model(const std::string& compiler, const std::string& safe_name, const CompileOptions& options)
{
    bool exit_ok = false;
    CompileResult result = run_compiler(resolve_compiler(compiler), safe_name + ".qc", options, exit_ok);
    if (result.status == CompileResult::Status::CompilerNotFound || result.status == CompileResult::Status::TimedOut ||
        result.status == CompileResult::Status::Cancelled)
        return result;

    if (!exit_ok || !fs::exists(options.work_dir / from_utf8(safe_name + ".mdl"))) {
        result.status = CompileResult::Status::Failed;
        return result;
    }
    remove_intermediates(options.work_dir, safe_name);
    result.status = CompileResult::Status::Ok;
    return result;
}

} // namespace gs
