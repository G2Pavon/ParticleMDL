#pragma once

#include <imgui.h>

#include "GlTexture.hpp"
#include "PreviewScene.hpp"
#include "Settings.hpp"

namespace gs {

// Owns the preview textures and draws a Scene into an ImGui draw list.
class PreviewRenderer {
public:
    static constexpr int kSwatchSize = 200;

    // Rebuilds the texture from the current settings.
    void update_textures(const TextureSettings& s);
    // Shows a texture that was made elsewhere (see TextureJob), drawn the way `blend` says.
    void set_texture(const IndexedImage& image, BlendMode blend);
    // The exported texture, palettized exactly as written to the BMP.
    ImTextureID swatch() const { return game_.imgui_id(); }

    // Blends the faces additively, as GoldSrc draws "$texrendermode additive"
    // (full-bright; the game also applies map lighting).
    void draw(ImDrawList* dl, ImVec2 origin, ImVec2 size, Rgb background, const Scene& scene) const;

private:
    GlTexture game_;   // export texture: color premultiplied by intensity, palettized
    BlendMode blend_ = BlendMode::Additive;
};

} // namespace gs
