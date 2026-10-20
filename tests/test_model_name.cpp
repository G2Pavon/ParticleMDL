#include <doctest/doctest.h>

#include <string>

#include "Exporter.hpp"

using namespace gs;

TEST_CASE("model_name_problem accepts ordinary names")
{
    CHECK_FALSE(model_name_problem("fire"));
    CHECK_FALSE(model_name_problem("my fire v2"));
    CHECK_FALSE(model_name_problem("a.b"));
    CHECK_FALSE(model_name_problem("custom_particles"));
}

TEST_CASE("model_name_problem rejects names that are not plain ASCII")
{
    // "Jos" + e-acute, and a Japanese name, as UTF-8 bytes
    for (const char* name : {"Jos\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC", "caf\xC3\xA9 fire"}) {
        CAPTURE(name);
        const auto problem = model_name_problem(name);
        REQUIRE(problem);
        CHECK(problem->find("ASCII") != std::string::npos);
    }
    CHECK_FALSE(model_name_problem("plain ascii ~!@#$%^&()_+-=[]{};',.`"));
}

TEST_CASE("model_name_problem rejects an empty name")
{
    CHECK(model_name_problem(""));
    CHECK(model_name_problem("   "));
}

TEST_CASE("model_name_problem rejects path tricks")
{
    for (const char* name : {".", "..", "a/b", "a\\b", "c:x", "/abs", "../up", "..\\up"}) {
        CAPTURE(name);
        CHECK(model_name_problem(name));
    }
}

TEST_CASE("model_name_problem rejects quotes and control characters")
{
    CHECK(model_name_problem("say \"hi\""));
    CHECK(model_name_problem(std::string("tab\there")));
    CHECK(model_name_problem(std::string("line\nbreak")));
    CHECK(model_name_problem(std::string("nul\0x", 5)));
    CHECK(model_name_problem("bell\x07"));
}

TEST_CASE("model_name_problem limits the length so the texture name fits")
{
    CHECK(kMaxModelNameLength == 59);
    CHECK_FALSE(model_name_problem(std::string(kMaxModelNameLength, 'a')));
    const auto problem = model_name_problem(std::string(kMaxModelNameLength + 1, 'a'));
    REQUIRE(problem);
    CHECK(problem->find("too long") != std::string::npos);
    CHECK(problem->find("59") != std::string::npos);
}

TEST_CASE("model_name_problem explains what is wrong")
{
    CHECK(model_name_problem("")->find("empty") != std::string::npos);
    CHECK(model_name_problem("a/b")->find("must not contain") != std::string::npos);
    CHECK(model_name_problem("a\"b")->find("quotes") != std::string::npos);
}

TEST_CASE("safe_model_name turns spaces into underscores and drops .mdl")
{
    CHECK(safe_model_name("my fire") == "my_fire");
    CHECK(safe_model_name("fire.mdl") == "fire");
    CHECK(safe_model_name("a b c") == "a_b_c");
    CHECK(safe_model_name("plain") == "plain");
}
