// SW EQ05 Console — 4-band console EQ (spec: 仕様書 v1.0 「EQ05 Console」)
// Signal per channel: [Drive Pre] -> HPF(18 dB/oct) -> LPF(12 dB/oct) -> LF -> LMF -> HMF -> HF -> [Drive Post]
// Output and In are applied by sw::Shell (common frame), after Auto gain as the spec orders.
#pragma once
#include "sw/band_spectrum.hpp"
#include "sw/copy_atomic.hpp"
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/saturate.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include "sw/unit.hpp"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace sw::eq05 {

enum ParamId {
    HfGain, HfFreq, HfShape, HmfGain, HmfFreq, HmfQ, LmfGain, LmfFreq, LmfQ,
    LfGain, LfFreq, LfShape, Hpf, Lpf, Drive, DrivePos, Output, In, Oversample, Unit, kNumParams
};

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);       // takes effect smoothly
    double getParam(int id) const { return target_[static_cast<size_t>(id)]; }
    void snapToTargets();                           // jump to targets (after state load)
    void process(float** ch, int numCh, int n);     // in place, 1 or 2 channels
    int latencySamples() const { return 0; }

    // Match (EVO, class B): the reference comes from the screen as mono float samples in base64 pieces (refBegin / refAppendBase64 / refCommit: GUI thread, which analyses it); match() (audio thread) listens to the input
    // until it has heard 10 s of playing (or cancels); then needsFit() is true and fit() (GUI thread) fits the knobs to the difference of the two long-term spectra by least squares, within the knobs' ranges; the audio
    // thread applies the values and hands them to the host (takeParamWrite: bit 0 begin, 1 value, 2 end).
    void refBegin(double rate);
    bool refAppendBase64(const char* text);
    bool refCommit();
    void refClear();
    bool hasReference() const { return refReady_.load(); }
    void match();
    bool matching() const { return listening_.load(); }
    double matchProgress() const { return progress_.load(); }
    bool needsFit() const { return fitPending_.load(); }
    void fit();
    int takeParamWrite(int& id, double& plain);
    double matchBefore() const { return before_.load(); }     // the RMS of the difference of the two tone curves (dB, level taken out) before and after the fit
    double matchAfter() const { return after_.load(); }

private:
    struct Chain {
        Svf hfShelf, hfBell, hmf, lmf, lfShelf, lfBell, hpf2, lpf;
        OnePole hpf1;
        OsSwitch os;
        double onset = 1.0;   // Unit A / B / C: where the saturation sets in (a factor on the drive gain)
    };
    struct Ctl {  // smoothed control values shared by both channels
        LinearSmoother hfGain, hfFreqN, hmfGain, hmfFreqN, hmfQN, lmfGain, lmfFreqN, lmfQN,
            lfGain, lfFreqN, hpfFreqN, lpfFreqN, drive;
        LinearSmoother hfBell, lfBell, hpfOn, lpfOn;  // 10 ms crossfades
    };
    void updateCoefficients(int rampSamples = 0);
    int unit_ = 0;   // Unit A / B / C (sw/unit.hpp): slots 0 HF, 1 HMF, 2 LMF, 3 LF, 4 high-pass, 5 low-pass, and the drive
    double eq(Chain& c, double x) const;
    double drive(Chain& c, double x) const;
    double runChain(Chain& c, double x, int pos) const;
    bool anySmoothing() const;

    double fs_ = 48000.0;
    std::array<double, kNumParams> target_{};
    Ctl ctl_;
    std::array<Chain, 2> ch_{}, fade_{};
    Saturator sat_;
    int drivePos_ = 1, fadePos_ = 1, fadeRemaining_ = 0, fadeLength_ = 480;
    double hfBell_ = 0, lfBell_ = 0, hpfOn_ = 0, lpfOn_ = 0;
    // Match
    static constexpr int kMatchParams = 12;                // HF / HMF / LMF / LF: gain, freq, (Q or shape)
    BandSpectrum input_;
    std::array<double, BandSpectrum::kBands> inBands_{}, refBands_{};
    CopyAtomic<bool> listening_{false}, fitPending_{false}, refReady_{false}, resultReady_{false};
    CopyAtomic<double> progress_{0.0}, before_{0.0}, after_{0.0};
    std::vector<uint8_t> refBytes_; double refRate_ = 48000.0; bool refOpen_ = false, refBroken_ = false;
    std::array<std::pair<int, double>, kMatchParams> result_{}, writes_{};   // result_: by the GUI thread (resultReady_), writes_: the queue of the audio thread
    int nWrites_ = 0, writeAt_ = 0;
    void listen(float** ch, int numCh, int n);
    void applyMatch();
};

}  // namespace sw::eq05
