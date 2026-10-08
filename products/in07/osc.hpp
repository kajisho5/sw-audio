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
//   Samples: 5 loops (2 s, crossfaded seams) and 3 one-shots made at 48 kHz, the root at C4 (key 60). Stored at 5 levels (an octave apart), each
//   2x oversampled (its content under 0.21 of the stored rate, nothing above 0.25: 191-tap filters) and read with an 8-tap Kaiser-windowed sinc
//   (512 phases, interpolated): the images of the read stay under -70 dB at any speed. A note reads the richest level whose content, at its
//   speed, folds back no lower than 18 kHz, with the read at most 3 stored samples per output sample.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
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
    static constexpr int kLevels = 5, kTail = 96;  // a one-shot ends kTail samples (at kRate) after its length (the filters' smear)
    static constexpr int kPre = 3, kPost = 6;      // guard points around the stored data (the 8-tap read looks 3 back and 4 ahead)
    static constexpr double kRate = 48000.0, kRootHz = 261.6255653005986;   // C4
    struct Sample {
        bool loop = false;
        int length = 0;                          // samples at kRate (a loop's is a multiple of 16)
        std::array<std::vector<float>, kLevels> lv;   // level j stored at 2 kRate / 2^j; a loop's guards wrap, a one-shot's are zeros
        const float* at(int j) const { return lv[static_cast<size_t>(j)].data() + kPre; }
    };
    std::array<Sample, kSamples> s;
    // speed: 1 = the sample's own pitch. The content of level j reaches 21.6 kHz / 2^j (at kRate): at this speed it must fold back no lower
    // than 18 kHz (or not at all), and the read must take at most 3 stored samples per output sample (the kernel's design range).
    static int level(double speed, double fs) { double b; return level(speed, fs, b); }
    // with a blend: over the last quarter octave before a level runs out, the next (darker) level fades in (weight `blend` 0..1), so a pitch
    // that sweeps across the boundary (glide, bend, flyby) changes the top of the sound smoothly instead of in one step
    static int level(double speed, double fs, double& blend) {
        const double top = fs - 18000.0 > 0.5 * fs ? fs - 18000.0 : 0.5 * fs;
        const double g = std::max(std::log2(std::max(1e-9, 21600.0 * speed / top)), std::log2(std::max(1e-9, 2.0 * kRate * speed / (3.0 * fs))));
        int j = static_cast<int>(std::ceil(g));
        if (j < 0) j = 0;
        if (j > kLevels - 1) j = kLevels - 1;
        constexpr double w = 0.25;
        blend = j < kLevels - 1 ? std::min(1.0, std::max(0.0, (g - (j - w)) / w)) : 0.0;
        return j;
    }
};
const SampleBank& sampleBank();

// the sample read: an 8-tap Kaiser-windowed sinc (beta 7, cutoff 0.45 of the stored rate; passband to 0.21 within 0.05 dB, images from 0.79
// under -76 dB), 512 phases with linear interpolation between them. Reads data[floor(x) - 3 .. floor(x) + 4]; x >= 0.
struct SincTable {
    static constexpr int kTaps = 8, kPhases = 512;
    std::vector<float> h, d;                     // h[p * 8 + t]; d = the step to the next phase
};
const SincTable& sincTable();
inline double sampleRead(const SincTable& k, const float* data, double x) {
    const long long i = static_cast<long long>(x);
    const double u = (x - static_cast<double>(i)) * SincTable::kPhases;
    const int p = static_cast<int>(u);
    const double w = u - p;
    const float* h = k.h.data() + p * SincTable::kTaps;
    const float* d = k.d.data() + p * SincTable::kTaps;
    const float* s = data + i - 3;
    const float wf = static_cast<float>(w);
    float acc[SincTable::kTaps];   // in float (the data and the taps are float): eight independent products the compiler can vectorise
    for (int t = 0; t < SincTable::kTaps; ++t) acc[t] = (h[t] + wf * d[t]) * s[t];
    return static_cast<double>(((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7])));
}
inline double sampleRead(const float* data, double x) { return sampleRead(sincTable(), data, x); }

// sin(2 pi x) for any x (cycles) from a 4096-point table with linear interpolation (within 3e-7)
const float* sineTable();
inline double sinCycles(const float* t, double x) {
    x -= static_cast<double>(static_cast<long long>(x)) - (x < 0.0 ? 1.0 : 0.0);   // floor without the call
    const double i = x * 4096.0;
    const int k = static_cast<int>(i);
    return t[k] + (i - k) * (t[k + 1] - t[k]);
}

}  // namespace sw::in07
