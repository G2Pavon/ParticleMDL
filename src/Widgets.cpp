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

bool transport_button(const char* id, TransportIcon icon, const char* tooltip, bool repeat)
{
    const float height = ImGui::GetFrameHeight();
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, repeat);
    const bool pressed = ImGui::Button(id, ImVec2(height * 1.5f, height));
    ImGui::PopItemFlag();
    ImGui::SetItemTooltip("%s", tooltip);

    // The icon, left to right: bars and triangles, centred in the button. The text colour follows
    // the style's alpha, so a locked button fades with its frame.
    enum Part { Bar, Left, Right, Big };
    std::array<Part, 3> parts{};
    std::size_t count = 0;
    switch (icon) {
    case TransportIcon::First: parts = {Bar, Left, Left}; count = 3; break;
    case TransportIcon::StepBack: parts = {Bar, Left}; count = 2; break;
    case TransportIcon::Play: parts = {Big}; count = 1; break;
    case TransportIcon::Pause: parts = {Bar, Bar}; count = 2; break;
    case TransportIcon::StepForward: parts = {Right, Bar}; count = 2; break;
    case TransportIcon::Last: parts = {Right, Right, Bar}; count = 3; break;
    }
    const float s = std::round(height * 0.45f);   // the icon's height
    const float gap = icon == TransportIcon::Pause ? s * 0.25f : s * 0.08f;
    auto width = [&](Part p) { return p == Bar ? s * 0.25f : p == Big ? s * 0.85f : s * 0.6f; };
    float total = gap * static_cast<float>(count - 1);
    for (std::size_t i = 0; i < count; ++i)
        total += width(parts[i]);

    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float top = std::round((lo.y + hi.y - s) * 0.5f), bottom = top + s, middle = (top + bottom) * 0.5f;
    float x = std::round((lo.x + hi.x - total) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    for (std::size_t i = 0; i < count; ++i) {
        const float w = width(parts[i]);
        switch (parts[i]) {
        case Bar: draw->AddRectFilled(ImVec2(x, top), ImVec2(x + w, bottom), color); break;
        case Left: draw->AddTriangleFilled(ImVec2(x + w, top), ImVec2(x, middle), ImVec2(x + w, bottom), color); break;
        case Right:
        case Big: draw->AddTriangleFilled(ImVec2(x, top), ImVec2(x + w, middle), ImVec2(x, bottom), color); break;
        }
        x += w + gap;
    }
    return pressed;
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
