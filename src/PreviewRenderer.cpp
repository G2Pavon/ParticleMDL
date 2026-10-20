#include "PreviewRenderer.hpp"

#include <GLFW/glfw3.h>   // pulls in the system GL header (blend state)

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

// GoldSrc draws additive model textures with glBlendFunc(GL_ONE, GL_ONE).
void set_additive_blend(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFunc(GL_ONE, GL_ONE);
}

} // namespace

void PreviewRenderer::update_textures(const TextureSettings& s)
{
    set_texture(export_texture(s), s.blend);
}

void PreviewRenderer::set_texture(const IndexedImage& image, BlendMode blend)
{
    blend_ = blend;
    game_.upload(expand_rgba(image));   // the transparent colour of a masked texture has alpha 0
}

void PreviewRenderer::draw(ImDrawList* dl, ImVec2 origin, ImVec2 size, Rgb background, const Scene& scene) const
{
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, end, true);
    dl->AddRectFilled(origin, end, IM_COL32(background.r, background.g, background.b, 255));

    for (const SceneLine& l : scene.lines)
        dl->AddLine(to_imvec(l.a), to_imvec(l.b), to_imcol(l.color), ui::px(l.thickness));

    // Additive adds the colour to the picture; the others are drawn over it, the transparent pixels of a
    // masked texture having alpha 0.
    if (blend_ == BlendMode::Additive)
        dl->AddCallback(set_additive_blend, nullptr);
    for (const SceneFace& f : scene.faces)
        dl->AddImageQuad(game_.imgui_id(), to_imvec(f.pts[0]), to_imvec(f.pts[1]), to_imvec(f.pts[2]),
                         to_imvec(f.pts[3]), to_imvec(f.uvs[0]), to_imvec(f.uvs[1]), to_imvec(f.uvs[2]),
                         to_imvec(f.uvs[3]));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    // Over the particles, so the emitter is never hidden by them.
    for (const SceneLine& l : scene.overlay)
        dl->AddLine(to_imvec(l.a), to_imvec(l.b), to_imcol(l.color), ui::px(l.thickness));

    dl->PopClipRect();
}

} // namespace gs
