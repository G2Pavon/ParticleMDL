#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "Settings.hpp"
#include "SettingsJson.hpp"

#include <nlohmann/json.hpp>

using namespace gs;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("validate leaves the defaults untouched")
{
    Settings s;
    CHECK_FALSE(validate(s));
    CHECK(s == Settings{});
}

TEST_CASE("validate leaves the blank settings untouched")
{
    Settings s = blank_settings();
    CHECK_FALSE(validate(s));
}

TEST_CASE("validate replaces non-finite numbers with the defaults")
{
    const Settings defaults;
    Settings s;
    s.duration = kNaN;
    s.lifespan = kInf;
    s.gravity = -kInf;
    s.speed = kNaN;
    s.quad_width = kNaN;
    CHECK(validate(s));
    CHECK(s.duration == defaults.duration);
    CHECK(s.lifespan == defaults.lifespan);
    CHECK(s.gravity == defaults.gravity);
    CHECK(s.speed == defaults.speed);
    CHECK(s.quad_width == defaults.quad_width);
}

TEST_CASE("validate resets a whole vector when any component is not finite")
{
    Settings s;
    s.velocity = {1.0, kNaN, 2.0};
    s.mesh_rotation = {kInf, 0.0, 0.0};
    CHECK(validate(s));
    CHECK(s.velocity == Vec3{});
    CHECK(s.mesh_rotation == Vec3{});
}

TEST_CASE("validate replaces out-of-range enum values with the defaults")
{
    const Settings defaults;
    Settings s;
    s.geometry = static_cast<Geometry>(99);
    s.end_behavior = static_cast<EndBehavior>(-1);
    s.texture.pattern = static_cast<TexPattern>(1000);
    s.texture.color_mode = static_cast<ColorMode>(2);
    s.scale_mode = static_cast<ScaleMode>(2);
    CHECK(validate(s));
    CHECK(s.geometry == defaults.geometry);
    CHECK(s.end_behavior == defaults.end_behavior);
    CHECK(s.texture.pattern == defaults.texture.pattern);
    CHECK(s.texture.color_mode == defaults.texture.color_mode);
    CHECK(s.scale_mode == defaults.scale_mode);
}

TEST_CASE("validate clamps numbers to their hard limits")
{
    Settings s;
    s.duration = 1000;
    s.lifespan = 5;
    s.fps = 1000;
    s.texture.hardness = 0;
    s.scale_start = -1;
    s.scale_end = 1e9;
    s.volume_width = 0;
    s.volume_depth = 1e12;
    s.volume_height = -3;
    s.quad_width = 0.5;
    s.quad_height = 1e6;
    CHECK(validate(s));
    CHECK(s.duration == kMaxDuration);
    CHECK(s.lifespan == 1.0);
    CHECK(s.fps == kMaxFps);
    CHECK(s.texture.hardness == kMinHardness);
    CHECK(s.scale_start == 0.0);
    CHECK(s.scale_end == kMaxScale);
    CHECK(s.volume_width == 1.0);
    CHECK(s.volume_depth == kMaxVolume);
    CHECK(s.volume_height == 1.0);
    CHECK(s.quad_width == 1.0);
    CHECK(s.quad_height == kMaxQuadSize);

    Settings low;
    low.duration = 0;
    low.lifespan = -1;
    low.fps = -5;
    low.texture.hardness = 1e6;
    CHECK(validate(low));
    CHECK(low.duration == kMinDuration);
    CHECK(low.lifespan == kMinLifespan);
    CHECK(low.fps == kMinFps);
    CHECK(low.texture.hardness == kMaxHardness);
}

TEST_CASE("validate keeps values on the limits")
{
    Settings s;
    s.duration = kMaxDuration;
    s.lifespan = kMinLifespan;
    s.fps = kMinFps;
    s.texture.hardness = kMaxHardness;
    CHECK_FALSE(validate(s));
}

TEST_CASE("validate limits the particle count to the bone budget")
{
    struct Case {
        Geometry geometry;
        ScaleMode scale;
        int max;
    };
    // One bone per particle, or one plus four per quad with vertex bones; 127 in all.
    for (const Case c : {Case{Geometry::SingleQuad, ScaleMode::Static, 127},
                         Case{Geometry::VolumetricBox, ScaleMode::Static, 127},
                         Case{Geometry::SingleQuad, ScaleMode::VertexBone, 25},
                         Case{Geometry::CrossedQuads, ScaleMode::VertexBone, 14},
                         Case{Geometry::TriPlanarQuads, ScaleMode::VertexBone, 9},
                         Case{Geometry::VolumetricBox, ScaleMode::VertexBone, 14}}) {
        CAPTURE(enum_id(c.geometry));
        CAPTURE(enum_id(c.scale));
        CHECK(max_particles(c.geometry, c.scale) == c.max);

        Settings s;
        s.geometry = c.geometry;
        s.scale_mode = c.scale;
        s.count = 100000;
        CHECK(validate(s));
        CHECK(s.count == c.max);

        s.count = 0;
        CHECK(validate(s));
        CHECK(s.count == 1);
    }
}

TEST_CASE("validate snaps the texture size to a supported one")
{
    auto snapped = [](int size) {
        Settings s;
        s.texture.size = size;
        validate(s);
        return s.texture.size;
    };
    CHECK(snapped(64) == 64);
    CHECK(snapped(128) == 128);
    CHECK(snapped(256) == 256);
    CHECK(snapped(512) == 512);
    CHECK(snapped(100) == 128);
    CHECK(snapped(96) == 64);     // a tie goes to the smaller size
    CHECK(snapped(384) == 256);
    CHECK(snapped(1000) == 512);
    CHECK(snapped(-5) == 64);
    CHECK(snapped(0) == 64);
}

TEST_CASE("validated is validate on a copy and is idempotent")
{
    Settings bad;
    bad.duration = kNaN;
    bad.count = 100000;
    bad.fps = 0;

    const Settings fixed = validated(bad);
    CHECK(std::isnan(bad.duration));   // the argument was a copy
    CHECK(fixed.count == 127);

    Settings again = fixed;
    CHECK_FALSE(validate(again));
    CHECK(again == fixed);
}

TEST_CASE("texture settings compare equal exactly when they give the same texture")
{
    const TextureSettings base;
    CHECK(base == TextureSettings{});

    auto changed = [&](auto change) {
        TextureSettings t;
        change(t);
        return t != base;
    };
    CHECK(changed([](TextureSettings& t) { t.pattern = TexPattern::HollowRing; }));
    CHECK(changed([](TextureSettings& t) { t.color_mode = ColorMode::Gradient; }));
    CHECK(changed([](TextureSettings& t) { t.core_color.r = 1; }));
    CHECK(changed([](TextureSettings& t) { t.edge_color.b = 9; }));
    CHECK(changed([](TextureSettings& t) { t.size = 128; }));
    CHECK(changed([](TextureSettings& t) { t.hardness = 1.5; }));
    CHECK(changed([](TextureSettings& t) { t.file = "image.png"; }));

    // The physics and the geometry do not change the texture.
    Settings a, b;
    b.gravity = -1;
    b.count = 3;
    b.geometry = Geometry::VolumetricBox;
    CHECK(a.texture == b.texture);
}

TEST_CASE("the settings file stays flat although the texture settings are grouped")
{
    Settings s;
    s.texture.hardness = 2.5;
    s.texture.size = 256;
    const nlohmann::json j = to_json(s);
    CHECK(j.at("hardness") == 2.5);
    CHECK(j.at("texture_size") == 256);
    CHECK_FALSE(j.contains("texture"));
}
