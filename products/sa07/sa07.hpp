// SW SA07 Lo-Fi — records, old tapes and radios (spec: 仕様書 v1.0「SA07 Lo-Fi」)
// Input -> wow (sw::WobbleDelay, fixed 1 ms = 48 samples centre) -> low cut by era -> Bandwidth (LR4 low-pass, 3..20 kHz) -> Mono (side fold-in)
// -> + Crackle (random pops that ring at 4 kHz) + Dust (sparse single-sample grains). The random parts are seeded at prepare, so a render
// is the same every time. EVO Era: choosing an era writes Crackle, Wow and Bandwidth together (through takeParamWrite, three parameters);
// a later manual change of any of them wins. The Mix is the shared frame's.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include "sw/wobble.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::sa07 {

enum ParamId { Era, Crackle, Dust, Wow, Bandwidth, Mono, Mix, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { writes_.clear(); }   // values just loaded: an Era in the loaded state must not overwrite the other values
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    int takeParamWrite(int& id, double& plain);   // bit 0 begin, 1 value, 2 end; one parameter per call
    double crackle() const { return target_[Crackle]; }

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    void applyParam(int id, double v);
    void updateFilters();
    double rnd(int ch);
    double fs_ = 48000.0, popRing_ = 0;
    std::array<double, kNumParams> target_{};
    WobbleDelay wob_;
    struct Ch { Svf hp; Lr4 lp; Svf pop; uint32_t rng = 1; };
    std::array<Ch, 2> c_{};
    std::vector<std::pair<int, double>> writes_;
    bool begun_ = false;
};

}  // namespace sw::sa07
