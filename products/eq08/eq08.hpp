// SW EQ08 Linear — 24-band EQ: Linear (FIR from the combined magnitude, partitioned FFT convolution),
// Minimum (TPT SVF IIR, zero latency), Mixed (below 200 Hz minimum phase, above linear). spec: 仕様書 v1.0「EQ08 Linear」
// Kernel: 2048 taps at 48 kHz (scaled with fs), latency = L/2 + one 128-sample block. New kernels crossfade over 20 ms.
// EVO Pre-ring guard: bands whose own linear kernel rings more than 3 ms ahead of the peak above -60 dB go minimum phase.
#pragma once
#include "sw/convolver.hpp"
#include "sw/copy_atomic.hpp"
#include "sw/fir_design.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include "sw/worker.hpp"
#include <array>
#include <thread>
#include <vector>

namespace sw::eq08 {

constexpr int kBands = 24;
enum BandField { On, Type, Freq, Gain, Q, kPerBand };
enum ParamId { Phase = kBands * kPerBand, Guard, Ms, Output, Length, kNumParams };  // Length appended (ids stay stable)
enum PhaseMode { Linear, Minimum, Mixed };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;  // desired (Phase); applied at prepare
    int kernelLength() const { return L_; }

    // The Linear / Mixed kernel is designed on a background thread when asked (the plug-in layer does; spec: the recalculation runs on another thread): the audio thread copies the parameters, kicks the
    // thread and takes the finished kernel at a later block, when the crossfade of the last one is over. Without it (the default: tests, offline) the design runs in process() as before.
    // Call before prepare(); the thread lives from prepare() to the next prepare() or the end (no thread in Minimum mode, which has no kernel).
    void useWorker(bool on) { wantWorker_ = on; }
    // a bounce runs faster than real time: offline, process() waits for the design it asked for (so the kernel takes over at the same sample in every run); realtime, it does not wait (the default)
    void setOffline(bool on) { offline_ = on; }
    bool workerRunning() const { return job_.running(); }   // (a copy of a Processor has no thread: it designs in process() until its own prepare())
    void waitKernel() { job_.waitIdle(); }                   // not the audio thread: returns when no design is on its way (the next process() takes a finished one)
    int kernelsApplied() const { return applied_.load(); }   // kernels handed to the convolver since prepare()
    std::thread::id lastDesignThread() const { return lastThread_; }   // (tests: which thread ran the last design)

private:
    struct GuardCache { double key[4] = {-1, -1, -1, -1}; bool guard = false; };
    // a kernel design: a copy of the parameters, the tables and the work arrays; the audio thread owns `sync_`, the worker thread `wd_` (they are handed over by st_)
    struct Design {
        std::array<double, kNumParams> t{};
        double fs = 48000.0; int mode = Linear;
        FirDesigner designer;
        std::vector<BandShape> lin, mn;
        std::array<GuardCache, kBands> cache{};
        const std::vector<double>* result = nullptr;
        std::thread::id ranOn;
        void prepare(int L) { designer.prepare(L); lin.reserve(kBands); mn.reserve(kBands); cache = {}; result = nullptr; }
        bool active(int b) const { return t[static_cast<size_t>(b * kPerBand + On)] > 0.5; }
        BandShape shape(int b) const;
        bool guarded(int b);
        const std::vector<double>& run();   // the kernel of `t` (the designer's own buffer: valid until the next run)
    };
    BandShape shape(int b) const;
    bool active(int b) const { return target_[static_cast<size_t>(b * kPerBand + On)] > 0.5; }
    void rebuildKernel(bool immediate);
    void updateIir(int ramp);
    static int kernelLengthFor(double fs, double base);
    double fs_ = 48000.0;
    int L_ = 2048, B_ = 128, mode_ = Linear, sinceKernel_ = 0, fadeLen_ = 960, dpos_ = 0;
    bool dirty_ = false, iirDirty_ = false, prepared_ = false;
    std::array<double, kNumParams> target_{};
    Convolver conv_;
    Design sync_, wd_;                           // the design's tables and work arrays (nothing is allocated when a knob moves)
    std::array<std::array<Svf, kBands>, 2> iir_{};
    std::vector<float> sideDelay_;
    std::vector<float> scratch_;
    bool wantWorker_ = false;
    CopyAtomic<int> st_{0};                      // the worker's hand-over: 0 nothing on its way, 1 asked (wd_.t is the request), 2 done (wd_.result is the kernel)
    CopyAtomic<int> applied_{0};
    CopyAtomic<bool> offline_{false};            // (set from the main thread, read by the audio thread)
    std::thread::id lastThread_;
    BackgroundWork job_;                         // (last: the thread is joined before the rest goes)
};

}  // namespace sw::eq08
