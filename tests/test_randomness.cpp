#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

Settings base()
{
    Settings s;
    s.count = 40;
    s.geometry = Geometry::SingleQuad;
    s.scale_mode = ScaleMode::Static;
    return s;
}

bool same_particles(const ParticleSystem& a, const ParticleSystem& b)
{
    if (a.particles().size() != b.particles().size())
        return false;
    for (std::size_t i = 0; i < a.particles().size(); ++i)
        if (!(a.particles()[i].spawn == b.particles()[i].spawn) || !(a.particles()[i].velocity == b.particles()[i].velocity))
            return false;
    return true;
}

} // namespace

TEST_CASE("the seed 0 is the original sequence and another seed gives another effect")
{
    const ParticleSystem zero(base());
    Settings s = base();
    s.seed = 0;
    CHECK(same_particles(zero, ParticleSystem(s)));

    s.seed = 1;
    const ParticleSystem one(s);
    s.seed = 2;
    const ParticleSystem two(s);
    CHECK_FALSE(same_particles(zero, one));
    CHECK_FALSE(same_particles(one, two));

    s.seed = 1;
    CHECK(same_particles(one, ParticleSystem(s)));   // and the same seed is the same effect
    s.seed = -5;
    CHECK(same_particles(ParticleSystem(s), ParticleSystem(s)));
    CHECK_FALSE(same_particles(zero, ParticleSystem(s)));
}

TEST_CASE("a particle does not depend on how many there are when the seed is the same")
{
    Settings few = base(), many = base();
    few.count = 5;
    many.count = 30;
    few.seed = many.seed = 9;
    const ParticleSystem a(few), b(many);
    for (std::size_t i = 0; i < a.particles().size(); ++i)
        CHECK(a.particles()[i].velocity == b.particles()[i].velocity);
}

TEST_CASE("without variance every particle has the settings' lifespan and size")
{
    Settings s = base();
    s.lifespan = 0.37;
    const ParticleSystem lived(s);
    for (const Particle& p : lived.particles()) {
        CHECK(p.lifespan == 0.37);
        CHECK(p.size == 1.0);
    }
}

TEST_CASE("lifespan variance shortens lifespans at random and never lengthens them")
{
    Settings s = base();
    s.lifespan = 0.8;
    s.lifespan_variance = 0.5;
    const ParticleSystem sys(s);
    double shortest = 1, longest = 0;
    for (const Particle& p : sys.particles()) {
        CHECK(p.lifespan <= 0.8);
        CHECK(p.lifespan >= 0.8 * 0.5 - 1e-12);
        shortest = std::min(shortest, p.lifespan);
        longest = std::max(longest, p.lifespan);
    }
    CHECK(longest - shortest > 0.1);   // they really differ

    // A particle is dead when its own lifespan is over.
    const Particle& p = sys.particles().front();
    const double end = p.lifespan * sys.loop_duration();
    // the particle is born at phase_offset; look at the time since it was born
    const double born = p.phase_offset * sys.loop_duration();
    // the particle's own time is the loop time plus where it is born in the loop
    const double t_alive = floor_mod(end * 0.9 - born, sys.loop_duration());
    const double t_dead = floor_mod(end * 1.1 - born, sys.loop_duration());
    CHECK(sys.life_state(p, t_alive).phase == LifeState::Phase::Alive);
    CHECK(sys.life_state(p, t_dead).past_lifespan());
}

TEST_CASE("size variance scales quads at random around their size")
{
    Settings s = base();
    s.size_variance = 0.4;
    const ParticleSystem sys(s);
    double smallest = 9, largest = 0;
    for (const Particle& p : sys.particles()) {
        CHECK(p.size >= 0.6 - 1e-12);
        CHECK(p.size <= 1.4 + 1e-12);
        smallest = std::min(smallest, p.size);
        largest = std::max(largest, p.size);
        // A static quad's pose carries the size; it is drawn and boxed with it.
        CHECK(sys.pose_at(p, 0.5).scale == doctest::Approx(p.size));
    }
    CHECK(largest - smallest > 0.3);
}

TEST_CASE("with vertex bones the size multiplies the scale over the life")
{
    Settings s = base();
    s.scale_mode = ScaleMode::VertexBone;
    s.scale_start = 2.0;
    s.scale_end = 1.0;
    s.count = 20;
    s.size_variance = 0.5;
    s.lifespan = 1.0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        const double t = p.phase_offset >= 1.0 ? 0.0 : 0.0;   // each particle at its own time 0.1 of the loop
        const double loop_time = std::fmod(sys.loop_duration() * (1.0 - p.phase_offset) + 0.1 * sys.loop_duration() + t, sys.loop_duration());
        const LifeState st = sys.life_state(p, loop_time);
        if (!st.moved_by_motion())
            continue;
        CHECK(sys.pose_at(p, loop_time).scale ==
              doctest::Approx(sys.scale_factor_at(p, st.eval_time, st.past_lifespan()) * p.size));
    }
}

