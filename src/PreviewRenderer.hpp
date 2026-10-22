#pragma once

#include <array>

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
    // Shows a texture that was made elsewhere (see TextureJob).
    void set_texture(const IndexedImage& image);
    // The six sides of the sky the scene's sky pieces show (kSkySides order), or none.
    void set_sky(const std::array<RgbaImage, 6>* sides);
    bool has_sky() const noexcept { return has_sky_; }
    // The exported texture, palettized exactly as written to the BMP.
    ImTextureID swatch() const { return swatch_.imgui_id(); }

    // Blends the faces additively, as GoldSrc draws "$texrendermode additive"
    // (full-bright; the game also applies map lighting).
    void draw(ImDrawList* dl, ImVec2 origin, ImVec2 size, Rgb background, const Scene& scene) const;

private:
    GlTexture swatch_;   // the exported texture, palettized exactly as written to the BMP
    GlTexture game_;     // the same as the game loads it: texture gamma, mipmaps, repeating
    std::array<GlTexture, 6> sky_;
    bool has_sky_ = false;
};

} // namespace gs
