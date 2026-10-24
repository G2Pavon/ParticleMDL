#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "BoneLayout.hpp"
#include "Exporter.hpp"
#include "MdlReader.hpp"
#include "SettingsJson.hpp"

using namespace gs;

TEST_CASE("a static layout is one root bone per particle")
{
    const BoneLayout b(3, 0);
    CHECK(b.count() == 3);
    CHECK_FALSE(b.vertex_bones());
    CHECK(b.particle_bone(0) == 0);
    CHECK(b.particle_bone(2) == 2);
    for (int bone = 0; bone < 3; ++bone) {
        CHECK(b.parent(bone) == -1);   // no shared root: each particle's bone is one
        CHECK(b.name(bone) == "p_" + std::to_string(bone));
    }
}

TEST_CASE("with corner bones each particle's corners follow its own bone and hang on it")
{
    const BoneLayout b(2, 4);   // two single quads with vertex bones
    CHECK(b.count() == 2 * 5);
    CHECK(b.vertex_bones());
    CHECK(b.particle_bone(0) == 0);
    CHECK(b.corner_bone(0, 0) == 1);
    CHECK(b.corner_bone(0, 3) == 4);
    CHECK(b.particle_bone(1) == 5);
    CHECK(b.corner_bone(1, 0) == 6);
    CHECK(b.corner_bone(1, 3) == 9);

    CHECK(b.parent(0) == -1);
    CHECK(b.parent(5) == -1);
    for (int c = 0; c < 4; ++c) {
        CHECK(b.parent(b.corner_bone(0, c)) == 0);
        CHECK(b.parent(b.corner_bone(1, c)) == 5);
    }
    CHECK(b.name(0) == "p_0");
    CHECK(b.name(1) == "c_1");
    CHECK(b.name(5) == "p_5");
    CHECK(b.name(9) == "c_9");
}

TEST_CASE("without particle bones every corner bone is a root")
{
    const BoneLayout b(2, 4, false);
    CHECK(b.count() == 8);
    CHECK(b.per_particle() == 4);
    CHECK_FALSE(b.particle_bones());
    CHECK(b.corner_bone(0, 0) == 0);
    CHECK(b.corner_bone(1, 3) == 7);
    for (int bone = 0; bone < 8; ++bone) {
        CHECK(b.parent(bone) == -1);
        CHECK(b.name(bone) == "c_" + std::to_string(bone));
    }
    CHECK(BoneLayout(3, 0, false).particle_bones());   // static particles always have their bone
}

TEST_CASE("a folding layout hangs a fold bone on each particle's bone")
{
    const BoneLayout b = BoneLayout::folding(3);
    CHECK(b.count() == 6);
    CHECK(b.fold_bones());
    CHECK_FALSE(b.vertex_bones());
    CHECK(b.particle_bone(1) == 2);
    CHECK(b.fold_bone(1) == 3);
    CHECK(b.parent(2) == -1);
    CHECK(b.parent(3) == 2);
    CHECK(b.name(2) == "p_2");
    CHECK(b.name(3) == "f_3");
}

TEST_CASE("the layout of a system agrees with bones_per_particle and the bone limit")
{
    for (const Geometry g : {Geometry::SingleQuad, Geometry::CrossedQuads, Geometry::TriPlanarQuads, Geometry::VolumetricBox})
        for (const ScaleMode mode : {ScaleMode::Static, ScaleMode::VertexBone, ScaleMode::Folding}) {
            CAPTURE(enum_id(g));
            CAPTURE(enum_id(mode));
            Settings s;
            s.geometry = g;
            s.scale_mode = mode;
            s.count = kMaxBones;   // as many as the bones allow
            const ParticleSystem sys(s);
            const BoneLayout& b = sys.bones();
            CHECK(b.particles() == sys.num_particles());
            // Folding falls back to Static for geometry that does not fold.
            CHECK(sys.settings().scale_mode == (mode == ScaleMode::Folding && !folds(g) ? ScaleMode::Static : mode));
            CHECK(b.per_particle() == bones_per_particle(g, sys.settings().scale_mode, sys.num_particles()));
            CHECK(b.count() == sys.num_particles() * b.per_particle());
            CHECK(b.count() <= kMaxBones);   // studiomdl refuses 128
            // The last particle's last bone is the last bone.
            const int last = sys.num_particles() - 1;
            CHECK((b.vertex_bones() ? b.corner_bone(last, b.corners_per_particle() - 1)
                   : b.fold_bones() ? b.fold_bone(last)
                                    : b.particle_bone(last)) == b.count() - 1);
        }
}

TEST_CASE("the bones of a compiled model are the layout's, in order")
{
    namespace fs = std::filesystem;
    const fs::path fixtures = fs::path(GS_FIXTURE_DIR) / "real_mdl";
    for (const char* name : {"crossed_static", "vertex_bones", "box_vertex_bones"}) {
        CAPTURE(name);
        const MdlInfo m = read_mdl_file(fixtures / (std::string(name) + ".mdl"));
        const ParticleSystem sys(read_settings_file(fixtures / (std::string(name) + ".json"), Settings{}).settings);
        const BoneLayout& b = sys.bones();
        REQUIRE(static_cast<int>(m.bones.size()) == b.count());
        for (int bone = 0; bone < b.count(); ++bone) {
            CHECK(m.bones[static_cast<std::size_t>(bone)].name == b.name(bone));
            CHECK(m.bones[static_cast<std::size_t>(bone)].parent == b.parent(bone));
        }
    }
}
