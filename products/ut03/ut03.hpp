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
#include "sw/band_spectrum.hpp"
#include "sw/copy_atomic.hpp"
#include "sw/loudness.hpp"
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
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
    // References are loaded from the screen's thread while the audio thread keeps playing: a reference is built off to the side and put in place with one pointer swap (the audio thread reads
    // whichever one it finds at the start of a block); a replaced one is kept until the audio thread has moved past it. One loading thread at a time.
    bool loadReference(int slot, const uint8_t* data, size_t size);   // slot 1 = B, 2 = C; false: not an audio file we read (the earlier reference stays)
    void clearReference(int slot);
    // the file arrives in pieces (the screen cannot hand over a path): stageBegin, stageAppend ..., stageCommit = loadReference of the collected bytes. Starting again drops an unfinished upload.
    bool stageBegin(int slot);
    bool stageAppend(const uint8_t* data, size_t size);   // false: no upload open, or over stageLimit (the upload is dropped; the commit then fails)
    bool stageAppendBase64(const char* text);             // the screen sends text: the same, from base64
    bool stageCommit();
    void stageAbort();
    void setStageLimit(size_t bytes) { stageLimit_ = bytes; }
    int loadsDone() const { return done_; }     // loads that worked / failed since the start: the screen compares them after its commit
    int loadsFailed() const { return failed_; }
    bool hasReference(int slot) const { return slot >= 1 && slot <= 2 && pub_[slot - 1].load() != nullptr; }
    double referenceSeconds(int slot) const;   // length of the loaded reference (0: none); the audio thread and the tests read it
    void setLoopRegion(double startSec, double endSec);
    double matchDb() const { return matchDb_; }
    double referenceLufs(int slot) const { const Ref* r = (slot >= 1 && slot <= 2) ? pub_[slot - 1].load() : nullptr; return r ? r->lufs : -200.0; }
    double inputLufs() const { return meter_.integrated(); }
    void regionOf(int slot, double& startSec, double& endSec) const;
    // SW Link (the adapter calls these on the audio thread after a block): the long-term spectrum of the reference the Source selects (B for B, C for C; for A the first one loaded), 60 bands of 1/6 octave
    // (sw::BandSpectrum, the whole file, mono: what EQ05 Match compares). linkSerial() is 0 without a reference, otherwise it changes with every new one; linkBands() fills db[BandSpectrum::kBands].
    unsigned linkSerial() const;
    bool linkBands(double* db) const;

private:
    struct Ref { std::vector<float> l, r; double rate = 0, lufs = -200; int chorus = 0; unsigned id = 0; bool measured = false; std::array<double, BandSpectrum::kBands> bands{}; bool hasBands = false; };   // immutable once published; at the host rate when prepared
    std::shared_ptr<const Ref> build(Decoded&& d) const;
    std::shared_ptr<const Ref> convert(const Ref& r) const;   // to the host rate (loudness and chorus again)
    void publish(int i, std::shared_ptr<const Ref> n);
    void reap();
    void region(const Ref& r, long long& a, long long& b) const;
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::shared_ptr<const Ref> owned_[2];                // what the loading thread holds
    CopyAtomic<const Ref*> pub_[2];                      // what the audio thread reads (the same objects)
    std::vector<std::pair<std::shared_ptr<const Ref>, unsigned>> retired_;   // replaced ones, freed once the audio thread has moved past the generation
    CopyAtomic<unsigned> gen_{0}, acked_{0}, serial_{0};
    CopyAtomic<int> done_{0}, failed_{0};
    unsigned seenId_[2] = {0, 0};
    std::vector<uint8_t> stage_; int stageSlot_ = 0; bool stageOpen_ = false, stageBroken_ = false; size_t stageLimit_ = size_t(512) << 20;
    CopyAtomic<double> customA_{0.0}, customB_{1e9};   // the loop region of Custom (set from the screen's thread)
    IntegratedLoudness meter_;
    double w_[3] = {1, 0, 0};          // linear ramps 0..1 per source
    double matchDb_ = 0, gDb_[2] = {0, 0};
    double hostSec_ = -1; bool hostPlaying_ = false;
    long long free_[2] = {0, 0}, lastEnd_[2] = {-1, -1};
    int jumpFade_[2] = {0, 0};
};

}  // namespace sw::ut03
