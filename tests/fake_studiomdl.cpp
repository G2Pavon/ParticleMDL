#include "fake_studiomdl.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

int fake_studiomdl(const char* qc_name)
{
    std::string mode;
    {
        std::ifstream qc(qc_name);
        std::getline(qc, mode);
    }
    const fs::path mdl = fs::path(qc_name).replace_extension(".mdl");

    if (mode == "ok") {
        std::ofstream(mdl) << "IDST";
        std::printf("compiled %s\n", qc_name);
        std::fflush(stdout);
        std::fprintf(stderr, "warning: a note on stderr\n");
        return 0;
    }
    if (mode == "fail") {
        std::printf("ERROR: bad qc\n");
        return 1;
    }
    if (mode == "nomdl")
        return 0;
    if (mode == "sleep") {
        std::printf("started\n");
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::ofstream(mdl) << "IDST";
        return 0;
    }
    if (mode == "cwd") {
        std::printf("%s", fs::current_path().string().c_str());
        return 0;
    }
    if (mode == "stdin") {
        long total = 0;
        while (std::getchar() != EOF)
            ++total;
        std::printf("stdin: %ld bytes", total);
        return 0;
    }
    if (mode == "lots") {
        for (int i = 0; i < 20000; ++i)
            std::printf("line %05d: the quick brown fox jumps over the lazy dog, again and again and again.\n", i);
        return 0;
    }
    if (mode == "args") {
        std::printf("%s", qc_name);
        return 0;
    }
    std::printf("unknown mode '%s'\n", mode.c_str());
    return 2;
}
