#include <doctest/doctest.h>

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "Presets.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

std::string qc_of(const Settings& s)
{
    return export_files(ParticleSystem(s)).bytes_of(".qc");
}

struct Box {
    double v[6];
};

Box bbox_of(const std::string& qc)
{
    Box b{};
    const std::size_t at = qc.find("$bbox ");
    REQUIRE(at != std::string::npos);
    REQUIRE(std::sscanf(qc.c_str() + at, "$bbox %lf %lf %lf %lf %lf %lf", &b.v[0], &b.v[1], &b.v[2], &b.v[3], &b.v[4],
                        &b.v[5]) == 6);
    return b;
}

} // namespace

TEST_CASE("the model scale is written to the QC as $scale")
{
    Settings s;
    CHECK(s.model_scale == 1.0);
    CHECK(qc_of(s).find("\n$scale 1.0\n") != std::string::npos);   // as every export has always had
    s.model_scale = 0.25;
    CHECK(qc_of(s).find("\n$scale 0.25\n") != std::string::npos);
    s.model_scale = 3;
    CHECK(qc_of(s).find("\n$scale 3.0\n") != std::string::npos);
}

TEST_CASE("the header box is scaled with the model, since studiomdl takes it as written")
{
    Settings s;
    const Box one = bbox_of(qc_of(s));
    s.model_scale = 2;
    const Box two = bbox_of(qc_of(s));
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        // Each side is the scaled extent rounded outward plus a unit of margin.
        CHECK(std::abs(two.v[i]) >= 2 * std::abs(one.v[i]) - 3);
        CHECK(std::abs(two.v[i]) <= 2 * std::abs(one.v[i]) + 1);
    }
}

TEST_CASE("the model scale is saved, kept in range, and kept by a preset like the name")
{
    Settings s;
    s.model_scale = 0.5;
    CHECK(settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{}).settings.model_scale == 0.5);

    nlohmann::json old = to_json(Settings{});
    old.erase("model_scale");
    CHECK(settings_from_json(old, Settings{}).settings.model_scale == 1.0);   // files from before it existed

    Settings wild;
    wild.model_scale = 1000;
    CHECK(validate(wild));
    CHECK(wild.model_scale == kMaxModelScale);
    wild.model_scale = 0;
    CHECK(validate(wild));
    CHECK(wild.model_scale == kMinModelScale);

    Settings mine;
    mine.model_scale = 4;
    apply_preset(mine, *find_preset("Fire"));
    CHECK(mine.model_scale == 4);
}
