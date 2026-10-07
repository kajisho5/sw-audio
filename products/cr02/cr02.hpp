// SW CR02 Stutter — beat-synchronised repeats of the last slice of the input (spec: 仕様書 v1.0「CR02 Stutter」). Reported delay 0 (the slice is taken from the input that has just gone by).
//   Grid 1/8 / 1/16 / 1/32 / Triplet (1/8 triplet = a third of a beat) sets the step; the 16-step Pattern (cr02.step01 .. step16, not automatable) says which steps stutter (the others pass the dry
//   signal). The position comes from the host: tempo and the beats to the next bar (a 4/4 bar is assumed); without a transport it runs from the start at the tempo (120 bpm without one).
//   At the start of a stuttering step the last (step / Repeat) samples are copied and played Repeat times; Gate = the open part of every repeat (1 ms fades); Pitch = the playback rate (the slice loops
//   when it runs out); Reverse plays it backwards; Filter = a low-pass (second order) on the repeats (20 kHz = out); Mix mixes them with the dry signal during the step.
//   Randomize / Clear (methods for the UI buttons): Clear empties the pattern; Randomize puts steps with a probability by their place in the
//   beat (on the beat 0.85, the eighth 0.6, the sixteenth 0.35, else 0.4), raised by 0.3 (up to 0.95) at the steps where the input had an onset in the last bar (EVO, class A).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::cr02 {

enum ParamId { Grid, Gate, Repeat, Pitch, Reverse, Filter, Mix, Step01, Step16 = Step01 + 15, kNumParams };

const std::vector<ParamSpec>& specs();
double gridBeats(int grid);   // 0: 1/8 (0.5 beat), 1: 1/16 (0.25), 2: 1/32 (0.125), 3: triplet (1/3)

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm) { bpm_ = bpm; }
    void setTransport(bool playing, double beatsToNextBar);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // the Pattern buttons (the UI calls them and writes the 16 step parameters to the host: a core that changed parameters by itself would differ from a plain parameter flush)
    void randomize();
    void clearPattern();
    bool stepOn(int i) const { return target_[static_cast<size_t>(Step01 + i)] > 0.5; }
    bool stuttering() const { return active_; }

private:
    void startStep(int64_t idx);
    double fs_ = 48000.0, bpm_ = 0.0, barBeat_ = 0.0, env_ = 0.0, slow_ = 0.0;
    int64_t bars_ = 0, lastStep_ = -1, pos_ = 0;
    bool prepared_ = false, active_ = false;
    double hostToBar_ = -1.0;
    uint32_t rng_ = 0x9e3779b9u;
    int sliceLen_ = 1, rep_ = 1, stepSamples_ = 1, sinceStart_ = 0;
    double readPos_ = 0.0;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> ring_;
    std::array<std::vector<float>, 2> slice_;
    std::array<Svf, 2> lp_{};
    std::array<int64_t, 16> onsetBar_{};
    size_t wpos_ = 0, mask_ = 0;
};

}  // namespace sw::cr02
