// SW SA02 Console Sum — analog console summing colour (spec: 仕様書 v1.0「SA02 Console Sum」)
// Input -> per-instance deviation (seed) -> drive into a split saturation (lows and the rest on separate biased shapers, 2x OS) with the
// colour's tone -> crosstalk -> width -> console noise -> Output. Group: instances with the same group number in one process load each
// other (their summed level adds up to +3 dB of drive), like channels on one bus. The seed (hidden state, saved with the project)
// is created with the instance (and kept across prepare); the plugin layer stores it in the state (saveExtra / loadExtra).
#pragma once
#include "sw/param.hpp"
#include "sw/shaper.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace sw::sa02 {

enum ParamId { Color, Drive, Crosstalk, Noise, Width, Output, Group, Oversample, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();   // movable (the group slot moves with it), not copyable
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { out_.skip(1 << 30); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    // the instance's seed is hidden state: saved with the project through the plugin layer's extra-state block
    int seed() const { return seed_; }
    void setSeed(int seed) { seed_ = std::clamp(seed, 1, 65535); applySeed(); }
    void saveExtra(std::vector<uint8_t>& out) const { out = {static_cast<uint8_t>(seed_ & 255), static_cast<uint8_t>(seed_ >> 8)}; }
    void loadExtra(const uint8_t* d, size_t n) { if (n >= 2) setSeed(d[0] | (d[1] << 8)); }

private:
    struct Slot {   // this instance's place in its group's registry (process-wide); released when the instance goes away
        int group = 1, idx = -1;
        Slot() = default;
        Slot(Slot&& o) noexcept : group(o.group), idx(o.idx) { o.idx = -1; }
        Slot& operator=(Slot&& o) noexcept { release(); group = o.group; idx = o.idx; o.idx = -1; return *this; }
        ~Slot() { release(); }
        void release();
    };
    void applySeed();
    void joinGroup();
    void leaveGroup();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    LinearSmoother out_;
    BiasShaper2x lf_, hf_;
    std::array<double, 2> lp_{};
    double lpA_ = 0;
    struct Tone { Svf shelf, bump, lowpass; };
    std::array<Tone, 2> tone_{};
    // per-instance deviation (from the seed)
    std::array<double, 2> devGain_{}, devDrive_{};
    int unit_ = 0;   // Unit A / B / C (sw/unit.hpp): a fixed set of tolerances on top of the instance's own spread - slots 0 shelf, 1 bump, 2 low-pass; the saturation
    double devNoise_ = 0;
    int seed_ = 0;
    Slot slot_;
    uint32_t rng_ = 0x9e3779b9u;
    double msSmooth_ = 0, loadDb_ = 0, msC_ = 0;
};

}  // namespace sw::sa02
