// SW RV03 Spring — spring reverb (spec: 仕様書 v1.0「RV03 Spring」). Wet signal only (Mix is the shared frame's); no reported delay.
//   Each spring is a loop: delay line -> one-pole low-pass (Tone) -> a chain of 28 "stretched" all-passes (a + z^-M) / (1 + a z^-M), a < 0, which delays the lows
//   more than the highs (the spring's chirp; Tension sets a and M) -> feedback gain (decay about 2.5 s). Springs 1..3 run in parallel with loop times of 33 / 41 / 52 ms.
//   Dwell = drive into the springs through a soft clip. Drip = how much the loops are excited by note onsets (the excitation is 0.7 while a note is held and rises
//   by 0.6 x Drip while an onset detector is open, about 60 ms after the onset), so held notes do not "boing".
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv03 {

enum ParamId { Springs, Dwell, Tone, Tension, Drip, Mix, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

// group delay (samples) of one spring's dispersion chain at f Hz for a Tension setting 0..10
double dispersionDelay(double f, double fs, double tension);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    static constexpr int kStages = 28, kMaxM = 4;
    struct Stage { std::array<double, 8> xr{}, yr{}; };   // rings of 8 (M <= 4 plus the interpolation tap)
    struct Spring {
        std::vector<float> buf; size_t pos = 0; double loopSamples = 0, lp = 0, dc = 0;
        std::array<Stage, kStages> st{};
        int idx = 0;   // position in the M-sample rings
    };
    void updateSprings();
    double fs_ = 48000.0, fast_ = 0, slow_ = 0, gate_ = 0, toneC_ = 0.5, aCoef_ = -0.6, fb_[3] = {0.9, 0.9, 0.9};
    int m_ = 3;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Spring, 3> sp_{};
    std::array<Svf, 2> shelf_{};
};

}  // namespace sw::rv03
