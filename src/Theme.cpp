#include "Theme.hpp"

namespace gs::theme {

void apply(ImGuiStyle& style)
{
    // Metrics: a little more air than the default, small corner radii, thin borders.
    style.WindowPadding = {10, 10};
    style.FramePadding = {7, 4};
    style.ItemSpacing = {8, 5};
    style.ItemInnerSpacing = {6, 4};
    style.IndentSpacing = 16;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;
    style.DisabledAlpha = kDisabledAlpha;

    style.WindowRounding = 4;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 3;
    style.TabRounding = 3;

    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;

    ImVec4* c = style.Colors;
    auto rgba = [](float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); };

    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = rgba(0.45f, 0.46f, 0.49f);
    c[ImGuiCol_WindowBg] = rgba(0.15f, 0.15f, 0.16f);
    c[ImGuiCol_ChildBg] = rgba(0.13f, 0.13f, 0.14f);
    c[ImGuiCol_PopupBg] = rgba(0.12f, 0.12f, 0.13f, 0.98f);
    c[ImGuiCol_Border] = rgba(0.07f, 0.07f, 0.08f);
    c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = rgba(0.20f, 0.20f, 0.22f);
    c[ImGuiCol_FrameBgHovered] = rgba(0.26f, 0.26f, 0.29f);
    c[ImGuiCol_FrameBgActive] = rgba(0.30f, 0.30f, 0.34f);

    c[ImGuiCol_TitleBg] = rgba(0.11f, 0.11f, 0.12f);
    c[ImGuiCol_TitleBgActive] = rgba(0.13f, 0.13f, 0.14f);
    c[ImGuiCol_TitleBgCollapsed] = rgba(0.11f, 0.11f, 0.12f, 0.75f);
    c[ImGuiCol_MenuBarBg] = rgba(0.10f, 0.10f, 0.11f);

    c[ImGuiCol_ScrollbarBg] = rgba(0.11f, 0.11f, 0.12f);
    c[ImGuiCol_ScrollbarGrab] = rgba(0.27f, 0.27f, 0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(0.35f, 0.35f, 0.39f);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(0.42f, 0.42f, 0.46f);

    c[ImGuiCol_CheckMark] = kAccent;
    // Darker than the accent, so the white values on the grab stay readable (about 4.6:1).
    c[ImGuiCol_SliderGrab] = rgba(0.74f, 0.38f, 0.08f);
    c[ImGuiCol_SliderGrabActive] = rgba(0.82f, 0.46f, 0.14f);

    c[ImGuiCol_Button] = rgba(0.23f, 0.23f, 0.26f);
    c[ImGuiCol_ButtonHovered] = rgba(0.30f, 0.30f, 0.34f);
    c[ImGuiCol_ButtonActive] = rgba(0.36f, 0.36f, 0.40f);

    c[ImGuiCol_Header] = rgba(0.98f, 0.55f, 0.15f, 0.22f);      // selectables, selected combo items
    c[ImGuiCol_HeaderHovered] = rgba(0.98f, 0.55f, 0.15f, 0.32f);
    c[ImGuiCol_HeaderActive] = rgba(0.98f, 0.55f, 0.15f, 0.42f);

    c[ImGuiCol_Separator] = rgba(0.22f, 0.22f, 0.24f);
    c[ImGuiCol_SeparatorHovered] = kAccentHover;
    c[ImGuiCol_SeparatorActive] = kAccent;

    c[ImGuiCol_ResizeGrip] = rgba(0.25f, 0.25f, 0.28f, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = kAccentHover;
    c[ImGuiCol_ResizeGripActive] = kAccent;

    c[ImGuiCol_Tab] = rgba(0.16f, 0.16f, 0.17f);
    c[ImGuiCol_TabHovered] = rgba(0.98f, 0.55f, 0.15f, 0.55f);
    c[ImGuiCol_TabActive] = rgba(0.22f, 0.22f, 0.24f);
    c[ImGuiCol_TabUnfocused] = rgba(0.13f, 0.13f, 0.14f);
    c[ImGuiCol_TabUnfocusedActive] = rgba(0.18f, 0.18f, 0.20f);

    c[ImGuiCol_PlotLines] = kMuted;
    c[ImGuiCol_PlotLinesHovered] = kAccent;
    // Filled portions (the sequence memory bar) use the same darker orange as the slider grabs.
    c[ImGuiCol_PlotHistogram] = rgba(0.74f, 0.38f, 0.08f);
    c[ImGuiCol_PlotHistogramHovered] = rgba(0.82f, 0.46f, 0.14f);
    c[ImGuiCol_TableHeaderBg] = rgba(0.17f, 0.17f, 0.18f);
    c[ImGuiCol_TableBorderStrong] = rgba(0.10f, 0.10f, 0.11f);
    c[ImGuiCol_TableBorderLight] = rgba(0.16f, 0.16f, 0.17f);
    c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgba(1, 1, 1, 0.02f);

    c[ImGuiCol_TextSelectedBg] = rgba(0.98f, 0.55f, 0.15f, 0.35f);
    c[ImGuiCol_DragDropTarget] = kAccentHover;
    c[ImGuiCol_NavHighlight] = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = rgba(1, 1, 1, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = rgba(0.8f, 0.8f, 0.8f, 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = rgba(0.05f, 0.05f, 0.06f, 0.6f);
}

} // namespace gs::theme
