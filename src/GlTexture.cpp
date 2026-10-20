#include "GlTexture.hpp"

#include <cstdint>
#include <utility>

#include <GLFW/glfw3.h>   // pulls in the system GL header

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace gs {

GlTexture::GlTexture()
{
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GlTexture::~GlTexture()
{
    if (id_)
        glDeleteTextures(1, &id_);
}

GlTexture::GlTexture(GlTexture&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

GlTexture& GlTexture::operator=(GlTexture&& other) noexcept
{
    if (this != &other) {
        if (id_)
            glDeleteTextures(1, &id_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void GlTexture::upload(const RgbaImage& img)
{
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
}

ImTextureID GlTexture::imgui_id() const
{
    return static_cast<ImTextureID>(static_cast<std::intptr_t>(id_));
}

} // namespace gs
