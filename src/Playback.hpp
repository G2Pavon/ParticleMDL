#pragma once

#include <array>

namespace gs {

// The timing of what the preview plays: the effect's duration, split into its exported frame
// steps, looping or played once.
struct Clip {
    double duration = 1.0;   // seconds, above 0
    int steps = 1;           // frame steps in the duration, at least 1
    bool looping = true;

    double frame_time(int frame) const { return frame * duration / steps; }
    // The last frame there is to show: a loop's closing frame is its first again, so a loop
    // ends a step earlier than a sequence played once.
    int last_frame() const { return looping ? steps - 1 : steps; }
};

// The preview's clock: where in the effect it is, whether it plays and how fast. A looping
// effect wraps around; one played once stops on its last frame and holds it, as the game does.
class Playback {
public:
    static constexpr std::array<double, 5> kSpeeds{0.25, 0.5, 1.0, 2.0, 4.0};

    double time() const noexcept { return time_; }
    bool playing() const noexcept { return playing_; }
    double speed() const noexcept { return speed_; }
    void set_speed(double speed) noexcept { speed_ = speed; }

    // The exported frame on show (the engine blends towards the next one).
    int frame(const Clip& clip) const;
    bool at_start() const noexcept { return time_ == 0.0; }
    bool at_end(const Clip& clip) const { return time_ == clip.frame_time(clip.last_frame()); }

    // Moves the clock on by `delta` seconds of real time when playing. Also keeps the time
    // inside the clip when the clip got shorter.
    void advance(double delta, const Clip& clip);

    // Playing an effect that stopped at its end starts it again.
    void play(const Clip& clip);
    void pause() noexcept { playing_ = false; }
    void toggle(const Clip& clip);

    // These pause and land on a frame: back or forward `frames` from the frame on show
    // (wrapping round a loop, stopping at the ends of a sequence played once), or its ends.
    void step(int frames, const Clip& clip);
    void to_start() noexcept;
    void to_end(const Clip& clip);

    // Any time within the clip; playing or not is kept.
    void seek(double time, const Clip& clip);
    // Back to the start, playing or not.
    void rewind() noexcept { time_ = 0.0; }
    // Playing at normal speed from where it is: the preview without the playback controls.
    void play_freely() noexcept;

private:
    double time_ = 0.0;
    bool playing_ = true;
    double speed_ = 1.0;
};

} // namespace gs
