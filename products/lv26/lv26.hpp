// SW LV26 Mono — a mono-compatibility safety device (spec: 仕様書 v1.0「LV26 Mono」). Reported delay 0; no Auto gain, no Delta.
//   M = (L + R) / 2, S = (L - R) / 2. Width (0..200 %) scales S; Low mono (Off, 20..300 Hz) removes the S below that frequency (2nd-order high-pass on S). Mono check (monitoring, not automatable): both channels carry M.
//   Auto phase fix (EVO, class B): L and R are split into 4 bands (Linkwitz-Riley at 250 Hz, 1.5 kHz and 6 kHz); in every band the correlation of L and R is followed (200 ms); where it is under -0.2 (the band cancels in mono) its S is lowered
//   by 12 dB x (-c - 0.2) / 0.8 (up to 12 dB), with a 100 ms smoothing. The correction is a cascade on S (a low shelf at 250 Hz, bells at 600 Hz and 3 kHz, a high shelf at 6 kHz; each exactly the identity at 0 dB, so with every g_b = 1 the S passes bit for bit), updated every 16 samples while a gain moves. bandCorrelation(b) and bandReductionDb(b) are for the screen.
#pragma once
#include "sw/lr4split.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lv26 {

enum ParamId { Width, LowMono, MonoCheck, AutoPhaseFix, kNumParams };
constexpr int kBands = 4;

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double bandCorrelation(int b) const { return corr_[static_cast<size_t>(b)]; }
    double bandReductionDb(int b) const { return 20.0 * std::log10(std::max(g_[static_cast<size_t>(b)], 1e-9)); }

private:
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    void setSideFilters();
    Lr4Split4 splitLR_;   // L and R are channels 0 and 1
    std::array<Svf, 4> sideF_{};
    std::array<double, kBands> appliedDb_{}; unsigned tick_ = 0;
    Svf lowMono_;
    std::array<double, kBands> pLR_{}, pLL_{}, pRR_{}, corr_{}, g_{};
};

}  // namespace sw::lv26
