#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "Presets.hpp"
#include "SettingsJson.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

// 4 particles, 2 s, 10 fps: 20 steps, a particle lives 1 s.
Settings one_shot(EndBehavior end = EndBehavior::HideUnderground, ScaleMode scale = ScaleMode::Static)
{
    Settings s;
    s.loop = false;
    s.count = 4;
    s.duration = 2.0;
    s.fps = 10;
    s.lifespan = 0.5;
    s.end_behavior = end;
    s.scale_mode = scale;
    s.geometry = Geometry::SingleQuad;
    s.speed = 0;
    s.spread = 0;
    s.burst = 100;
    s.gravity = 0;
    return s;
}

double length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

TEST_CASE("an effect loops by default and a one-shot is saved as such")
{
    CHECK(Settings{}.loop);
    CHECK(Settings{}.burst_window == 0.0);

    Settings s;
    s.loop = false;
    s.burst_window = 0.25;
    const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{});
    CHECK_FALSE(loaded.settings.loop);
    CHECK(loaded.settings.burst_window == 0.25);

    // Files from before the setting existed loop.
    nlohmann::json old = to_json(Settings{});
    old.erase("loop");
    old.erase("burst_window");
    CHECK(settings_from_json(old, Settings{}).settings.loop);

    Settings wild;
    wild.burst_window = 7;
    CHECK(validate(wild));
    CHECK(wild.burst_window == 1.0);
}

TEST_CASE("a one-shot sequence has its last frame at the full duration, a loop has frame 0 again")
{
    const ParticleSystem shot(one_shot());
    CHECK(shot.export_frames() == 21);
    CHECK(shot.frame_time(0) == 0.0);
    CHECK(shot.frame_time(10) == doctest::Approx(1.0));
    CHECK(shot.frame_time(20) == doctest::Approx(2.0));

    Settings looping = one_shot();
    looping.loop = true;
    const ParticleSystem loop(looping);
    CHECK(loop.export_frames() == 21);
    CHECK(loop.frame_time(20) == 0.0);
}

TEST_CASE("one-shot particles are all born at the start and are not born again")
{
    const ParticleSystem sys(one_shot());
    for (const Particle& p : sys.particles()) {
        const LifeState start = sys.life_state(p, 0.0);
        CHECK(start.phase == LifeState::Phase::Alive);
        CHECK(start.eval_time == 0.0);
        CHECK(sys.life_state(p, 0.9).phase == LifeState::Phase::Alive);
        // Past the lifespan they are gone, also at the very end where a loop would have started again.
        CHECK(sys.life_state(p, 1.2).phase == LifeState::Phase::Dead);
        CHECK(sys.life_state(p, 2.0).phase == LifeState::Phase::Dead);
        CHECK(sys.generation(p, 0.0) == sys.generation(p, 2.0));
    }

    Settings looping = one_shot();
    looping.loop = true;
    const ParticleSystem loop(looping);
    // The last particle of a loop is born at the loop time: alive again at the end of the loop.
    CHECK(loop.life_state(loop.particles().back(), 2.0).phase == LifeState::Phase::Alive);
}

TEST_CASE("a burst window spreads the births over the start of the effect")
{
    Settings s = one_shot();
    s.burst_window = 0.5;   // births over the first second
    const ParticleSystem sys(s);
    const auto& p = sys.particles();
    REQUIRE(p.size() == 4);
    CHECK(p[0].birth == 0.0);
    CHECK(p[1].birth == doctest::Approx(0.25));
    CHECK(p[3].birth == doctest::Approx(0.75));

    CHECK(sys.life_state(p[3], 0.5).phase == LifeState::Phase::Unborn);
    CHECK(sys.life_state(p[3], 0.75).phase == LifeState::Phase::Alive);
    CHECK(sys.life_state(p[3], 0.75).eval_time == doctest::Approx(0.0));
    // It lives its lifespan from its own birth.
    CHECK(sys.life_state(p[3], 0.75 + 0.9).phase == LifeState::Phase::Alive);
    CHECK(sys.life_state(p[3], 0.75 + 1.1).phase == LifeState::Phase::Dead);

    // Waiting, it is out of sight like a hidden one.
    const ParticlePose waiting = sys.pose_at(p[3], 0.1);
    CHECK_FALSE(waiting.visible);
    CHECK(waiting.pos.z == doctest::Approx(sys.hide_z()));
}

TEST_CASE("an unborn vertex-bone particle is collapsed")
{
    Settings s = one_shot(EndBehavior::HideInCore, ScaleMode::VertexBone);
    s.burst_window = 0.5;
    const ParticleSystem sys(s);
    const ParticlePose waiting = sys.pose_at(sys.particles().back(), 0.1);
    CHECK_FALSE(waiting.visible);
    CHECK(waiting.scale == 0.0);
}

