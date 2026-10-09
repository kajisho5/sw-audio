// SW MD05 Rotary — rotating-speaker simulation (spec: 仕様書 v1.0「MD05 Rotary」). Wet signal only (Mix is the shared frame's, default 100 %).
//   in (L+R mean) -> Drive (unity-gain soft clip, 0 .. +18 dB) -> split at 800 Hz (Linkwitz-Riley 4th order): the highs go to the HORN rotor, the lows to the DRUM rotor.
//   Each rotor (angle theta, speed w) reaches two mics (at +-60 degrees): Doppler: delay = 1 ms - R cos(theta - phi) / 343 m/s (horn R 0.18 m: +-0.52 ms; drum R 0.12 m: +-0.35 ms) and
//   amplitude g = ((1 - k) + k (1 + cos(theta - phi)) / 2) / (1 - k / 2), k = (horn 0.7, drum 0.45) x (1 - 0.5 Mic distance): the mean over a turn is 1, Far (right end) halves the swing (room).
//   Speed: Stop 0 Hz, Slow (horn 0.8 / drum 0.67 Hz), Fast (6.7 / 5.7 Hz); each speed follows its target exponentially with the time constant tau_horn = 0.3 + 0.2 Accel seconds,
//   tau_drum = 3 tau_horn. Horn / Drum volumes (0 .. 10): gain = v / 7 (7 = 1). Cabinet resonances: horn bell 2.5 kHz +1.5 dB (Q 1), drum bell 110 Hz +2 dB (Q 0.9).
//   The Doppler centre delay is 1 ms and is the reported latency (48 samples at 48 kHz, fixed).
//   MIDI / footswitch (EVO, spec "CC64, CC1, Note"): CC64 (sustain pedal) >= 64 Fast, below Slow; CC1 (mod wheel) 0-31 Stop, 32-95 Slow, 96-127 Fast; the notes C2 / C#2 / D2 (36 / 37 / 38) Stop / Slow / Fast
//   (the key numbers are a design value). Speed changes at once, the rotors follow it with their inertia; the change is handed to the host like a parameter written by the plug-in (takeParamWrite).
#pragma once
#include "sw/param.hpp"
#include "sw/saturate.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::md05 {

enum ParamId { Speed, Accel, Horn, Drum, MicDistance, Drive, Mix, Unit, kNumParams };
enum SpeedId { Stop = 0, Slow = 1, Fast = 2 };

const std::vector<ParamSpec>& specs();
double hornTargetHz(int speed);
double drumTargetHz(int speed);
double hornTau(double accel);   // seconds
double drumTau(double accel);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const;
    double hornHz() const { return hornHz_; }
    double drumHz() const { return drumHz_; }
    double dopplerSamples(bool horn, int mic) const { return lastDelay_[horn ? 0 : 1][mic]; }
    // MIDI (audio thread): a controller / a note sets Speed; takeParamWrite() hands it to the host once (bit 0 begin, bit 1 value, bit 2 end = 7)
    void midiControl(int cc, int value);
    void midiNote(int key, bool on);
    int takeParamWrite(int& id, double& plain);

private:
    double read(int rotor, double delay) const;
    double fs_ = 48000.0, hornHz_ = 0.0, drumHz_ = 0.0, hornAngle_ = 0.0, drumAngle_ = 0.0, lastDelay_[2][2] = {{0, 0}, {0, 0}};
    size_t pos_ = 0, mask_ = 0;
    void midiSpeed(int speed);
    int pendingSpeed_ = -1;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;   // 0 horn (highs), 1 drum (lows)
    Svf lo1_, lo2_, hi1_, hi2_, hornBell_, drumBell_;
    Saturator sat_;
};

}  // namespace sw::md05
