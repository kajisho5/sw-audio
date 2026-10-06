// SW MS05 Leveler — automatic fader that writes its rides as host automation (spec: 仕様書 v1.0「MS05 Leveler」)
// Detection: K-weighted short-term loudness of the input (Source: Vocal = 150 Hz..5 kHz, Mix = full band, Bass = below 250 Hz with
// a longer window), gated below Gate. Ride (dB) moves toward clamp(Target - loudness, -Range, +Range) with Speed (3 s / 1 s / 0.3 s).
// Write automation On: Ride is the product's own output; takeParamWrite() hands the plugin layer the begin / value / end of a
// host gesture. Off: the Ride parameter (host automation) is the gain. Latency 0.
#pragma once
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::ms05 {

enum ParamId { Target, Range, Speed, Gate, Source, Ride, Write, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { gainFrom_ = gainTo_ = std::pow(10.0, rideDb_ / 20.0); }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double rideDb() const { return rideDb_; }
    // plugin layer: bit 0 = begin gesture, bit 1 = value (plain dB), bit 2 = end gesture; 0 = nothing to report
    int takeParamWrite(int& id, double& plain);

private:
    static constexpr int kControl = 64;
    void updateSource();
    double fs_ = 48000.0, rideDb_ = 0, gainFrom_ = 1.0, gainTo_ = 1.0, msLoud_ = 0, msRaw_ = 0, msC_ = 0, sent_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<KWeighting, 2> k_{};
    std::array<std::array<Svf, 2>, 2> hp_{}, lp_{};
    bool open_ = false, dirty_ = false;
};

}  // namespace sw::ms05
