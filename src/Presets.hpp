#pragma once

#include <span>
#include <string_view>

#include "Settings.hpp"

namespace gs {

struct Preset {
    std::string_view name;
    Settings settings;   // model_name is not part of a preset
};

std::span<const Preset> builtin_presets();
const Preset* find_preset(std::string_view name);

// Takes every field from the preset except the model name, then clamps the
// particle count to the bone budget.
void apply_preset(Settings& s, const Preset& preset);

} // namespace gs
