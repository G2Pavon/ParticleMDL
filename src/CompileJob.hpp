#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <stop_token>
#include <string>

#include "Compiler.hpp"

namespace gs {

// Runs compile_model() on a background thread so the UI stays responsive.
class CompileJob {
public:
    CompileJob() = default;
    ~CompileJob();   // cancels a running compile and waits for it to stop
    CompileJob(const CompileJob&) = delete;
    CompileJob& operator=(const CompileJob&) = delete;

    void start(std::string compiler, std::string safe_name, std::filesystem::path work_dir = ".");
    bool busy() const { return future_.valid(); }
    // The result on the first call after the compile finished, else nullopt.
    // Rethrows exceptions from the compile thread.
    std::optional<CompileResult> poll();
    const std::string& model_name() const { return safe_name_; }

private:
    std::stop_source stop_;
    std::future<CompileResult> future_;
    std::string safe_name_;
};

} // namespace gs
