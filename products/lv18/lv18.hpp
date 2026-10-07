// SW LV18 Pop Guard — catches mic noises (spec: 仕様書 v1.0「LV18 Pop Guard」). Reported delay 0, so the first 1..2 ms of an event pass before it is recognised (the spec's 要確認: a 2 ms look-ahead is not offered here).
//   Four detectors on the mono sum, each with its own reaction; Sensitivity (Low / Mid / High) scales the thresholds (x1.6 / 1 / 0.6 of the Mid values below):
//   Plug pop (a step in the very low band): the signal under 30 Hz (1st-order low-pass, 5 ms) over 0.05 and 3 times its 200 ms average -> mute for Mute time (3 ms fade down, 20 ms back).
//   Handling (a knock, mostly low-frequency): the 2 ms energy over the 100 ms background (frozen while an event is on) by 20 dB while the part under 300 Hz carries 70 % of it and the peak is over -20 dBFS -> mute for Mute time.
//   Plosive (a puff of air: a burst under 150 Hz without highs): the under-150 Hz energy over its 100 ms background by 18 dB and the part over 300 Hz under a tenth of it -> a 200 Hz high-pass (4th order) fades in for Mute time.
//   Wind (steady low-frequency rumble): the level under 50 Hz (two 1st-order low-passes: a voice has next to nothing there) over -38 dBFS (100 ms average) for 150 ms -> the same high-pass, held while it lasts (and 300 ms after).
//   caught(type) counts the events since reset (the screen's "Caught today"); Plug pop / Wind / Handling / Plosive switch the detectors (Off, Off... On / On / On / Off by default).
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv18 {

enum ParamId { Sensitivity, MuteTime, PlugPop, Wind, Handling, Plosive, kNumParams };
enum EventType { EvPlug = 0, EvWind, EvHandling, EvPlosive, kEvents };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int caught(int type) const { return count_[static_cast<size_t>(type)]; }
    int caughtTotal() const { int s = 0; for (int c : count_) s += c; return s; }
    void resetCounts() { count_.fill(0); }

private:
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<int, kEvents> count_{};
    // detector state
    double lp30_ = 0, avg30_ = 0, lp300_ = 0, lp150_ = 0, lp50_ = 0, lp50b_ = 0, eL50_ = 0, eFast_ = 0, eLow_ = 0, eBg_ = 0, eL150_ = 0, eH_ = 0, eL150bg_ = 0, peak_ = 0;
    double muteLeft_ = 0, hpLeft_ = 0, windOn_ = 0, windOff_ = 0, refractory_ = 0;
    bool windActive_ = false;
    double mg_ = 1.0, hpMix_ = 0.0;
    std::array<std::array<Svf, 2>, 2> hp_{};   // 4th-order 200 Hz high-pass per channel
};

}  // namespace sw::lv18
