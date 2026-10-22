#pragma once

#include <vector>

#include <imgui.h>

#include "TextureGenerator.hpp"

namespace gs {

// Owning wrapper for an RGBA8 OpenGL texture with linear filtering and
// clamped edges. Needs a current GL context for its whole lifetime.
class GlTexture {
public:
    GlTexture();
    ~GlTexture();
    GlTexture(GlTexture&& other) noexcept;
    GlTexture& operator=(GlTexture&& other) noexcept;
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;

    void upload(const RgbaImage& img);
    // A texture as the game binds a model texture: every mipmap level given (the first the full size),
    // trilinear filtering, and repeating at its edges.
    void upload_mipmaps(const std::vector<RgbaImage>& levels);
    ImTextureID imgui_id() const;

private:
    unsigned int id_ = 0;
};

} // namespace gs
