#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <set>
#include <string>

#include "Exporter.hpp"
#include "Presets.hpp"

using namespace gs;

namespace {

// Presets that are allowed one known warning, with a part of its text. A preset
// listed here must really have it, so the entry goes when the preset is fixed.
struct AcceptedWarning {
    std::string_view preset;
    std::string_view text;
};
// None now: Ambient Ash, the last, streaked at each respawn until it folded its quads.
constexpr std::array<AcceptedWarning, 0> kAccepted{};

const AcceptedWarning* accepted_for(std::string_view preset)
{
    const auto it = std::ranges::find(kAccepted, preset, &AcceptedWarning::preset);
    return it != std::end(kAccepted) ? &*it : nullptr;
}

} // namespace

TEST_CASE("preset names are unique and can be found")
{
    std::set<std::string_view> names;
    for (const Preset& p : builtin_presets()) {
        CHECK(names.insert(p.name).second);
        const Preset* found = find_preset(p.name);
        REQUIRE(found != nullptr);
        CHECK(found->name == p.name);
    }
    CHECK(find_preset("No Such Preset") == nullptr);
    CHECK(names.size() >= 11);
}

TEST_CASE("every preset keeps the particle count it asks for")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);
        CHECK(s.count == preset.settings.count);   // no silent cut to the bone budget
        CHECK(s.count <= max_particles(s.geometry, s.scale_mode));
    }
}

TEST_CASE("every preset is inside the limits of validate")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s = preset.settings;
        CHECK_FALSE(validate(s));
        CHECK(s == preset.settings);
    }
}

TEST_CASE("every preset exports without blockers, and warns only where accepted")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);
        const ExportReport report = analyze_export(ParticleSystem(s));
        CHECK(report.blockers.empty());

        if (const AcceptedWarning* accepted = accepted_for(preset.name)) {
            REQUIRE(report.warnings.size() == 1);
            CHECK(report.warnings.front().find(accepted->text) != std::string::npos);
        } else {
            CHECK(report.warnings.empty());
        }
    }
}

TEST_CASE("every preset leaves room in the sequence budget")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);
        const ExportReport report = analyze_export(ParticleSystem(s));
        CHECK(report.sequence_bytes < kMaxSequenceBytes * 95 / 100);   // not on the edge of the 64 KB limit
    }
}