TEST_CASE("the mesh of a static quad is drawn at the size of its particle")
{
    Settings s = base();
    s.count = 6;
    s.quad_width = 10;
    s.quad_height = 10;
    s.size_variance = 0.5;
    const ParticleSystem sys(s);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "particlemdl_size_mesh";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    export_model(sys, dir);
    std::ifstream in(dir / "custom_particles_ref.smd");
    std::string line;
    std::vector<double> extent(7, 0.0);   // largest |coordinate| of each bone's vertices
    bool triangles = false;
    while (std::getline(in, line)) {
        if (line == "triangles") {
            triangles = true;
            continue;
        }
        int bone;
        double x, y, z;
        if (triangles && std::sscanf(line.c_str(), " %d %lf %lf %lf", &bone, &x, &y, &z) == 4)
            extent[static_cast<std::size_t>(bone)] = std::max({extent[static_cast<std::size_t>(bone)], std::abs(x), std::abs(z)});
    }
    std::filesystem::remove_all(dir);
    for (std::size_t i = 0; i < sys.particles().size(); ++i)
        CHECK(extent[i + 1] == doctest::Approx(5.0 * sys.particles()[i].size).epsilon(1e-5));
}

TEST_CASE("the bounding box and the hiding height make room for the largest particle")
{
    Settings s = base();
    s.size_variance = 0.8;
    s.quad_width = 20;
    s.quad_height = 20;
    s.end_behavior = EndBehavior::HideUnderground;
    const ParticleSystem sys(s);
    CHECK(sys.hide_z() <= sys.min_z_raw() * sys.volume_scale().z - 20.0 * 1.8 + 1e-9);

    const BakedPoses baked = bake(sys);
    const ParticleSystem::Bounds box = animation_bounds(sys, baked);
    for (int f = 0; f < baked.stored_frames(); ++f)
        for (int i = 0; i < baked.particles(); ++i)
            CHECK(baked.at(f, i).pos.z >= box.min.z);
}

TEST_CASE("without drag the motion is the plain ballistic one")
{
    Settings s = base();
    s.gravity = -100;
    const ParticleSystem sys(s);
    const Particle& p = sys.particles().front();
    const double t = 0.5;
    const Vec3 got = sys.position_at(p, t);
    const Vec3 scale = sys.volume_scale();
    CHECK(got.z == doctest::Approx((p.spawn.z + p.velocity.z * t + 0.5 * -100.0 * t * t) * scale.z));
}

TEST_CASE("a tiny drag is the same as none")
{
    Settings none = base(), tiny = base();
    tiny.drag = 1e-7;
    const ParticleSystem a(none), b(tiny);
    for (const double t : {0.1, 0.8, 2.0}) {
        const Vec3 p = a.position_at(a.particles()[3], t), q = b.position_at(b.particles()[3], t);
        CHECK(q.x == doctest::Approx(p.x).epsilon(1e-4));
        CHECK(q.y == doctest::Approx(p.y).epsilon(1e-4));
        CHECK(q.z == doctest::Approx(p.z).epsilon(1e-4));
    }
}

TEST_CASE("drag slows a particle towards its stopping point")
{
    Settings s = base();
    s.gravity = 0;
    s.turbulence = 0;
    s.speed = 100;
    s.spread = 0;
    s.burst = 0;
    s.spawn_radius = 0;
    s.drag = 2.0;
    s.volume_width = 100000;   // keep the fit from rescaling what is measured
    const ParticleSystem sys(s);
    const Particle& p = sys.particles().front();
    const double v0 = std::hypot(p.velocity.x, p.velocity.y);
    // The distance covered tends to v0 / k.
    for (const double t : {0.5, 1.0, 2.0}) {
        const double expected = v0 / 2.0 * (1.0 - std::exp(-2.0 * t));
        const Vec3 at = sys.position_at(p, t);
        const Vec3 scale = sys.volume_scale();
        CHECK(std::hypot(at.x / scale.x, at.y / scale.y) == doctest::Approx(expected).epsilon(1e-6));
    }
}

