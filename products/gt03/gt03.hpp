// SW GT03 Pedalboard — a row of up to 8 pedals (spec: 仕様書 v1.0「GT03 Pedalboard」). Reported delay 0. The pedals reuse other products' processing: Comp = DY08, Drive and Fuzz = SA06 (three bands, one type), Chorus = MD01, Delay = DL01, Reverb = RV01.
//   Slots 1..8 in signal order. A slot has Type (None / Comp / Drive / Fuzz / Chorus / Delay / Reverb), On and three knobs A, B, C (0..10; the spec's 要確認 says the screen has no pedal knobs, so these are design values):
//     Comp   A Sustain (threshold -6 - 3.4 A dB, ratio 2 + 0.6 A, attack 10 ms, release 150 ms, knee 6 dB, make-up 0.35 x the reduction at -18 dBFS), B Level (-12..+12 dB around 5), C -.
//     Drive  A Drive 0..24 dB (Tube, Medium shape, all three bands), B Tone (-6..+6 dB tilt around 5), C Level (-12..+12 dB around 5).        Fuzz  the same with the Fuzz type.
//     Chorus A Rate (0.1..5 Hz, log), B Depth, C Mix (cross-fade dry -> wet; Mode I+II, Width 100 %).
//     Delay  A Time (50..1000 ms, log), B Feedback (0..80 %), C Mix (dry + wet x C / 10; Analog mode, no sync).     Reverb  A Decay (0.3..8 s, log), B Tone (damping 2..16 kHz, log), C Mix (dry + wet x C / 10; Plate, size 60 %).
//   Each pedal type exists twice (a pool of 12 cores allocated in prepare()); a third pedal of one type is None. Every slot's cores run in the order of the slots; a pedal that is Off or None is skipped entirely.
//   Input / Output (+-24 dB) and Bypass all (a bit-exact bypass: the pedals are not run, so their tails freeze and come back when it is switched off) as in the spec; Noise gate: Off at the lowest position, else -80..-20 dB, a gate of 50 dB range (1 ms attack, 60 ms hold, 150 ms release) in front of the board (the first thing after Input).
//   Default: slot 1 Comp (On), slots 2..6 Drive, Fuzz, Chorus, Delay, Reverb (Off) — the six pedals of the screen, all knobs at 5 — and slots 7 and 8 empty: a plain load is transparent apart from a gentle compressor.
//   Tuner (EVO, class A; display only): sw::PitchTracker on the (gated) input: tunerHz(), tunerNote() (0..127, nearest MIDI note), tunerCents() (-50..+50); it never touches the sound and runs while Bypass all is on.
#pragma once
#include "dl01/dl01.hpp"
#include "dy08/dy08.hpp"
#include "md01/md01.hpp"
#include "rv01/rv01.hpp"
#include "sa06/sa06.hpp"
#include "sw/gate.hpp"
#include "sw/param.hpp"
#include "sw/pitch_tracker.hpp"
#include <array>
#include <memory>
#include <vector>

namespace sw::gt03 {

constexpr int kSlots = 8, kPerSlot = 5;
enum PedalType { None = 0, Comp, Drive, Fuzz, Chorus, Delay, Reverb, kTypes };
enum Field { Type, On, KnobA, KnobB, KnobC };
// order: Input, Output, Noise gate, Bypass all, then slot 1 (Type, On, A, B, C), slot 2, ...
enum ParamId { Input, Output, NoiseGate, BypassAll, Slot1 = 4, kNumParams = Slot1 + kSlots * kPerSlot };
constexpr int slotParam(int slot, int field) { return Slot1 + slot * kPerSlot + field; }   // slot 0..7

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { assign(); applyAll(); } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double tunerHz() const { return hz_; }
    int tunerNote() const;
    double tunerCents() const;
    bool slotActive(int slot) const { return slot >= 0 && slot < kSlots && assigned_[static_cast<size_t>(slot)].type != None && assigned_[static_cast<size_t>(slot)].pool >= 0 && target_[static_cast<size_t>(slotParam(slot, On))] > 0.5; }

private:
    struct Assigned { int type = None, pool = -1; };
    void assign();
    void applyAll();
    void applySlot(int slot);
    void run(float** ch, int numCh, int n);
    double fs_ = 48000.0;
    bool prepared_ = false;
    int maxBlock_ = 256;
    std::array<double, kNumParams> target_{};
    std::array<Assigned, kSlots> assigned_{};
    // the pools: two cores of each type
    std::array<std::unique_ptr<dy08::Processor>, 2> comp_;
    std::array<std::unique_ptr<sa06::Processor>, 4> drive_;   // 0,1 Drive; 2,3 Fuzz
    std::array<std::unique_ptr<md01::Processor>, 2> chorus_;
    std::array<std::unique_ptr<dl01::Processor>, 2> delay_;
    std::array<std::unique_ptr<rv01::Processor>, 2> reverb_;
    std::array<std::array<bool, 2>, kTypes> wasOn_{};
    GateEngine gate_;
    PitchTracker tracker_;
    double hz_ = 0, inGain_ = 1.0;
    std::array<std::vector<float>, 2> dry_;
};

}  // namespace sw::gt03
