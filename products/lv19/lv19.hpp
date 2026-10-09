// SW LV19 Av Sync — audio / video delay correction (spec: 仕様書 v1.0「LV19 Av Sync」). A delay line of 0..1000 ms (fractional delays interpolated, glides on a change). **Reported delay 0** (the delay is the effect; the host must not compensate it).
//   Frame rate 24 / 25 / 29.97 / 30 / 59.94 fps; Frames (display) = Delay x fps / 1000 (frames()); Lock to video On: the delay is rounded to whole frames (frameMs() each).
//   Clap sync (EVO, class B; a button, not a parameter): markVideoClap(reactionMs) says "the hands met now" — from the SW AUDIO for OBS plug-in with the frame time (reactionMs 0), or from the user's button press in the VST3 version (a person is late by about
//   150 ms, so the screen passes it). The core keeps the last 2 s of the input, waits 0.5 s, and takes the loudest sharp transient (peak over 12 times the median of the 2 s window) in the 2.5 s as the audio clap. The video moment minus the audio moment is the delay
//   needed when the sound comes first (> 0: the Delay value is written to the host through takeParamWrite, rounded to 0.1 ms; Lock to video then rounds it to frames). When the sound is late (< 0) a delay line cannot help: audioLate() is true and
//   lateMs() says how much the video must be delayed instead.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::lv19 {

enum ParamId { FrameRate, Delay, LockToVideo, kNumParams };
enum SyncState { Idle = 0, Waiting = 1, Done = 2, Failed = 3 };

const std::vector<ParamSpec>& specs();
double framesPerSecond(int index);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) cur_ = effectiveDelayMs() * 0.001 * fs_; }
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    int latencySamples() const { return 0; }
    double effectiveDelayMs() const;
    double frames() const { return effectiveDelayMs() * 0.001 * framesPerSecond(static_cast<int>(target_[FrameRate] + 0.5)); }
    double frameMs() const { return 1000.0 / framesPerSecond(static_cast<int>(target_[FrameRate] + 0.5)); }
    void markVideoClap(double reactionMs = 0.0);
    int syncState() const { return state_; }
    bool audioLate() const { return late_; }
    double foundMs() const { return foundMs_; }
    double lateMs() const { return late_ ? -foundMs_ : 0.0; }
    int takeParamWrite(int& id, double& plain);

private:
    double fs_ = 48000.0, cur_ = 0.0;
    bool prepared_ = false, late_ = false, writePending_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    std::vector<float> mono_, scratch_;   // the last 2 s of the mono sum (a ring); scratch for the median (sized in prepare)
    size_t mask_ = 0, pos_ = 0, monoPos_ = 0, monoN_ = 0;
    int state_ = Idle; long long markIndex_ = 0, total_ = 0; double foundMs_ = 0; long long waitLeft_ = 0;
};

}  // namespace sw::lv19
