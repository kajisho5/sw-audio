// SW LO03 Low Focus — keeps kick and bass out of each other's way (spec: 仕様書 v1.0「LO03 Low Focus」)
//   Focus: a dynamic bell (Q 1.2) whose cut follows the Role:
//     Kick  — the band's tail is tightened: cut = 9 dB x Tight x how far the band level has fallen below its recent peak (dead zone 25 %)
//     Bass  — the band is ducked for about 80 ms after a kick onset in the sidechain (key) input (no key connected: no ducking)
//     Both  — tail tightening plus ducking from onsets found in the track's own lows (the duck attack is slowed to 4 ms so the kick itself passes)
//   Mud cut: a static bell (Q 1.0) at the Mud cut frequency, cut = 6 dB x Tight.   Mono below: the sides under the corner go (LR4 high-pass), mids stay flat.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::lo03 {

enum ParamId { Role, Focus, Tight, MudCut, MonoBelow, kNumParams };
enum RoleId { Kick = 0, Bass = 1, Both = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n) { run(ch, numCh, n, nullptr, 0); }
    void processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) { run(ch, numCh, n, sc, scCh); }
    int latencySamples() const { return 0; }
    double focusCutDb() const { return focusDb_; }   // current dynamic cut of the Focus bell (display / tests)

private:
    struct Lr4 {
        Svf a, b;
        void setup(Svf::Mode m, double fc, double fs) { a.setup(m, fc, fs, 0.70710678, 0); b.setup(m, fc, fs, 0.70710678, 0); }
        double process(double x) { return b.process(a.process(x)); }
    };
    void run(float** ch, int numCh, int n, const float* const* sc, int scCh);
    void updateStatic(bool ramp);
    void updateMono();
    double fs_ = 48000.0, focusDb_ = 0, e_ = 0, ref_ = 0, kFast_ = 0, kSlow_ = 0, duck_ = 0, duckSm_ = 0, refDec_ = 0, duckDec_ = 0, eRel_ = 0, kRel_ = 0;
    int ctl_ = 0;
    bool prepared_ = false, monoOn_ = false;
    std::array<double, kNumParams> target_{};
    struct Ch { Svf focus, mud; };
    std::array<Ch, 2> c_{};
    Svf det_, key_;
    Lr4 sideHp_, midLp_, midHp_;
};

}  // namespace sw::lo03
