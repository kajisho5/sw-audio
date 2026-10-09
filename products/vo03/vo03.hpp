// SW VO03 Harmony — up to four harmony voices made from one voice (spec: 仕様書 v1.0「VO03 Harmony」). The lead passes at the centre, delayed by the latency (there is no Mix in the table:
//   the harmonies are added to it); the track is mono (a stereo input is summed).
//   One sw::PitchAnalyzer, one sw::PsolaSynth and one sw::PitchCorrector per voice. The singer's note is the nearest note of the scale (Key + Scale: appended parameters, the table has none);
//   Source Scale: the voice sings `Interval` degrees of that scale above / below it (-7 .. +7, "3rd" = +2); Fixed: the same degrees as a major-scale interval in semitones, whatever the key
//   (+2 = +4 semitones); MIDI (chords from a MIDI track): not connected yet, it plays like Scale. The singer's vibrato is kept. Per voice: On, Interval, Level (-60 .. 0 dB), Pan (constant power),
//   Formant (-3 .. +3 semitones: the voice's vowel only), Humanize (a slow random drift of the pitch, +-30 cents x Humanize) and Delay (0 .. 100 ms).
//   Reported delay 1450 samples (the engine's lookahead: 2 x the longest period + ...).
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_correct.hpp"
#include "sw/pitch_engine.hpp"
#include <array>
#include <vector>

namespace sw::vo03 {

constexpr int kVoices = 4;
// ids: Source, then per voice (v = 0..3) kPerVoice parameters, then Key, Scale
enum VoiceParam { On, Interval, Level, Pan, Formant, Humanize, Delay, kPerVoice };
enum { Source = 0, kFirstVoice = 1, Key = kFirstVoice + kVoices * kPerVoice, Scale, kNumParams };
inline constexpr int voiceParam(int voice, int p) { return kFirstVoice + voice * kPerVoice + p; }
enum SourceId { Midi = 0, ScaleSrc = 1, Fixed = 2 };
enum ScaleId { Major = 0, Minor = 1 };

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
    // for the screen: the newest analysis frame is voiced; the singer's pitch and the pitch each voice is made to sing (MIDI note numbers, meaningful while voiced)
    bool voiced() const { return an_.currentVoiced(); }
    double leadSemitones() const { return v_[0].corr.lastMeasuredSemitones(); }
    double voiceSemitones(int v) const { return v_[static_cast<size_t>(v)].corr.lastOutputSemitones(); }

private:
    struct Voice {
        PsolaSynth synth; PitchCorrector corr; std::vector<float> delay; size_t dpos = 0;
        double drift = 0.0, ph1 = 0.0, ph2 = 0.0, noise = 0.0; unsigned rng = 1;
    };
    void apply();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    PitchAnalyzer an_;
    std::array<Voice, kVoices> v_;
};

}  // namespace sw::vo03
