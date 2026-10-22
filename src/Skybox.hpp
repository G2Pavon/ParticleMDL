#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "MathUtil.hpp"
#include "TextureGenerator.hpp"

namespace gs {

// The skies of a game, found and drawn as GoldSrc (and Xash3D) do. A sky "desert" is six images,
// gfx/env/desert + rt, bk, lf, ft, up, dn, as .tga (or .bmp), named as Half-Life does ("desertrt")
// or Quake does ("desert_rt").

inline constexpr std::array<std::string_view, 6> kSkySides = {"rt", "bk", "lf", "ft", "up", "dn"};

// The folders whose gfx/env holds the game's skies, first found first: the mod folder's _addon and
// _hd, itself and its _downloads, then the same of the game it falls back to (liblist.gam's
// fallback_dir, else valve). Only those that exist.
std::vector<std::filesystem::path> sky_search_folders(const std::filesystem::path& game);

// The skies with all six sides, by name, sorted without regard to case. File names are matched
// without regard to case: the game's are often mixed case, and Linux tells them apart.
std::vector<std::string> list_skyboxes(const std::filesystem::path& game);

// The six side images of a sky, in kSkySides order. Throws std::runtime_error when a side is missing
// or cannot be read.
std::array<RgbaImage, 6> load_skybox(const std::filesystem::path& game, std::string_view name);

// The sky as the engine puts it around the camera. A face is +X, -X, +Y, -Y, +Z or -Z (0 to 5) of the
// game's world, which the preview shares for an effect placed at yaw 0; (s, t) runs over it in
// [-1, 1]. `side` is the image (index into kSkySides) the face shows; `uv` its texture coordinates.
struct SkyPoint {
    Vec3 dir;
    Vec2 uv;
};
int sky_face_side(int face) noexcept;
SkyPoint sky_point(int face, double s, double t) noexcept;

} // namespace gs