TEST_CASE("drag with gravity reaches a terminal speed of gravity over drag")
{
    Settings s = base();
    s.speed = 0;
    s.burst = 0;
    s.spread = 0;
    s.spawn_radius = 0;
    s.turbulence = 0;
    s.gravity = -300;
    s.drag = 3.0;
    const ParticleSystem sys(s);
    const Particle& p = sys.particles().front();
    const double scale = sys.volume_scale().z;
    const double dt = 1e-4;
    const double late = 5.0;   // far beyond 1/k
    const double speed = (sys.position_at(p, late + dt).z - sys.position_at(p, late).z) / dt / scale;
    CHECK(speed == doctest::Approx(-300.0 / 3.0).epsilon(1e-4));
}

TEST_CASE("the new settings are saved, validated and absent from old files")
{
    Settings s;
    s.seed = 77;
    s.lifespan_variance = 0.3;
    s.size_variance = 0.2;
    s.drag = 1.5;
    const SettingsLoad back = settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{});
    CHECK(back.settings.seed == 77);
    CHECK(back.settings.lifespan_variance == 0.3);
    CHECK(back.settings.size_variance == 0.2);
    CHECK(back.settings.drag == 1.5);

    nlohmann::json old = to_json(Settings{});
    for (const char* key : {"seed", "lifespan_variance", "size_variance", "drag"})
        old.erase(key);
    const Settings loaded = settings_from_json(old, Settings{}).settings;
    CHECK(loaded.seed == 0);
    CHECK(loaded.drag == 0.0);

    Settings wild;
    wild.lifespan_variance = 5;
    wild.size_variance = -1;
    wild.drag = 99;
    CHECK(validate(wild));
    CHECK(wild.lifespan_variance == kMaxVariance);
    CHECK(wild.size_variance == 0.0);
    CHECK(wild.drag == kMaxDrag);
}

TEST_CASE("variance keeps the effect inside the limits and free of drawn jumps")
{
    Settings s = base();
    s.scale_mode = ScaleMode::VertexBone;
    s.count = 25;
    s.lifespan_variance = 0.6;
    s.size_variance = 0.5;
    s.drag = 1.0;
    s.seed = 31;
    const ParticleSystem sys(s);
    CHECK(analyze_export(sys).blockers.empty());

    const BakedPoses baked = bake(sys);
    double natural = 0;
    for (int i = 0; i < sys.num_particles(); ++i)
        for (int f = 0; f < sys.loop_steps(); ++f) {
            const double t0 = sys.frame_time(f), t1 = sys.loop_duration() * (f + 1) / sys.loop_steps();
            const Particle& p = sys.particles()[static_cast<std::size_t>(i)];
            if (sys.generation(p, t0) != sys.generation(p, t1) ||
                sys.life_state(p, t0).past_lifespan() != sys.life_state(p, t1).past_lifespan())
                continue;
            const Vec3 d = sys.pose_at(p, t1).pos - sys.pose_at(p, t0).pos;
            natural = std::max(natural, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
        }
    for (int i = 0; i < baked.particles(); ++i)
        for (int f = 0; f < baked.steps(); ++f) {
            const ParticlePose& a = baked.at(f, i);
            const ParticlePose& b = baked.at(f + 1, i);
            if (a.scale > 0 || b.scale > 0) {
                const Vec3 d = b.pos - a.pos;
                CHECK(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) <= natural * 1.5 + 1e-6);
            }
        }
}

TEST_CASE("the drag formula agrees with stepping the equation of motion, for small and large drags")
{
    for (const double drag : {1e-7, 1e-3, 0.02, 0.049 / 3.0, 0.5, 3.0}) {
        CAPTURE(drag);
        Settings s = base();
        s.gravity = -200;
        s.speed = 0;
        s.spread = 0;
        s.burst = 150;
        s.spawn_radius = 0;
        s.turbulence = 0;
        s.drag = drag;
        s.duration = 3.0;
        const ParticleSystem sys(s);
        const Particle& p = sys.particles().front();
        const double scale = sys.volume_scale().z;

        // dv/dt = g - k v, stepped with a small step (semi-implicit, plenty exact at 1e-5 s).
        double z = p.spawn.z, v = p.velocity.z;
        const double dt = 1e-5;
        double t = 0;
        for (const double until : {0.2, 1.0, 2.9}) {
            while (t < until) {
                v += (-200.0 - drag * v) * dt;
                z += v * dt;
                t += dt;
            }
            CHECK(sys.position_at(p, t).z / scale == doctest::Approx(z).epsilon(2e-3));
        }
    }
}
