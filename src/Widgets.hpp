#pragma once

#include <array>
#include <cstddef>
#include <charconv>

#include <imgui.h>

#include "Enums.hpp"
#include "Settings.hpp"

// Editor widgets on top of Dear ImGui. Each returns true when the value changed.
namespace gs::ui {

// UI scale for high-DPI screens: every fixed pixel size goes through px().
void set_scale(float scale);
float scale();
inline float px(float v)
{
    return v * scale();
}

// Width of value fields; the rest of the row holds the label.
inline float field_width()
{
    return px(-140.0f);
}

void group_label(const char* text);

// "-" reset button (to 0 when the range contains it, else to `def`) followed by
// a slider. `id` keeps rows with equal labels apart.
bool reset_slider(const char* id, const char* label, int& v, int vmin, int vmax, int def,
                  ImGuiSliderFlags flags = 0);
bool reset_slider(const char* id, const char* label, double& v, double vmin, double vmax, double def,
                  const char* format = "%.6g", ImGuiSliderFlags flags = 0);

// A double setting that only takes whole numbers (see for_each_whole_value): the slider steps by 1 and
// a typed value is rounded.
bool whole_slider(const char* id, const char* label, double& v, double vmin, double vmax, double def);

// A random seed: typed in, or a new one from the "Seed" button beside it, across the panel with no
// label (the tooltip says which seed it is). `id` keeps two of them apart.
bool seed_input(const char* id, int& seed, const char* tooltip);

// Edits a 0..255 color. Does not set the item width.
bool color_edit(const char* label, Rgb& c, ImGuiColorEditFlags flags = 0);

// Labels come from the enum's table.
template <typename E>
bool enum_combo(const char* label, E& value)
{
    int idx = static_cast<int>(value);
    constexpr auto getter = [](void*, int i) { return enum_names<E>()[static_cast<std::size_t>(i)].label; };
    ImGui::SetNextItemWidth(field_width());
    if (!ImGui::Combo(label, &idx, getter, nullptr, static_cast<int>(enum_names<E>().size())))
        return false;
    value = static_cast<E>(idx);
    return true;
}

// Formats into a stack buffer: no allocation per frame.
template <std::size_t N>
bool int_combo(const char* label, int& value, const std::array<int, N>& values)
{
    auto text = [](int v, std::array<char, 16>& buf) {
        *std::to_chars(buf.data(), buf.data() + buf.size() - 1, v).ptr = '\0';
        return buf.data();
    };
    std::array<char, 16> buf{};
    bool changed = false;
    ImGui::SetNextItemWidth(field_width());
    if (ImGui::BeginCombo(label, text(value, buf))) {
        for (int v : values) {
            if (ImGui::Selectable(text(v, buf), v == value)) {
                value = v;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace gs::ui
