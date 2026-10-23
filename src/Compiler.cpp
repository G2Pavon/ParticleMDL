#include "Compiler.hpp"

#include <string>
#include <string_view>

#include <studiomdl/studiomdl.hpp>

namespace gs {

CompiledModel compile_model(const ExportFiles& files)
{
    studiomdl::MemoryFileSystem fs;
    for (const ExportFile& file : files.files)
        fs.add(file.name, file.bytes);

    CompiledModel out;
    studiomdl::CompileOptions options;
    options.log = [&out](studiomdl::LogLevel level, std::string_view message) {
        (level == studiomdl::LogLevel::Warning ? out.warnings : out.notes).emplace_back(message);
    };
    try {
        out.mdl = studiomdl::compile(files.safe_name + ".qc", fs, options).mdl;
    } catch (const studiomdl::CompileError& e) {
        std::string message = e.what();
        while (!message.empty() && (message.back() == '\n' || message.back() == ' '))
            message.pop_back();   // some of its messages end in a line break
        throw CompileFailed(message);
    }
    return out;
}

} // namespace gs
