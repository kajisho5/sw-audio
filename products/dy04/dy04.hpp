// SW DY04 Gate — 500-series gate / expander / ducker (spec: 仕様書 v1.0「DY04 Gate」, ranges: 04_parameters)
// Key = main input or the external sidechain, through optional key HPF / LPF. Listen outputs the key.
#pragma once
#include "sw/bleed_learner.hpp"
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dy04 {

enum ParamId { Threshold, Range, Attack, Hold, Release, Mode, KeyHpf, KeyLpf, KeyHpfHz, KeyLpfHz, Listen, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const { return 0; }
    bool isOpen() const { return gate_.isOpen(); }
    // Learn (EVO, class B; the same learner as CS02's): while it listens (at most kLearnSeconds) the key's onsets are measured; when it stops, whether you press again or the time is up, the Threshold goes between the wanted
    // hits and the bleed and the Key HPF / LPF frequencies to the edges of the hits' band (not the Key HPF / LPF switches). The core writes them to the host (takeParamWrite: bit 0 begin, 1 value, 2 end). Audio thread.
    static constexpr double kLearnSeconds = 30.0;
    void learn();                      // starts it; while it listens, stops it and applies what it heard
    bool learning() const { return learner_.learning(); }
    double learnProgress() const { return learner_.progress(); }
    int learnOnsets() const { return learner_.onsets(); }
    bool learnedOk() const { return learnedOk_; }   // the last Learn found two groups
    int takeParamWrite(int& id, double& plain);

private:
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void applyGate();
    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    void applyLearned(const BleedLearner::Result& r);
    GateEngine gate_;
    std::array<Svf, 2> hp_{}, lp_{};
    BleedLearner learner_;
    std::vector<float> keyMono_;
    std::array<std::pair<int, double>, 3> writes_{};
    int nWrites_ = 0, writeAt_ = 0;
    bool wasLearning_ = false, learnedOk_ = false;
};

}  // namespace sw::dy04
