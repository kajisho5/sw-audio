// SW SA01 Tape — tape recorder saturation, head bump, wow / flutter, hiss (spec: 仕様書 v1.0「SA01 Tape」)
// Input -> simplified hysteresis saturation (2x OS) -> variable delay (fixed centre 1 ms = 48 samples @48 kHz, so the reported latency
// never changes) -> Repro (head bump + high-frequency loss by speed) -> hiss -> Output. EVO Calibrate: 5 s of input sets Input so the
// mean level sits at 0 VU = -18 dBFS (startCalibrate(); the plugin layer writes the result back with takeParamWrite).
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::sa01 {

enum ParamId { Speed, Formula, Input, Saturation, Wow, Flutter, Hiss, Output, Repro, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { in_.skip(1 << 30); out_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    void startCalibrate();
    bool calibrating() const { return calLeft_ > 0; }
    double inputDb() const { return target_[Input]; }
    int takeParamWrite(int& id, double& plain);   // bit 0 begin, 1 value, 2 end (plugin layer)

private:
    static constexpr int kRing = 4096;
    void updateTone();
    double hermite(const std::vector<float>& r, double pos) const;
    double fs_ = 48000.0;
    int centre_ = 48, pos_ = 0; unsigned driftCount_ = 0;
    std::array<double, kNumParams> target_{};
    LinearSmoother in_, out_;
    std::array<Oversampler2x, 2> os_{};
    std::array<double, 2> z_{}, zOs_{};
    double envC_ = 0;
    std::array<double, 2> envOs_{};
    std::array<std::vector<float>, 2> ring_;
    double wowPh_[2] = {0, 0}, flPh_[3] = {0, 0, 0}, drift_ = 0, driftTarget_ = 0;
    struct ToneCh { Svf bump, loss; };
    std::array<ToneCh, 2> tone_{};
    uint32_t rng_ = 0x2468ace1u;
    double hissA_ = 0, hissLp_[2] = {0, 0}, hissNorm_ = 1.0;
    long calLeft_ = 0; double calSum_ = 0; long calCount_ = 0; bool calPending_ = false;
};

}  // namespace sw::sa01
