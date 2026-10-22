#include "PreviewRenderer.hpp"

#include <GLFW/glfw3.h>   // pulls in the system GL header (blend state)

#include "EngineLook.hpp"
#include "TextureGenerator.hpp"
#include "Widgets.hpp"

namespace gs {

namespace {

ImVec2 to_imvec(Point2 p)
{
    return {p.x, p.y};
}

ImU32 to_imcol(Rgba8 c)
{
    return IM_COL32(c.r, c.g, c.b, c.a);
}

// GoldSrc adds an additive model texture to the picture (an Additive entity at renderamt 255: colour
// and alpha both 255, glBlendFunc GL_SRC_ALPHA, GL_ONE, which is the texture added as it is).
void set_additive_blend(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFunc(GL_ONE, GL_ONE);
}

} // namespace

void PreviewRenderer::update_textures(const TextureSettings& s)
{
    set_texture(export_texture(s));
}

void PreviewRenderer::set_texture(const IndexedImage& image)
{
    swatch_.upload(to_rgba(expand(image)));
    game_.upload_mipmaps(engine_mipmaps(to_rgba(expand(with_texture_gamma(image)))));
}

void PreviewRenderer::set_sky(const std::array<RgbaImage, 6>* sides)
{
    has_sky_ = sides != nullptr;
    if (sides)
        for (std::size_t i = 0; i < sky_.size(); ++i)
            sky_[i].upload((*sides)[i]);
}

void PreviewRenderer::draw(ImDrawList* dl, ImVec2 origin, ImVec2 size, Rgb background, const Scene& scene) const
{
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, end, true);
    dl->AddRectFilled(origin, end, IM_COL32(background.r, background.g, background.b, 255));

    // The sky, opaque, behind everything (the engine draws it without blending too).
    if (has_sky_)
        for (const SkyQuad& q : scene.sky)
            dl->AddImageQuad(sky_[static_cast<std::size_t>(q.side)].imgui_id(), to_imvec(q.pts[0]), to_imvec(q.pts[1]),
                             to_imvec(q.pts[2]), to_imvec(q.pts[3]), to_imvec(q.uvs[0]), to_imvec(q.uvs[1]),
                             to_imvec(q.uvs[2]), to_imvec(q.uvs[3]));

    for (const SceneLine& l : scene.lines)
        dl->AddLine(to_imvec(l.a), to_imvec(l.b), to_imcol(l.color), ui::px(l.thickness));

    // Additive: the colour is added to the picture.
    dl->AddCallback(set_additive_blend, nullptr);
    for (const SceneFace& f : scene.faces)
        dl->AddImageQuad(game_.imgui_id(), to_imvec(f.pts[0]), to_imvec(f.pts[1]), to_imvec(f.pts[2]),
                         to_imvec(f.pts[3]), to_imvec(f.uvs[0]), to_imvec(f.uvs[1]), to_imvec(f.uvs[2]),
                         to_imvec(f.uvs[3]));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    dl->PopClipRect();
}

} // namespace gs
