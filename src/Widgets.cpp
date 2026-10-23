#include "Widgets.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gs::ui {

namespace {



template <typename T>
bool reset_button(T& v, T vmin, T vmax, T def)
{
    const bool pressed = ImGui::Button("-");
    if (pressed)
        v = (vmin <= T(0) && T(0) <= vmax) ? T(0) : def;
    ImGui::SetItemTooltip("Clean");
    ImGui::SameLine();
    return pressed;
}

float g_scale = 1.0f;

} // namespace

void set_scale(float scale)
{
    g_scale = scale;
}

float scale()
{
    return g_scale;
}

void group_label(const char* text)
{
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::TextColored(theme::kMuted, "%s", text);
}

bool reset_slider(const char* id, const char* label, int& v, int vmin, int vmax, int def, ImGuiSliderFlags flags)
{
    ImGui::PushID(id);
    bool changed = reset_button(v, vmin, vmax, def);
    ImGui::SetNextItemWidth(field_width());
    changed |= ImGui::SliderInt(label, &v, vmin, vmax, "%d", flags);
    ImGui::PopID();
    return changed;
}

bool reset_slider(const char* id, const char* label, double& v, double vmin, double vmax, double def,
                  const char* format, ImGuiSliderFlags flags)
{
    ImGui::PushID(id);
    bool changed = reset_button(v, vmin, vmax, def);
    ImGui::SetNextItemWidth(field_width());
    changed |= ImGui::SliderScalar(label, ImGuiDataType_Double, &v, &vmin, &vmax, format, flags);
    ImGui::PopID();
    return changed;
}

bool dropdown_item(const char* label, bool current)
{
    bool picked = ImGui::Selectable(label, current);
    if (current)
        ImGui::SetItemDefaultFocus();   // the keyboard starts on the current one
    // The keyboard moved onto it (the mouse does not move the keyboard's item).
    else if (ImGui::IsItemFocused())
        picked = true;
    return picked;
}

bool whole_slider(const char* id, const char* label, double& v, double vmin, double vmax, double def)
{
    bool changed = reset_slider(id, label, v, vmin, vmax, def, "%.0f");
    if (const double whole = std::round(v); whole != v) {
        v = whole;
        changed = true;
    }
    return changed;
}

bool seed_input(const char* id, int& seed, const char* tooltip)
{
    ImGui::PushID(id);
    // The field and the button take half the panel's width.
    const float button = px(60);
    const float row = ImGui::GetContentRegionAvail().x * 0.5f;
    ImGui::SetNextItemWidth(std::max(px(40), row - button - ImGui::GetStyle().ItemSpacing.x));
    bool changed = ImGui::InputInt("##seed", &seed, 0, 0);
    ImGui::SetItemTooltip("%s", tooltip);
    ImGui::SameLine();
    if (ImGui::Button("Seed", ImVec2(button, 0))) {
        seed = static_cast<int>(static_cast<unsigned>(ImGui::GetTime() * 1000.0) & 0x7FFFFFFFu);
        changed = true;
    }
    ImGui::SetItemTooltip("%s", tooltip);
    ImGui::PopID();
    return changed;
}

Locked::Locked(bool locked, const char* reason) : locked_(locked), reason_(reason)
{
    // A group makes the locked controls one item, so a single hover test covers all of them.
    ImGui::BeginGroup();
    ImGui::BeginDisabled(locked_);
}

Locked::~Locked()
{
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (locked_ && reason_ && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", reason_);
}

bool color_edit(const char* label, Rgb& c, ImGuiColorEditFlags flags)
{
    float f[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
    if (!ImGui::ColorEdit3(label, f, flags | ImGuiColorEditFlags_Uint8))
        return false;
    auto byte = [](float x) { return static_cast<std::uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    c = {byte(f[0]), byte(f[1]), byte(f[2])};
    return true;
}

} // namespace gs::ui
