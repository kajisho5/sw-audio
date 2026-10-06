// SW VO07 Vocal Strip — a vocal chain in the right order: clean -> tone -> dynamics -> space (spec: 仕様書 v1.0「VO07 Vocal Strip」). Reported delay 0.
//   HPF (20 .. 300 Hz, second order) -> De-ess (sw DY05 core, Split, 6.5 kHz, Range = -1.5 dB x De-ess) -> Breath (own: when the 40 ms level is more than 15 dB under the phrase peak
//   (held, falling 6 dB/s) and the sound is noise-like (zero crossings above 0.12 per sample), the gain goes down by 1.5 dB x Breath, up to -15 dB; 10 ms / 60 ms) ->
//   Body (bell 200 Hz, Q 0.8) / Presence (bell 3 kHz, Q 0.9) / Air (high shelf 12 kHz) each +-6 dB -> Comp (sw DY02 core: Level = Comp, Prog speed, Auto makeup On = the loudness stays)
//   -> Level (+-12 dB, after the compressor) -> + Plate (sw RV02 core, Decay 1.8 s, pre-delay 20 ms, low cut 120 Hz) x Plate/10 x 0.5 (-6 dB at 10) + Echo (sw DL01 core, Analog, an eighth note
//   at the host tempo (250 ms without a tempo), Feedback 30 %, HPF 200 Hz, LPF 6 kHz) x Echo/10 x 0.5. Both sends take the signal after Level. Output is the shared frame's.
#pragma once
#include "dl01/dl01.hpp"
#include "dy02/dy02.hpp"
#include "dy05/dy05.hpp"
#include "rv02/rv02.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::vo07 {

enum ParamId { Hpf, Deess, Breath, Body, Presence, Air, Comp, Level, Plate, Echo, Output, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double breathGainDb() const { return breathDb_; }
    double compReductionDb() const { return comp_.gainReductionDb(); }

private:
    void chunk(float** ch, int nch, int n);
    void setTone();
    void setEcho();
    double fs_ = 48000.0, bpm_ = 0.0, env_ = 0.0, peakDb_ = -200.0, breathDb_ = 0.0, hold_ = 0.0, zcr_ = 0.0, prevX_ = 0.0;
    int maxBlock_ = 512;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    struct Chan { Svf hp, body, pres, air; };
    std::array<Chan, 2> ch_{};
    dy05::Processor deess_;
    dy02::Processor comp_;
    rv02::Processor plate_;
    dl01::Processor echo_;
    std::array<std::vector<float>, 2> sendP_, sendE_;
};

}  // namespace sw::vo07
