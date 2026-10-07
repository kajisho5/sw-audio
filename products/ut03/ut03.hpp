// SW UT03 Reference — A/B against reference tracks at matched loudness (spec: 仕様書 v1.0「UT03 Reference」). Reported delay 0; no Auto gain, no Delta.
//   Source A = the input (untouched, bit-identical at the defaults), B / C = reference 1 / 2 loaded with loadReference(). WAV (PCM 8/16/24/32, float 32/64, extensible) and AIFF (PCM 8-32) are decoded here;
//   FLAC and MP3 are not (the loader says so by returning false). The file is converted to the host rate with a windowed-sinc resampler when it is loaded or when prepare() changes the rate.
//   Loudness match: the reference is played at (integrated loudness of the input - integrated loudness of the reference), the input measured with sw::IntegratedLoudness (30 s memory), the reference over the whole file;
//   matchDb() is the amount shown on the screen ("Match -1.2 LU"). While the input is below the absolute gate nothing is applied (0 dB).
//   Loop: Intro = first 20 s, Verse = 20-40 s, Chorus = the loudest 20 s of the file (100 ms mean-square blocks, 1 s steps), Custom = setLoopRegion(). A file of 20 s or less loops as a whole.
//   Sync play: with the host position (setPlayhead) the reference runs at region start + host time modulo region length, and is silent while the host stands still; without Sync (or when the host gives no time) it runs free.
//   Crossfade: equal-power ramp between the sources. Level trims the reference only. Loop ends and position jumps get a 5 ms fade.
//   The loaded references are not part of the saved state (they are files); the screen reloads them.
#pragma once
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::ut03 {

enum ParamId { Source, LoudnessMatch, Crossfade, Loop, Sync, Level, kNumParams };
enum SourceId { MixA = 0, RefB = 1, RefC = 2 };
enum LoopId { Intro = 0, Verse = 1, Chorus = 2, Custom = 3 };

const std::vector<ParamSpec>& specs();

struct Decoded { std::vector<float> l, r; double rate = 0; };
// WAV / AIFF bytes -> stereo float (mono is duplicated). False for anything else or damaged data.
bool decodeAudio(const uint8_t* data, size_t size, Decoded& out);
std::vector<float> resample(const std::vector<float>& in, double from, double to);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void setPlayhead(double seconds, bool playing) { hostSec_ = seconds; hostPlaying_ = playing; }
    bool loadReference(int slot, const uint8_t* data, size_t size);   // slot 1 = B, 2 = C
    void clearReference(int slot);
    bool hasReference(int slot) const { return slot >= 1 && slot <= 2 && !ref_[slot - 1].l.empty(); }
    void setLoopRegion(double startSec, double endSec);
    double matchDb() const { return matchDb_; }
    double referenceLufs(int slot) const { return (slot >= 1 && slot <= 2) ? ref_[slot - 1].lufs : -200.0; }
    double inputLufs() const { return meter_.integrated(); }
    void regionOf(int slot, double& startSec, double& endSec) const;

private:
    struct Ref { Decoded src; std::vector<float> l, r; double lufs = -200; int chorus = 0; };
    void rebuild(int i);
    void region(int i, long long& a, long long& b) const;
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Ref ref_[2];
    double customA_ = 0, customB_ = 1e9;
    IntegratedLoudness meter_;
    double w_[3] = {1, 0, 0};          // linear ramps 0..1 per source
    double matchDb_ = 0, gDb_[2] = {0, 0};
    double hostSec_ = -1; bool hostPlaying_ = false;
    long long free_[2] = {0, 0}, lastEnd_[2] = {-1, -1};
    int jumpFade_[2] = {0, 0};
};

}  // namespace sw::ut03
