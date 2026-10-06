// SW VO04 Doubler — natural-sounding doubles (spec: 仕様書 v1.0「VO04 Doubler」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   Voices (1 / 2 / 4 / 8): each voice is a tap of a delay line whose time wanders slowly and irregularly (two incommensurate slow sines, 0.02 .. 0.07 Hz, never a periodic
//   chorus sweep): 0.5 ms + Timing x 3 ms x u (u wanders 0.15 .. 1, so Timing 10 = up to 30 ms). The pitch of a voice is moved only by the slope of its delay (ratio 1 - dD/dt):
//   Pitch var adds three more sines (0.7 .. 2.3 Hz, voice-specific) with a delay slope of 0.8 x Pitch var cents rms. After a pause (input below -60 dB for 100 ms) the delay of every voice
//   returns to its smallest value at once (nothing is sounding), and then grows back at no more than 0.5 % slope (about 8 cents): the start of a phrase is not smeared.
//   Spread puts the voices from the centre to the whole stereo width (MD07 construction, linear pan, unit power); Tone tilts the doubles: high shelf (3 kHz) +-8 dB, low shelf (400 Hz) -+4 dB.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::vo04 {

enum ParamId { Voices, Spread, Timing, PitchVar, Tone, Mix, kNumParams };

const std::vector<ParamSpec>& specs();
constexpr int kMaxVoices = 8;
double voicePan(int voice, int voices, double spread);   // 0 (left) .. 1 (right)

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double voiceDelaySamples(int voice) const { return lastDelay_[static_cast<size_t>(voice)]; }   // the delay used for the last sample (wander + pitch part)

private:
    double read(int c, double delay) const;
    void setTone();
    double fs_ = 48000.0, toneState_ = -1.0, env_ = 0.0, t_ = 0.0;
    double lastDelay_[kMaxVoices] = {}, base_[kMaxVoices] = {};
    long silent_ = 0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    struct Chan { Svf hi, lo; };
    std::array<Chan, 2> ch_{};
};

}  // namespace sw::vo04