TEST_CASE("the sequence of a one-shot effect has no loop flag, a loop has")
{
    auto qc_of = [](const Settings& s) {
        const fs::path dir = fs::temp_directory_path() / "particlemdl_one_shot_qc";
        fs::remove_all(dir);
        fs::create_directories(dir);
        export_model(ParticleSystem(s), dir);
        std::ifstream in(dir / "custom_particles.qc");
        std::stringstream text;
        text << in.rdbuf();
        in.close();   // Windows does not delete a file that is open
        fs::remove_all(dir);
        return text.str();
    };
    const std::string shot = qc_of(one_shot());
    CHECK(shot.find("$sequence \"idle\" \"custom_particles_anim\" fps 10") != std::string::npos);
    CHECK(shot.find("loop") == std::string::npos);

    Settings looping = one_shot();
    looping.loop = true;
    CHECK(qc_of(looping).find("\"custom_particles_anim\" loop fps 10") != std::string::npos);
}

TEST_CASE("the baked frames of a one-shot effect end where the effect ends")
{
    const ParticleSystem sys(one_shot(EndBehavior::HideInCore, ScaleMode::VertexBone));
    const BakedPoses baked = bake(sys);
    CHECK_FALSE(baked.looping());
    CHECK(baked.stored_frames() == 21);
    CHECK(baked.frames() == 21);
    for (int i = 0; i < baked.particles(); ++i) {
        CHECK(baked.at(0, i).scale > 0.0);    // alive at the start
        CHECK(baked.at(20, i).scale == 0.0);  // gone at the end, not the start again
        CHECK(baked.at(25, i).scale == baked.at(20, i).scale);   // past the end holds the last frame
    }

    // Sampled outside the duration it holds the first and last frame instead of wrapping.
    CHECK(baked.sample(0, -5.0).pos == baked.at(0, 0).pos);
    CHECK(baked.sample(0, 50.0).pos == baked.at(20, 0).pos);
}

TEST_CASE("a one-shot effect has no jump to hide at the end of the loop")
{
    // Static quads that stay alive to the end: a loop would need to hide the wrap, a one-shot has none.
    Settings s = one_shot(EndBehavior::EndlessLoop);
    s.lifespan = 1.0;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    CHECK(baked.unhidden_jump() == 0.0);

    Settings looping = s;
    looping.loop = true;
    looping.speed = 60;
    CHECK(bake(ParticleSystem(looping)).unhidden_jump() > 0.0);
}

TEST_CASE("no visible quad of a one-shot effect with vertex bones travels farther than the motion allows")
{
    for (const EndBehavior end : {EndBehavior::DropThroughFloor, EndBehavior::HideUnderground, EndBehavior::HideInCore,
                                  EndBehavior::StopAndStay, EndBehavior::EndlessLoop})
        for (const double window : {0.0, 0.3, 0.9})
            for (const double lifespan : {0.4, 0.8, 1.0}) {
                CAPTURE(enum_id(end));
                CAPTURE(window);
                CAPTURE(lifespan);
                Settings s = one_shot(end, ScaleMode::VertexBone);
                s.lifespan = lifespan;
                s.burst_window = window;
                s.count = 8;
                s.fps = 15;
                s.speed = 60;
                s.spread = 30;
                s.gravity = -100;
                const ParticleSystem sys(s);
                const BakedPoses baked = bake(sys);

                // The motion's own step: segments in which a particle neither appears nor dies.
                double natural = 0;
                for (int i = 0; i < sys.num_particles(); ++i)
                    for (int f = 0; f < sys.loop_steps(); ++f) {
                        const double t0 = sys.frame_time(f), t1 = sys.loop_duration() * (f + 1) / sys.loop_steps();
                        const LifeState a = sys.life_state(sys.particles()[i], t0), b = sys.life_state(sys.particles()[i], t1);
                        if (a.unborn() != b.unborn() || a.past_lifespan() != b.past_lifespan())
                            continue;
                        natural = std::max(natural, length(sys.pose_at(sys.particles()[i], t1).pos - sys.pose_at(sys.particles()[i], t0).pos));
                    }
                const double limit = natural * 1.5 + 1e-6;

                for (int i = 0; i < baked.particles(); ++i)
                    for (int f = 0; f < sys.loop_steps(); ++f) {
                        const ParticlePose& a = baked.at(f, i);
                        const ParticlePose& b = baked.at(f + 1, i);
                        if (a.scale > 0 || b.scale > 0)
                            CHECK(length(b.pos - a.pos) <= limit);
                    }
            }
}

TEST_CASE("the Explosion preset is a one-shot burst of vertex-bone particles")
{
    const Preset* explosion = find_preset("Explosion");
    REQUIRE(explosion);
    CHECK_FALSE(explosion->settings.loop);
    CHECK(explosion->settings.emission_angle == 360.0);   // out all around
    CHECK(explosion->settings.scale_mode == ScaleMode::VertexBone);
    Settings s;
    apply_preset(s, *explosion);
    CHECK(analyze_export(ParticleSystem(s)).warnings.empty());
}
