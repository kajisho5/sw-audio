// SW LV11 Mic Switch — mic on / off, auto mute on silence and a cough button (spec: 仕様書 v1.0「LV11 Mic Switch」). Reported delay 0.
//   Mic: Live (open), Push to talk (closed except while the button is held — the one button of the spec, "Hold to cough", doubles as the talk button in this mode: design), Off (closed).
//   Hold to cough: while held, the mic is closed (not automatable; works during Lock — the screen sends it). Auto mute On silence: the input (RMS over 100 ms) stays under Silence for Hold seconds -> closed; it opens again 3 dB over Silence.
//   Every open / close is a linear ramp of Fade ms. Duck others: while this mic is open and over Silence it publishes its Duck others amount; the other instances of this product in the same process (sw::LinkMember, see sw/link.hpp)
//   lower themselves by the deepest amount offered (10 ms attack, 300 ms release). **This stands in for SW Link, which does not exist yet; it works inside one process (SW AUDIO engine, most hosts), not across them.**
#pragma once
#include "sw/link.hpp"
#include "sw/param.hpp"
#include <array>
#include <vector>

namespace sw::lv11 {

enum ParamId { Mic, AutoMute, Silence, Hold, Fade, DuckOthers, HoldToCough, kNumParams };
enum MicId { Live = 0, PushToTalk = 1, Off = 2 };
struct LinkTag {};

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { g_ = wantOpen() ? 1.0 : 0.0; } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    bool isOpen() const { return g_ > 0.5; }
    bool autoMuted() const { return autoMuted_; }
    double gain() const { return g_; }
    double duckDb() const { return duckDb_; }
    int linkIndex() const { return link_.index(); }

private:
    bool wantOpen() const;
    double fs_ = 48000.0;
    bool prepared_ = false, autoMuted_ = false;
    std::array<double, kNumParams> target_{};
    double g_ = 1.0, ms_ = 0, quiet_ = 0, duckDb_ = 0;
    LinkMember<LinkTag> link_;
    LinkWatch<LinkTag> watch_;
};

}  // namespace sw::lv11
