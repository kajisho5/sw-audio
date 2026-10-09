// SW VO01 Tune — pitch correction, Auto (spec: 仕様書 v1.0「VO01 Tune」). The whole signal is processed (no Mix); the track is mono (a stereo input is summed, both outputs are the same).
//   sw::PitchAnalyzer + sw::PsolaSynth (time-domain PSOLA: formants stay where they are) with sw::PitchCorrector deciding the factor at every mark: Scale (Major / Chromatic / Custom) in Key (12),
//   Speed (0 .. 400 ms, how fast the pitch moves to the note), Humanize (keeps part of the singer's offset), Vibrato (Natural / Reduce / Flat), Formant (Keep / Follow), Transpose (+-12 semitones).
//   Reported delay = the engine's lookahead (2 x the longest period + 256 samples: 1450 at 48 kHz for 85 Hz and up). Nothing is shifted below 85 Hz or in unvoiced sounds (those come back delayed).
//   View Graph (edit the pitch curve on a graph), Detect MIDI (notes from a MIDI track), Snap to grid and Reference need the screen, the host's MIDI or ARA: stored, no effect yet (Graph plays like Auto
//   without correction).  EVO: suggestedKey() from the key detection of PitchCorrector.
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_correct.hpp"
#include "sw/pitch_engine.hpp"
#include <array>
#include <vector>

namespace sw::vo01 {

enum ParamId { View, Scale, Speed, Humanize, Vibrato, Formant, Transpose, DetectMidi, SnapToGrid, Reference, Key, CustomScale, kNumParams };
enum ViewId { Graph = 0, Auto = 1 };
enum ScaleId { Major = 0, Chromatic = 1, Custom = 2 };
enum VibratoId { Natural = 0, Reduce = 1, Flat = 2 };

const std::vector<ParamSpec>& specs();
PitchConfig engineConfig(double fs);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    int suggestedKey(double* confidence = nullptr) const { return corr_.suggestedKey(confidence); }
    double lastNoteSemitones() const { return corr_.lastOutputSemitones(); }
    double measuredSemitones() const { return corr_.lastMeasuredSemitones(); }   // the singer's pitch at the last mark (MIDI note number, fractional)
    bool voiced() const { return an_.currentVoiced(); }                           // the newest analysis frame is voiced (the two values above are only meaningful then)
    const PitchAnalyzer& analyzer() const { return an_; }

private:
    void apply();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    PitchAnalyzer an_;
    PsolaSynth synth_;
    PitchCorrector corr_;
};

}  // namespace sw::vo01
