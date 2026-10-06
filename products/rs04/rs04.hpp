// SW RS04 Declick — clicks and crackle removed by autoregressive interpolation (spec: 仕様書 v1.0「RS04 Declick」). Reported delay 512 samples (the look-ahead the interpolation needs).
//   Every 128 samples, per channel: an order-32 AR model is fitted to the last 1024 samples (sw/ar_repair.hpp); the excitation's robust scale sigma is median|e| / 0.6745; the samples about to leave
//   the plugin whose excitation is above T x sigma start a click. The click's length is the smallest of 1, 2, 4 ... samples (up to Click width) whose least-squares AR interpolation leaves no excitation
//   above T sigma; the samples are replaced. Target Click: T = 7 / 5 / 3.5 (Sensitivity Low / Mid / High), full replacement; Crackle: T = 4.5 / 3.5 / 2.8, at most 0.2 ms, replaced by Crackle %
//   of the way (x + c (interpolated - x)); Both: the click pass, then the crackle pass. Low guard On: when more than 90 % of the window's energy is below 150 Hz (a kick drum, not a click) T is x1.6.
//   clicksRepaired() counts the repairs (the display marks them; the spectrogram marks are the UI's).
#pragma once
#include "sw/ar_repair.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::rs04 {

enum ParamId { Target, Sensitivity, ClickWidth, Crackle, LowGuard, kNumParams };
enum TargetId { ClickOnly = 0, CrackleOnly = 1, Both = 2 };

const std::vector<ParamSpec>& specs();
constexpr int kLatency = 512, kHop = 128, kWin = 1024, kOrder = 32;

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return kLatency; }
    int clicksRepaired() const { return count_; }

private:
    struct Chan { std::vector<double> ring; };
    void analyse(Chan& c, int64_t t);
    int pass(std::vector<double>& w, double sigma, double T, int maxM, double amount, int rstart, int rend);
    double fs_ = 48000.0;
    int count_ = 0, wpos_ = 0, sinceHop_ = 0;
    int64_t t_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chan, 2> ch_;
    std::vector<double> w_, win_, e_, med_, tmp_;
    double a_[kOrder + 1] = {}, r_[kOrder + 1] = {};
    int lo_ = 0, hi_ = 0;
    ArWork work_;
};

}  // namespace sw::rs04
