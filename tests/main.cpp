// The test runner. It is also the fake studiomdl of the compiler tests: run
// with a single "<name>.qc" argument, it plays the part of the compiler.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "fake_studiomdl.hpp"

void set_self_path(const std::string& path);   // test_compiler.cpp

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]).ends_with(".qc"))
        return fake_studiomdl(argv[1]);

    set_self_path(argv[0]);
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
