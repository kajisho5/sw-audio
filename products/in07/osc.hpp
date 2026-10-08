// SWINGBY (SW IN07) — the oscillator sources besides the analog shapes: a wavetable bank, the FM helpers and a small sample bank.
//   Everything is generated in code (no audio files), once per process, the first time a synth is prepared (not on the audio thread).
//   Wavetables: 8 tables x 16 frames, each frame defined by its harmonics (1..1024; some drawn as one cycle at 8192 points and analysed),
//   normalised to an RMS of 0.5 (-6 dBFS) so every table and position plays at about the same level. Each frame is stored at 20 mip levels,
//   half an octave apart (1024, 724, 512, ... 2, 1 harmonics), each as long as 4 x its harmonics (256 .. 2048 points), read with 4-point cubic
//   (Hermite) interpolation (linear left images at -59 dB for the brightest frame high up; see README);
//   a copy plays the richest level with no harmonic above Nyquist at its pitch (strict: nothing folds back, the top is lost by at most half an octave).
//   FM: two sine operators (carrier, modulator at a ratio), the index (0..10 rad) with its own decay; the index is held under the level where the first
//   sideband past Nyquist stays under 0.002 (-54 dB; an upper and a lower one can fold together: -51 dB), (I/2)^k / k! <= 0.002 for the k-th sideband
//   (the series' first term: J_k is smaller still). Feedback is limited by pitch and index too (fmFeedbackLimit).
//   Samples: 5 loops (2 s, crossfaded seams) and 3 one-shots at 48 kHz, the root at C4 (key 60). Stored at 5 levels (48 kHz and 4 octaves down, each
//   low-passed under its Nyquist): a note reads the level whose top lands under fs - 18 kHz (what folds back stays above 18 kHz).
#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace sw::in07 {

enum OscTypeId { OscAnalog = 0, OscWavetable = 1, OscFm = 2, OscSample = 3 };
constexpr int kWaveTables = 8;
constexpr int kSamples = 8;
constexpr int kFmRatios = 12;
extern const char* const kWaveTableNames[kWaveTables];
extern const char* const kSampleNames[kSamples];
extern const char* const kFmRatioLabels[kFmRatios];
double fmRatioOf(int index);                     // the modulator's frequency over the carrier's: 0.5, 1, 1.5, 2, 3, 4, 5, 6, 7, 9, 11, 14
constexpr double kFmIndexMax = 10.0;             // radians at 100 %
constexpr double kFmFeedbackMax = 1.3;           // radians at 100 % (the modulator's own feedback, the average of its last two outputs)
double fmIndexLimit(double carrierInc, double ratio);   // the highest index (radians) that keeps the sidebands past Nyquist under -50 dB together
// the highest feedback (radians) for this pitch and index: feedback gives the modulator harmonics of its own, which widen every sideband set.
// Fitted under measurements of the operator pair (ratios 1 and 3, C3..C8, index 0.5..10 rad; the largest feedback whose output keeps the
// non-harmonic power under -50 dB): 0.26 (y - 0.5) with y = room / (I + 1)^1.5, room = (fs/2 - fc) / fm, and less again under 8 harmonics of room.
double fmFeedbackLimit(double carrierInc, double ratio, double index);

struct WaveBank {
    static constexpr int kFrames = 16, kLevels = 20, kMaxHarmonics = 1024, kGuard = 3;   // one wrap point before the cycle, two after
    std::array<int, kLevels> size{}, harmonics{};
    std::array<size_t, kLevels> offset{};
    size_t stride = 0;                           // floats per frame (all its levels, each with kGuard wrap points); offset = the cycle's first point
    std::vector<float> data;
    int level(double inc) const {                // the richest level with every harmonic at or under Nyquist (inc = f / fs)
        const double hmax = inc > 0.0 ? 0.5 / inc : 1e9;
        for (int l = 0; l < kLevels; ++l) if (harmonics[static_cast<size_t>(l)] <= hmax) return l;
        return kLevels - 1;
    }
    const float* at(int table, int frame, int lev) const {
        return data.data() + (static_cast<size_t>(table) * kFrames + static_cast<size_t>(frame)) * stride + offset[static_cast<size_t>(lev)];
    }
};
const WaveBank& waveBank();

struct SampleBank {
    static constexpr int kLevels = 5, kTail = 96;  // a one-shot ends kTail level-0 samples after its length (the decimation filters' smear)
    static constexpr double kRate = 48000.0, kRootHz = 261.6255653005986;   // C4
    struct Sample {
        bool loop = false;
        int length = 0;                          // level-0 samples (a loop's is a multiple of 16)
        std::array<std::vector<float>, kLevels> lv;   // level j at kRate / 2^j; a loop's ends with its first samples again, a one-shot's with zeros
    };
    std::array<Sample, kSamples> s;
    static int level(double speed, double fs) {  // speed: 1 = the sample's own pitch
        const double top = fs - 18000.0 > 0.5 * fs ? fs - 18000.0 : 0.5 * fs, f = 21600.0 * speed;
        int j = 0;
        while (j < kLevels - 1 && f > top * static_cast<double>(1 << j)) ++j;
        return j;
    }
};
const SampleBank& sampleBank();

// sin(2 pi x) for any x (cycles) from a 4096-point table with linear interpolation (within 3e-7)
const float* sineTable();
inline double sinCycles(const float* t, double x) {
    x -= static_cast<double>(static_cast<long long>(x)) - (x < 0.0 ? 1.0 : 0.0);   // floor without the call
    const double i = x * 4096.0;
    const int k = static_cast<int>(i);
    return t[k] + (i - k) * (t[k + 1] - t[k]);
}

}  // namespace sw::in07
