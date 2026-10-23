#include "Compiler.hpp"

#include <string>
#include <string_view>

#include <studiomdl/studiomdl.hpp>

#include "PathUtil.hpp"
#include "StagedFiles.hpp"

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

CompiledModel export_mdl(const ParticleSystem& sys, const BakedPoses& baked, const AnimationTable& table,
                         const std::filesystem::path& out_dir)
{
    if (const auto problem = model_name_problem(sys.settings().model_name))
        throw std::invalid_argument(*problem);
    std::error_code ec;
    if (!std::filesystem::is_directory(out_dir, ec))
        throw std::runtime_error("export folder does not exist: " + to_utf8(out_dir));

    const ExportFiles files = export_files(sys, baked, table);
    CompiledModel model = compile_model(files);
    write_file_atomically(out_dir / from_utf8(files.safe_name + ".mdl"),
                          {reinterpret_cast<const char*>(model.mdl.data()), model.mdl.size()});
    return model;
}

CompiledModel export_mdl(const ParticleSystem& sys, const std::filesystem::path& out_dir)
{
    const BakedPoses baked = bake(sys);
    return export_mdl(sys, baked, animation_table(sys, baked), out_dir);
}

} // namespace gs
