// SW LV14 Align — speaker time alignment (spec: 仕様書 v1.0「LV14 Align」). A delay line of 0..500 ms (Skew k = 2, 0.01 ms steps; fractional delays are linearly interpolated) with an optional polarity flip.
//   **Reported delay: 0** (the delay is the effect: the host must not compensate it). Changes of Delay glide (one pole, 50 ms) instead of jumping, so a moved fader does not click.
//   Distance (display): Delay x the speed of sound, 331.5 + 0.6 x Air temp m/s (distanceM()). Air temp -10..40 degC (22).
//   Measure (EVO, class B; a button, not a parameter): the main system (the reference, e.g. pink noise sent to the main speakers) arrives on the second (sidechain) input, the measurement mic on the main input.
//   startMeasure() collects 3 s of both (mono sums); analyse() (not real time: the screen calls it) takes the lag 0..500 ms with the largest cross-correlation (FFT, refined with a parabola on the peak), needs a normalised peak of 0.1 and
//   at least 2x the next largest one away from it; on success takeParamWrite() hands the lag to the host as the Delay value (the delay that makes this speaker arrive together with the main system).
#pragma once
#include "sw/copy_atomic.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::lv14 {

enum ParamId { Delay, AirTemp, Polarity, kNumParams };
enum MeasureState { Idle = 0, Collecting = 1, Ready = 2, Done = 3, Failed = 4 };
constexpr double kMaxDelayMs = 500.0, kCollectSeconds = 3.0;

const std::vector<ParamSpec>& specs();
double speedOfSound(double tempC);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) cur_ = target_[Delay] * 0.001 * fs_; }
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    double distanceM() const { return target_[Delay] * 0.001 * speedOfSound(target_[AirTemp]); }
    // Measure
    void startMeasure();
    int measureState() const { return state_.load(); }
    bool analyse();
    double foundMs() const { return foundMs_; }
    double confidence() const { return confidence_; }
    int takeParamWrite(int& id, double& plain);

private:
    double fs_ = 48000.0, cur_ = 0.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    size_t mask_ = 0, pos_ = 0;
    CopyAtomic<int> state_{Idle}; size_t collected_ = 0, need_ = 0;   // state_, the results and writePending_ are shared with the screen's thread (analyse() runs there)
    std::vector<float> capRef_, capMic_;
    CopyAtomic<double> foundMs_{0.0}, confidence_{0.0}; CopyAtomic<bool> writePending_{false};
};

}  // namespace sw::lv14
