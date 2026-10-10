// SW EQ02 Surgical — 24-band surgical EQ (spec: 仕様書 v1.0「EQ02 Surgical 進化版」)
// Zero latency: TPT SVF with bandwidth pre-warp against cramping; cuts 6..96 dB/oct as Butterworth cascades.
// Natural: Zero latency + a 65-tap phase-only FIR that moves the phase toward the analog prototype (latency 32).
// Linear: FIR from the target magnitude on the shared engine (same as EQ08), latency L/2 + 128.
// Per band: dynamic range / threshold (band-limited detector), Stereo / Mid / Side placement when Mid/side is on.
// Assist (EVO B): setAssist(true) listens to the input (before the EQ) and resonances() gives the steady peaks that stick out of their surroundings (sw/resonance.hpp). Unmask needs SW Link.
#pragma once
#include "sw/bandfilter.hpp"
#include "sw/convolver.hpp"
#include "sw/copy_atomic.hpp"
#include "sw/fir_design.hpp"
#include "sw/param.hpp"
#include "sw/resonance.hpp"
#include "sw/worker.hpp"
#include <array>
#include <complex>
#include <thread>
#include <vector>

namespace sw::eq02 {

constexpr int kBands = 24;
enum BandField { On, Type, Freq, Gain, Q, Slope, Place, DynRange, DynThresh, kPerBand };
enum ParamId { PhaseMode = kBands * kPerBand, Ms, Output, Length, kNumParams };  // Length appended (ids stay stable)
enum Phase { ZeroLatency, Natural, Linear };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    void reset();                 // the host stopped or jumped: forget the audio (filters, detectors, convolver history), keep the kernels, the parameters and the worker thread
    int latencySamples() const;
    // Assist: the resonances of the input, strongest first: Hz and how many dB they stick out (averaged over seconds). Audio thread (the screen's button is queued to it); the analysis only runs while it is on.
    void setAssist(bool on) { if (on && !assist_) res_.reset(); assist_ = on; }
    bool assist() const { return assist_; }
    int resonances(ResonanceFinder::Mark* out) const { return assist_ ? res_.marks(out) : 0; }

    // The Linear kernels (one per path) are designed on a background thread when asked (the plug-in layer does; spec: the recalculation runs on another thread): the audio thread copies the parameters,
    // kicks the thread and takes the finished kernels at a later block, when the crossfade of the last ones is over. Without it (the default: tests, offline) the design runs in process() as before.
    // Call before prepare(); the thread lives from prepare() to the next prepare() or the end (only in Linear mode: the other modes have no kernel).
    void useWorker(bool on) { wantWorker_ = on; }
    // a bounce runs faster than real time: offline, process() waits for the design it asked for (so the kernel takes over at the same sample in every run); realtime, it does not wait (the default)
    void setOffline(bool on) { offline_ = on; }
    bool workerRunning() const { return job_.running(); }   // (a copy of a Processor has no thread: it designs in process() until its own prepare())
    void waitKernel() { job_.waitIdle(); }                   // not the audio thread: returns when no design is on its way (the next process() takes a finished one)
    int kernelsApplied() const { return applied_.load(); }   // kernel sets handed to the convolvers since prepare()
    std::thread::id lastDesignThread() const { return lastThread_; }   // (tests: which thread ran the last design)

private:
    // a kernel design: a copy of the parameters, the tables and the work arrays; the audio thread owns `sync_`, the worker thread `wd_` (they are handed over by st_)
    struct Design {
        std::array<double, kNumParams> t{};
        double fs = 48000.0;
        FirDesigner designer;
        std::vector<BandShape> lin;
        std::array<std::vector<double>, 2> kernel;
        std::thread::id ranOn;
        void prepare(int L) { designer.prepare(L); lin.reserve(kBands); for (auto& k : kernel) k.assign(static_cast<size_t>(L), 0.0); }
        void run();   // the kernel of each path for `t`
    };
    static constexpr int kNatDelay = 32, kNatTaps = 2 * kNatDelay + 1, kControl = 16;
    double t(int b, int f) const { return target_[static_cast<size_t>(b * kPerBand + f)]; }
    bool active(int b) const { return t(b, On) > 0.5; }
    bool onPath(int b, int p) const;
    bool dynamic(int b) const;
    BandShape shape(int b, double gainOffset = 0.0) const;
    void configure(int ramp);
    void buildNatural();
    void buildLinear(bool immediate);
    static int kernelLengthFor(double fs, double base);
    double fs_ = 48000.0;
    int mode_ = ZeroLatency, L_ = 2048, sinceKernel_ = 0, fadeLen_ = 960, natPos_ = 0;
    bool prepared_ = false, dirty_ = true, kernelDirty_ = true, assist_ = false;
    ResonanceFinder res_;
    std::array<double, kNumParams> target_{};
    struct Band {
        std::array<BandFilter, 2> f{};      // per path: the band (Zero latency / Natural) or its dynamic part (Linear)
        std::array<Svf, 2> det{};           // band-limited detector per path
        std::array<double, 2> env{}, offset{};
    };
    std::array<Band, kBands> band_{};
    std::array<Convolver, 2> conv_{};
    Design sync_, wd_;                           // the design's tables and work arrays (nothing is allocated when a knob moves)
    Fft natFft_{256};
    std::vector<std::complex<double>> natC_ = std::vector<std::complex<double>>(256);
    std::array<std::vector<double>, 2> nat_{}, natHist_{};
    double atk_ = 0, rel_ = 0;
    bool wantWorker_ = false;
    CopyAtomic<int> st_{0};                      // the worker's hand-over: 0 nothing on its way, 1 asked (wd_.t is the request), 2 done (wd_.kernel)
    CopyAtomic<int> applied_{0};
    CopyAtomic<bool> offline_{false};            // (set from the main thread, read by the audio thread)
    std::thread::id lastThread_;
    BackgroundWork job_;                         // (last: the thread is joined before the rest goes)
};

}  // namespace sw::eq02
