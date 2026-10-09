// SW LV15 Auto Mixer — gain-sharing automixer (spec: 仕様書 v1.0「LV15 Auto Mixer」). Reported delay 0. Up to 8 mics, one instance per mic; the instances of this product in one process share their levels through sw::LinkMember (sw/link.hpp)
//   and each applies only its own gain. **This stands in for SW Link (not built yet): it works where all the instances live in one process (the SW AUDIO engine for OBS, most hosts); a mic number is the order in which the instances were prepared.**
//   Level of a mic: RMS over 50 ms of the mono sum; "active" over -50 dBFS. Gain share: gain_i = w_i / sum(w) over the open mics (w = level; the Priority mic counts 3 times, +9.5 dB), never under Off atten; the sum of the gains stays 1 however many talk.
//   NOM limit: only the loudest N (the Priority mic always among them) are open; the rest sit at Off atten. Gate mode: every active mic (within the NOM limit) passes at 0 dB, the others at Off atten.
//   Last mic hold: when nobody is active the mic that spoke last stays open (the rest at Off atten); Off: with Gain share every mic gets 1/N, with Gate all go to Off atten.
//   Response (up / down): Slow 100 / 400 ms, Medium 40 / 150 ms, Fast 15 / 60 ms.
#pragma once
#include "sw/link.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::lv15 {

enum ParamId { Mode, LastMicHold, OffAtten, Response, Priority, NomLimit, kNumParams };
enum ModeId { GainShare = 0, GateMode = 1 };
struct LinkTag {};

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    int micNumber() const { return link_.index() + 1; }   // 1..8, 0 when the instance is not in the group
    double gain() const { return g_; }
    double levelDb() const { return 20.0 * std::log10(std::max(lvl_, 1e-9)); }
    // for the screen: every mic of the group (index 0..7): in the group, its mixer gain now (dB; the gain it is heading for) and whether it is among the open mics
    bool micUsed(int i) const { return used_[static_cast<size_t>(i)]; }
    bool micOpen(int i) const { return open_[static_cast<size_t>(i)]; }
    double micGainDb(int i) const { return shownDb_[static_cast<size_t>(i)]; }

private:
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    double g_ = 1.0, ms_ = 0, lvl_ = 0;
    std::array<double, 8> shownDb_{};
    std::array<bool, 8> used_{}, open_{};
    LinkMember<LinkTag> link_;
    LinkWatch<LinkTag> watch_;
};

}  // namespace sw::lv15
