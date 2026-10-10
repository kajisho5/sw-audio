// SW VO05 Rider — rides the vocal against the music (spec: 仕様書 v1.0「VO05 Rider」). The music comes in on the external sidechain.
// Vocal level: K-weighted, 150 Hz .. 5 kHz, 200 ms exponential mean square, updated only while the vocal is active (a 40 ms mean square of the raw input decides the gate and the breath skip). Music level: K-weighted, full band, 3 s window (the music's level, not its peaks).
// Wanted ride = clamp((music + Target) - vocal, -Range, +Range) dB; the ride moves toward it with Sensitivity (Low / Mid / High: time constant 2 s / 0.8 s / 0.3 s, dead band
// 1.5 / 0.75 / 0.25 dB: the ride starts moving when the error exceeds it, and settles on the wanted value). The ride holds when the vocal is below -50 dBFS, when no music is heard (no sidechain, or the music below -50 dBFS: "Music: not listening"), and with Breath skip On
// when the vocal is more than 15 dB below its recent peak (the peak hold falls 6 dB/s: a breath between phrases is not lifted).
// Write automation On: Ride is the product's own output (MS05 mechanism: takeParamWrite); Off: the Ride parameter (host automation) is the gain. Latency 0. No Output / Mix / Δ (spec).
#pragma once
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::vo05 {

enum ParamId { Target, Range, Sensitivity, BreathSkip, Ride, Write, MusicFrom, kNumParams };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { gainFrom_ = gainTo_ = std::pow(10.0, rideDb_ / 20.0); }
    void process(float** ch, int numCh, int n) { processWithSidechain(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh);
    int latencySamples() const { return 0; }
    double rideDb() const { return rideDb_; }
    bool listening() const { return listening_; }       // "Music: Listening"
    double vocalLufs() const;
    double musicLufs() const;
    // SW Link: where the music comes from (Music from: 0 the sidechain, 1 all the other SW AUDIO instances, 2.. the instance of one product): the plugin layer calls musicProduct() to know what to ask SW Link
    // for, and hands the loudness it found (short-term LUFS of that instance's output, or of all the others added) to setLinkedMusic() before every block. With Music from >= 1 the sidechain is not listened to.
    static const char* musicProduct(int step);        // nullptr: the sidechain; "*": all the others; otherwise the product code
    bool musicFromLink() const { return target_[MusicFrom] >= 0.5; }
    int musicFrom() const { return static_cast<int>(target_[MusicFrom] + 0.5); }
    void setLinkedMusic(bool valid, double lufs) { linkValid_ = valid; linkMs_ = valid ? std::pow(10.0, (lufs + 0.691) / 10.0) : 0.0; }
    // plugin layer: bit 0 = begin gesture, bit 1 = value (plain dB), bit 2 = end gesture; 0 = nothing to report
    int takeParamWrite(int& id, double& plain);

private:
    static constexpr int kControl = 64;
    void control(int nch);
    int ph_ = 0;                                  // samples into the control block (the grid is the stream's, not the host block's)
    double vSum_ = 0, rawSum_ = 0, mSum_ = 0;     // the sums of the control block being played
    bool scPresent_ = false;
    double fs_ = 48000.0, rideDb_ = 0, gainFrom_ = 1.0, gainTo_ = 1.0, vocMs_ = 0, vocRaw_ = 0, musMs_ = 0, vocC_ = 0, fastC_ = 0, musC_ = 0, peakDb_ = -200, sent_ = 0;
    long musAge_ = 0, vocAge_ = 0;
    std::array<double, kNumParams> target_{};
    std::array<KWeighting, 2> kv_{}, km_{};
    std::array<std::array<Svf, 2>, 2> hp_{}, lp_{};
    bool open_ = false, dirty_ = false, listening_ = false, moving_ = false, linkValid_ = false;
    double linkMs_ = 0;                            // the music (mean square, K-weighted) SW Link gave
};

}  // namespace sw::vo05
