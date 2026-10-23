#include <doctest/doctest.h>

#include "Playback.hpp"

using namespace gs;

namespace {

// 2 s in 20 steps: a frame every 0.1 s.
constexpr Clip kLoop{2.0, 20, true};
constexpr Clip kOnce{2.0, 20, false};

} // namespace

TEST_CASE("a loop has one frame less to show than a sequence played once")
{
    CHECK(kLoop.last_frame() == 19);   // frame 20 closes the loop as frame 0 again
    CHECK(kOnce.last_frame() == 20);
    CHECK(kOnce.frame_time(20) == doctest::Approx(2.0));
}

TEST_CASE("playing moves the clock on at its speed; pausing freezes it")
{
    Playback p;
    REQUIRE(p.playing());
    p.advance(0.5, kLoop);
    CHECK(p.time() == doctest::Approx(0.5));
    p.set_speed(0.25);
    p.advance(0.4, kLoop);
    CHECK(p.time() == doctest::Approx(0.6));
    p.pause();
    p.advance(1.0, kLoop);
    CHECK(p.time() == doctest::Approx(0.6));
    p.toggle(kLoop);
    CHECK(p.playing());
}

TEST_CASE("a loop wraps round and a sequence played once holds its last frame")
{
    Playback loop;
    loop.advance(2.5, kLoop);
    CHECK(loop.time() == doctest::Approx(0.5));
    CHECK(loop.playing());

    Playback once;
    once.advance(2.5, kOnce);
    CHECK(once.time() == 2.0);
    CHECK_FALSE(once.playing());
    CHECK(once.at_end(kOnce));
    CHECK(once.frame(kOnce) == 20);
    once.advance(1.0, kOnce);
    CHECK(once.time() == 2.0);   // held

    once.play(kOnce);   // played again from the start
    CHECK(once.time() == 0.0);
    CHECK(once.playing());
}

TEST_CASE("the frame on show is the exported frame the time is in")
{
    Playback p;
    p.pause();
    p.seek(0.3, kLoop);   // 0.3 / 0.1 rounds just under 3 in doubles
    CHECK(p.frame(kLoop) == 3);
    p.seek(0.35, kLoop);
    CHECK(p.frame(kLoop) == 3);
    p.seek(1.999, kLoop);
    CHECK(p.frame(kLoop) == 19);
}

TEST_CASE("a step pauses and lands on the next or the previous frame")
{
    Playback p;
    p.advance(0.35, kOnce);   // between frames 3 and 4
    p.step(1, kOnce);
    CHECK_FALSE(p.playing());
    CHECK(p.frame(kOnce) == 4);
    CHECK(p.time() == kOnce.frame_time(4));

    p.seek(0.35, kOnce);
    p.step(-1, kOnce);   // back to the frame on show first
    CHECK(p.time() == kOnce.frame_time(3));
    p.step(-1, kOnce);
    CHECK(p.time() == kOnce.frame_time(2));
}

TEST_CASE("steps stop at the ends of a sequence played once and wrap round a loop")
{
    Playback once;
    once.step(-1, kOnce);
    CHECK(once.at_start());
    once.to_end(kOnce);
    once.step(1, kOnce);
    CHECK(once.frame(kOnce) == 20);
    CHECK(once.at_end(kOnce));

    Playback loop;
    loop.step(-1, kLoop);
    CHECK(loop.frame(kLoop) == 19);
    loop.step(1, kLoop);
    CHECK(loop.at_start());
}

TEST_CASE("the ends pause on the first and the last frame")
{
    Playback p;
    p.advance(0.7, kLoop);
    p.to_end(kLoop);
    CHECK_FALSE(p.playing());
    CHECK(p.time() == kLoop.frame_time(19));
    p.to_start();
    CHECK(p.at_start());
}

TEST_CASE("seeking keeps playing or paused, and stays inside the clip")
{
    Playback p;
    p.seek(5.0, kOnce);
    CHECK(p.time() == 2.0);
    CHECK(p.playing());
    p.seek(2.0, kLoop);   // the far end of a loop's slider is just before its start again
    CHECK(p.time() < 2.0);
    CHECK(p.frame(kLoop) == 19);
    p.seek(-1.0, kLoop);
    CHECK(p.at_start());
}

TEST_CASE("a shorter clip brings the time back inside it")
{
    Playback p;
    p.pause();
    p.seek(1.5, kOnce);
    p.advance(0.0, Clip{1.0, 10, false});
    CHECK(p.time() == 1.0);
    p.seek(1.5, kLoop);
    p.advance(0.0, Clip{1.0, 10, true});
    CHECK(p.time() == doctest::Approx(0.5));
}

TEST_CASE("without the controls the preview plays at normal speed")
{
    Playback p;
    p.pause();
    p.set_speed(4.0);
    p.play_freely();
    CHECK(p.playing());
    CHECK(p.speed() == 1.0);
}
