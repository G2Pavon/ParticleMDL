#include "CompileJob.hpp"

#include <chrono>

namespace gs {

CompileJob::~CompileJob()
{
    stop_.request_stop();
    if (future_.valid())
        future_.wait();
}

void CompileJob::start(std::string compiler, std::string safe_name, std::filesystem::path work_dir)
{
    stop_ = std::stop_source();   // a fresh one: a stop request is permanent
    safe_name_ = safe_name;
    CompileOptions options;
    options.work_dir = std::move(work_dir);
    options.stop = stop_.get_token();
    future_ = std::async(std::launch::async, [compiler = std::move(compiler), safe_name = std::move(safe_name),
                                              options = std::move(options)] {
        return compile_model(compiler, safe_name, options);
    });
}

std::optional<CompileResult> CompileJob::poll()
{
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return future_.get();
}

} // namespace gs
