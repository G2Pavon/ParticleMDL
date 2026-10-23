#include "Playback.hpp"

#include <algorithm>
#include <cmath>

#include "MathUtil.hpp"

namespace gs {

namespace {

// A time inside the clip: a loop wraps round (its end is its start again), a sequence played
// once stops at its ends.
double within(double time, const Clip& clip)
{
    if (clip.looping)
        return floor_mod(time, clip.duration);
    return std::clamp(time, 0.0, clip.duration);
}

} // namespace

int Playback::frame(const Clip& clip) const
{
    // A frame's own time counts as that frame, whatever the rounding of the division.
    const int f = static_cast<int>(std::floor(time_ / clip.duration * clip.steps + 1e-9));
    return std::clamp(f, 0, clip.last_frame());
}

void Playback::advance(double delta, const Clip& clip)
{
    if (playing_)
        time_ += delta * speed_;
    if (!clip.looping && time_ >= clip.duration) {
        time_ = clip.duration;   // the game holds the last frame of a sequence played once
        playing_ = false;
        return;
    }
    time_ = within(time_, clip);
}

void Playback::play(const Clip& clip)
{
    if (!clip.looping && time_ >= clip.duration)
        time_ = 0.0;
    playing_ = true;
}

void Playback::toggle(const Clip& clip)
{
    if (playing_)
        pause();
    else
        play(clip);
}

void Playback::step(int frames, const Clip& clip)
{
    playing_ = false;
    int target = frame(clip) + frames;
    // Between two frames, a step back first goes to the frame on show.
    if (frames < 0 && time_ > clip.frame_time(frame(clip)) + 1e-9)
        ++target;
    const int count = clip.last_frame() + 1;
    target = clip.looping ? ((target % count) + count) % count : std::clamp(target, 0, clip.last_frame());
    time_ = clip.frame_time(target);
}

void Playback::to_start() noexcept
{
    playing_ = false;
    time_ = 0.0;
}

void Playback::to_end(const Clip& clip)
{
    playing_ = false;
    time_ = clip.frame_time(clip.last_frame());
}

void Playback::seek(double time, const Clip& clip)
{
    // A loop's end is its start again: the slider's far end stays just before it.
    time_ = clip.looping ? std::clamp(time, 0.0, std::nextafter(clip.duration, 0.0)) : within(time, clip);
}

void Playback::play_freely() noexcept
{
    playing_ = true;
    speed_ = 1.0;
}

} // namespace gs
