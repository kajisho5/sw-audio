// SW LV25 Live Delay — a tap-tempo delay (spec: 仕様書 v1.0「LV25 Live Delay」). Wet signal only (Mix is the shared frame's, default 15 %); reported delay 0.
//   Per channel: record = tone(in + Feedback x echo) -> delay line (Hermite read, the time glides over 100 ms) -> echo. Loop: high-pass 80 Hz and a low-pass by Tone (Dark 3 kHz / Neutral 6 kHz / Bright 12 kHz), limited by L tanh(x / L) (L = 1.6)
//   so that Feedback up to 95 % always settles. Time 1..2000 ms.
//   Clock: Tap — tap() (the screen's button, "Auto 不可"): two taps within 2 s set the Time to their distance, more taps average the last four intervals (up to 3 s apart each); the value goes to the host through takeParamWrite.
//   BPM — Time = a quarter note at the host tempo (setTempo; without tempo the Time knob). MIDI — midiClockTick() 24 times per quarter note (the engine feeds it: **the plug-in adapters carry no MIDI yet**, so in a plain host this mode keeps the last Time);
//   the tempo is measured over the last 24 ticks and Time = a quarter note.
//   Bypass (design: an input-only bypass the spec describes — "バイパスしても残りの返りは鳴り終わるまで出す" — as a parameter, 'Input bypass', appended after the spec's table): the input stops entering the loop, the echoes already in it ring out.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv25 {

enum ParamId { Clock, Time, Feedback, Tone, Mix, InputBypass, kNumParams };
enum ClockId { Tap = 0, Midi = 1, Bpm = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { timeS_ = goalS(); } }
    void setTempo(double bpm) { bpm_ = bpm; }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void tap();
    void midiClockTick();
    int takeParamWrite(int& id, double& plain);
    double timeMs() const { return goalS() * 1000.0; }
    double tailSeconds() const;   // how long the repeats go on (sw/tail.hpp)

private:
    double goalS() const;
    double fs_ = 48000.0, timeS_ = 0.5, bpm_ = 0.0, clock_ = 0.0, lastTap_ = -1e9, midiBpm_ = 0.0;
    int tapCount_ = 0, tickCount_ = 0;
    double tapGaps_[4] = {0, 0, 0, 0}, tickClock_ = 0.0;
    bool prepared_ = false, write_ = false;
    double written_ = 500.0, tapTime_ = 0.5;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    size_t mask_ = 0, pos_ = 0;
    std::array<double, 2> echo_{};
    struct Ch { Svf hp, lp; };
    std::array<Ch, 2> c_{};
};

}  // namespace sw::lv25
