// SW VO06 Formant — change the character of a voice without changing its timing (spec: 仕様書 v1.0「VO06 Formant」). Wet signal only (Mix is the shared frame's, default 100 %).
//   The same engine as VO01 (sw::PitchAnalyzer + sw::PsolaSynth) with a constant ratio source: pitch ratio 2^((Pitch + character pitch)/12), formant factor 2^((Formant + character formant)/12);
//   the length of the sound is never changed (PSOLA keeps every mark's time). Pitch -12 .. +12 semitones; Formant -5 .. +5 (one unit = one semitone of vowel shift: the spectral envelope moves, the pitch does not).
//   Character (design values: pitch / formant semitones added to the two knobs): Neutral 0 / 0, Deep -2 / -3, Bright 0 / +2, Child +4 / +4.
//   Keep timing On (default): the formant is only moved by Formant (and the character). Off: the formant follows the pitch (a pitch shift of +n semitones also moves the vowel by n semitones,
//   a "tape" voice that grows or shrinks with the pitch); the timing is the same either way (the engine cannot stretch time in real time without changing the delay).
//   Smooth On: the pitch and the formant glide to a new setting (40 ms); Off: the new setting applies at the next mark. Reported delay: 1450 samples (same engine and setting as VO01).
#pragma once
#include "sw/param.hpp"
#include "sw/pitch_engine.hpp"
#include <array>
#include <vector>

namespace sw::vo06 {

enum ParamId { Pitch, Formant, Character, KeepTiming, Smooth, Mix, kNumParams };
enum CharacterId { Neutral = 0, Deep = 1, Bright = 2, Child = 3 };

const std::vector<ParamSpec>& specs();
PitchConfig engineConfig(double fs);
// semitones added by a character: {pitch, formant}
std::array<double, 2> characterSemis(int character);

class Processor : public RatioSource {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    void ratio(double f0Hz, bool voiced, double dtSeconds, double& pitchRatio, double& formantRatio) override;

private:
    void wanted(double& pitchSemis, double& formantSemis) const;
    double fs_ = 48000.0, curPitch_ = 0.0, curFormant_ = 0.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    PitchAnalyzer an_;
    PsolaSynth synth_;
};

}  // namespace sw::vo06
