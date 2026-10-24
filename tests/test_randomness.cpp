#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
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

TEST_CASE("the seeds 0 are the original sequence and another system seed gives another effect")
{
    const ParticleSystem zero(base());
    Settings s = base();
    s.system_seed = 0;
    CHECK(same_particles(zero, ParticleSystem(s)));

    s.system_seed = 1;
    const ParticleSystem one(s);
    s.system_seed = 2;
    const ParticleSystem two(s);
    CHECK_FALSE(same_particles(zero, one));
    CHECK_FALSE(same_particles(one, two));

    s.system_seed = 1;
    CHECK(same_particles(one, ParticleSystem(s)));   // and the same seed is the same effect
    s.system_seed = -5;
    CHECK(same_particles(ParticleSystem(s), ParticleSystem(s)));
    CHECK_FALSE(same_particles(zero, ParticleSystem(s)));
}

namespace {

// Everything random in an effect with every random setting in use.
Settings everything_random()
{
    Settings s = base();
    s.count = 10;
    s.spawn_radius = 40;
    s.spawn_height = 20;
    s.turbulence = 30;
    s.lifespan_variance = 0.5;
    s.size_variance = 0.5;
    s.spin_spread = 90;
    s.cluster_size = 3;
    return s;
}

bool same_system_values(const Particle& a, const Particle& b)
{
    return a.spawn == b.spawn && a.velocity == b.velocity && a.turb_freq == b.turb_freq && a.turb_phase == b.turb_phase &&
           a.lifespan == b.lifespan;
}

bool same_particle_values(const ParticleSystem& a, const ParticleSystem& b, int i)
{
    const Particle& pa = a.particles()[static_cast<std::size_t>(i)];
    const Particle& pb = b.particles()[static_cast<std::size_t>(i)];
    return pa.spin == pb.spin && pa.size == pb.size && a.quads_of(i)[1][0].pos == b.quads_of(i)[1][0].pos;
}

} // namespace

TEST_CASE("each seed changes all of its own values and none of the other's")
{
    const Settings s = everything_random();
    Settings system = s, particle = s;
    system.system_seed = 12;
    particle.particle_seed = 12;
    const ParticleSystem original(s), new_system(system), new_particle(particle);
    for (int i = 0; i < original.num_particles(); ++i) {
        CAPTURE(i);
        const Particle& o = original.particles()[static_cast<std::size_t>(i)];
        const Particle& by_system = new_system.particles()[static_cast<std::size_t>(i)];
        const Particle& by_particle = new_particle.particles()[static_cast<std::size_t>(i)];

        // The system seed: where it starts, how it leaves, its wobble and its lifespan.
        CHECK(o.spawn != by_system.spawn);
        CHECK(o.velocity != by_system.velocity);
        CHECK(o.turb_freq != by_system.turb_freq);
        CHECK(o.turb_phase != by_system.turb_phase);
        CHECK(o.lifespan != by_system.lifespan);
        CHECK(same_particle_values(original, new_system, i));

        // The particle seed: its spin, its size and its cluster copies.
        CHECK(o.spin != by_particle.spin);
        CHECK(o.size != by_particle.size);
        CHECK(original.quads_of(i)[1][0].pos != new_particle.quads_of(i)[1][0].pos);
        CHECK(same_system_values(o, by_particle));
    }
}

TEST_CASE("a file with the one seed of earlier versions loads as both seeds and looks as it did")
{
    nlohmann::json j = to_json(everything_random());
    j.erase("system_seed");
    j.erase("particle_seed");
    j["seed"] = 7;
    const SettingsLoad loaded = settings_from_json(j, Settings{});
    CHECK(loaded.warnings.empty());
    CHECK(loaded.settings.system_seed == 7);
    CHECK(loaded.settings.particle_seed == 7);

    // What the version with one seed made of particle 3 with seed 7.
    const ParticleSystem sys(loaded.settings);
    const Particle& p = sys.particles()[3];
    auto near = [](double a, double b) { return std::abs(a - b) <= 1e-12 * std::max(1.0, std::abs(b)); };
    CHECK(near(p.spawn.x, 33.680566633424377));
    CHECK(near(p.spawn.y, -8.4689434978300202));
    CHECK(near(p.spawn.z, -17.175807339266544));
    CHECK(near(p.velocity.x, 29.866982699382984));
    CHECK(near(p.velocity.y, -7.5100217785743366));
    CHECK(near(p.velocity.z, 110.76699480938495));
    CHECK(near(p.spin.x, 0.46143450912463652));
    CHECK(near(p.spin.y, -0.768192327912534));
    CHECK(near(p.spin.z, 0.1975449806232836));
    CHECK(near(p.lifespan, 0.74065900702745646));
    CHECK(near(p.size, 1.4465141716372238));
    CHECK(near(p.turb_freq.x, 4.1475610091328878));
    CHECK(near(p.turb_freq.y, 0.97806624634938188));
    CHECK(near(p.turb_phase.x, 5.0670325506005653));
    CHECK(near(p.turb_phase.y, 3.6666806471443754));
    const Vec3 corner = sys.quads_of(3)[1][0].pos;
    CHECK(near(corner.x, 17.285011666652732));
    CHECK(near(corner.y, 1.2270060721715923));
    CHECK(near(corner.z, 3.8529686511616319));

    // A file that has the two seeds keeps them, whatever an old key says.
    j["system_seed"] = 3;
    j["particle_seed"] = 4;
    const Settings both = settings_from_json(j, Settings{}).settings;
    CHECK(both.system_seed == 3);
    CHECK(both.particle_seed == 4);

    j["seed"] = "seven";
    j.erase("system_seed");
    const SettingsLoad bad = settings_from_json(j, Settings{});
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].starts_with("seed: expected"));
}

TEST_CASE("a particle does not depend on how many there are when the seed is the same")
{
    Settings few = base(), many = base();
    few.count = 5;
    many.count = 30;
    few.system_seed = many.system_seed = 9;
    few.particle_seed = many.particle_seed = 4;
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

    std::istringstream in(export_files(sys).bytes_of("_ref.smd"));
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
            extent[static_cast<std::size_t>(bone)] = std::max({extent[static_cast<std::size_t>(bone)], std::abs(y), std::abs(z)});
    }
    for (std::size_t i = 0; i < sys.particles().size(); ++i)
        CHECK(extent[i] == doctest::Approx(5.0 * sys.particles()[i].size).epsilon(1e-5));
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
    s.system_seed = 77;
    s.particle_seed = 78;
    s.lifespan_variance = 0.3;
    s.size_variance = 0.2;
    s.drag = 1.5;
    const SettingsLoad back = settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{});
    CHECK(back.settings.system_seed == 77);
    CHECK(back.settings.particle_seed == 78);
    CHECK(back.settings.lifespan_variance == 0.3);
    CHECK(back.settings.size_variance == 0.2);
    CHECK(back.settings.drag == 1.5);

    nlohmann::json old = to_json(Settings{});
    for (const char* key : {"system_seed", "particle_seed", "lifespan_variance", "size_variance", "drag"})
        old.erase(key);
    const Settings loaded = settings_from_json(old, Settings{}).settings;
    CHECK(loaded.system_seed == 0);
    CHECK(loaded.particle_seed == 0);
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
    s.system_seed = 31;
    s.particle_seed = 32;
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
