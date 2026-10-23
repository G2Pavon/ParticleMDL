#pragma once

#include <imgui.h>

namespace gs::theme {

// Palette: neutral charcoal panels with one warm accent, in the spirit of
// modern DCC and game-engine editors.
inline constexpr ImVec4 kAccent{0.98f, 0.55f, 0.15f, 1.0f};        // headings, primary controls
inline constexpr ImVec4 kAccentHover{1.00f, 0.66f, 0.28f, 1.0f};
inline constexpr ImVec4 kAccentActive{0.86f, 0.46f, 0.10f, 1.0f};
inline constexpr ImVec4 kText{0.90f, 0.90f, 0.91f, 1.0f};
inline constexpr ImVec4 kMuted{0.58f, 0.59f, 0.62f, 1.0f};         // group labels, hints
inline constexpr ImVec4 kSuccess{0.45f, 0.84f, 0.50f, 1.0f};
inline constexpr ImVec4 kWarning{0.98f, 0.80f, 0.28f, 1.0f};
inline constexpr ImVec4 kError{0.96f, 0.40f, 0.40f, 1.0f};
inline constexpr ImVec4 kLink{0.45f, 0.72f, 0.98f, 1.0f};          // file-type highlight
// A locked control (ui::Locked) is drawn at this share of its opacity, fading it into the panel.
inline constexpr float kDisabledAlpha = 0.40f;

// Applies the palette and metrics to Dear ImGui's style. Sizes are unscaled:
// call ImGuiStyle::ScaleAllSizes afterwards for high-DPI screens.
void apply(ImGuiStyle& style);

} // namespace gs::theme
