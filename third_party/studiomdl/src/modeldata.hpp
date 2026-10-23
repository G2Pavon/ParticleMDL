/*
    Internal data used by studiomdl which is then transferred/mapped to the real .mdl data
*/
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "format/image/bmp.hpp"
#include "format/mdl.hpp"
#include "utils/filebytes.hpp"
#include "utils/mathlib.hpp"

struct TriangleVert
{
    int vertindex;
    int normindex; // index into normal array
    int s, t;
    float u, v;
};

struct Vertex
{
    int bone_id; // bone transformation index
    Vector3 pos; // original position
};

struct Normal
{
    int skinref;
    int bone_id; // bone transformation index
    Vector3 pos; // original position
};

struct BoneFixUp
{
    Vector3 worldorg; // original world position
    Matrix3x4 matrix;
    Matrix3x4 inv_matrix;
};

struct BoneTable
{
    std::string name;   // bone name for symbolic links. TODO: Check name length limits
    int parent;         // parent bone
    Vector3 pos;        // default pos
    Vector3 posscale;   // pos values scale
    Vector3 rot;        // default pos
    Vector3 rotscale;   // rotation values scale
    int group;          // hitgroup
    Vector3 bmin, bmax; // bounding box
};

struct RenameBone
{
    std::string from;
    std::string to;
};

struct HitBox
{
    std::string name; // bone name
    int bone;
    int group;          // hitgroup
    Vector3 bmin, bmax; // bounding box
};

struct HitGroup
{
    int group;
    std::string name; // bone name
};

struct BoneController
{
    std::string name;
    int bone;
    int type;
    int index;
    float start;
    float end;
};

struct Attachment
{
    std::string bonename;
    int bone;
    Vector3 org;
};

struct Node
{
    std::string name; // before: name[64]
    int parent;
    int mirrored;
};

struct Animation
{
    std::string name; // before: name[64]
    int startframe;
    int endframe;
    std::vector<Node> nodes;
    std::vector<int> boneimap;             // [bone table index] -> node index, -1 if absent
    std::vector<std::vector<Vector3>> pos; // [node, bone table index once relinked][frame]
    std::vector<std::vector<Vector3>> rot;
    std::vector<std::array<std::vector<StudioAnimationValue>, DEGREESOFFREEDOM>> anims; // [bone][axis] compressed values
};

struct Event
{
    int event;
    int frame;
    std::string options;
};

struct Sequence
{
    int motiontype;
    Vector3 linearmovement;

    std::string name; // before: name[64]
    int flags;
    float fps;
    int numframes;

    int activity;
    int actweight;

    int frameoffset; // used to adjust frame numbers

    std::vector<Event> events;

    std::vector<Animation> anims;
    float blendtype[MAXSEQUENCEBLEND];
    float blendstart[MAXSEQUENCEBLEND];
    float blendend[MAXSEQUENCEBLEND];

    int seqgroup;
    int animindex;

    Vector3 bmin;
    Vector3 bmax;
    int entrynode;
    int exitnode;
    int nodeflags;
};

struct RGB
{
    std::uint8_t r, g, b;
};

// FIXME: what about texture overrides inline with loading models
struct Texture
{
    std::string name;
    int flags;
    int srcwidth;
    int srcheight;
    FileBytes image;                       // source BMP file; its rows are copied into the .mdl by the writer
    BmpLayout layout;                      // where its pixel rows are
    std::array<std::uint8_t, 768> palette; // RGB palette, gamma-corrected by resize_texture
    float max_s;
    float min_s;
    float max_t;
    float min_t;
    int skintop;
    int skinleft;
    int skinwidth;
    int skinheight;
    int size; // pixels + palette as written to the .mdl
    int parent;
};

struct Mesh
{
    int numtris;
    std::vector<std::array<TriangleVert, 3>> triangles;

    int skinref;
    int numnorms;
};

struct Bone
{
    Vector3 pos;
    Vector3 rot;
};

struct Model
{
    std::string name;

    std::vector<Node> nodes;
    std::vector<Bone> skeleton;
    int boneref[MAXSTUDIOSRCBONES];  // is local bone (or child) referenced with a vertex
    int bonemap[MAXSTUDIOSRCBONES];  // local bone to world bone mapping
    int boneimap[MAXSTUDIOSRCBONES]; // world bone to local bone mapping

    std::vector<Vertex> verts;

    std::vector<Normal> normals;

    int nummesh;
    std::array<std::unique_ptr<Mesh>, MAXSTUDIOMESHES> pmeshes;
};

struct BodyPart
{
    std::string name;
    int num_submodels;
    int base;
};

// Writes a texture as stored in the .mdl: skinwidth x skinheight pixels taken
// from the source BMP with wrap-around from (skinleft, skintop), then the
// palette; `size` bytes in all. The skin is the full source size, so each row
// is a source row rotated left.
inline void write_skin(const Texture &texture, std::uint8_t *dest)
{
    const int width = texture.srcwidth;
    const int height = texture.srcheight;
    const auto wrap = [](long long value, int size)
    { return static_cast<int>(((value % size) + size) % size); };
    const int first_column = wrap(texture.skinleft, width);
    int t = wrap(static_cast<long long>(height) - texture.skinheight - texture.skintop, height);
    const std::span<const std::uint8_t> file = texture.image.bytes();
    for (int i = 0; i < texture.skinheight; i++, t = (t + 1 == height ? 0 : t + 1))
    {
        const std::uint8_t *row = texture.layout.row(file, height, t);
        dest = std::copy(row + first_column, row + width, dest);
        dest = std::copy(row, row + first_column, dest);
    }
    std::copy(texture.palette.begin(), texture.palette.end(), dest);
}
