// SW GT04 Bass Amp — preamp, four-band EQ, overdrive, small cabinet, and a DI blend (spec: 仕様書 v1.0「GT04 Bass Amp」)
//   amp path:  in -> 20 Hz high-pass -> preamp (2x OS biased tanh; Gain = drive and +-6 dB level) -> EQ: Low shelf 80 Hz, Lo mid (Mid Hz / 2), Hi mid (Mid Hz), High shelf 3.5 kHz
//              -> LR4 split at 150 Hz: lows stay clean, highs go through the overdrive (2x OS) -> cabinet (high-pass 35 Hz, +2 dB at 90 Hz, 6 kHz low-pass) -> Master
//   DI path:   in -> 35 Hz high-pass (as the cabinet's) -> the crossover's all-pass (LR4 low + high at 150 Hz, the same as the amp's) -> delay + first-order all-pass chosen so that the total phase at 150 Hz
//              equals the amp path's (EVO Phase align) -> blend
// The amp path's linear phase is measured with a small probe tone through a copy of the chain whenever an EQ setting changes.
#pragma once
#include "sw/param.hpp"
#include "sw/oversample.hpp"
#include "sw/shaper.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <complex>
#include <vector>

namespace sw::gt04 {

enum ParamId { Gain, Drive, Master, Low, LoMid, HiMid, High, MidHz, Di, DiBlend, PhaseAlign, Oversample, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double diDelaySamples() const { return tau_; }   // the delay the DI path gets at 150 Hz (samples)

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    struct Chain {   // one channel of the amp path (index 2 is the measuring copy)
        Svf hp20, low, lomid, himid, high, cabHp, cabBell;
        Lr4 split_lo, split_hi, cabLp;
        BiasShaper2x pre, drv;
        OsSwitch loOs;   // the clean low band goes through the same half-bands as the shaped branch: same delay
        double process(double x, double gPre, double hPre, double gDrv, double hDrv, double levelLin);
    };
    void updateEq();
    void applyOversample(int factor);
    void updateStatic();
    void measure();
    double fs_ = 48000.0, tau_ = 0.0, apA_ = 0.0;
    int delayN_ = 0, dpos_ = 0;
    bool prepared_ = false, eqDirty_ = false;
    std::array<double, kNumParams> target_{};
    std::array<Chain, 3> chain_{};
    std::array<std::vector<double>, 2> dline_{};
    std::array<Lr4, 2> diLo_{}, diHi_{};   // the DI path's copy of the amp's crossover all-pass
    std::array<Svf, 2> diHp_{};            // and of the cabinet's 35 Hz high-pass (the DI loses the same sub-bass, so the phase can follow)
    std::array<double, 2> apX_{}, apY_{};
    LinearSmoother master_, blend_;
};

}  // namespace sw::gt04
