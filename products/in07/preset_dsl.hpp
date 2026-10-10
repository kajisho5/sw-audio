// SWINGBY (SW IN07) — the small language the factory preset tables are written in (internal to presets.cpp and presets_more.cpp).
//   A preset is a list of (parameter id without "in07.", value or step label); inside a layer block the ids drop "lN." too.
//   The helpers below build the usual pieces (an oscillator, a filter, the envelopes, an effect, an LFO, a modulation slot) so a preset
//   reads as its recipe. Presets made with N() (the newer tables) switch off every effect they do not configure (Limit stays on);
//   the first tables (B()) keep the defaults for the effects they do not name.
#pragma once
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace sw::in07::dsl {

struct V {
    const char* id;
    double v = 0.0;
    std::string_view label{};
    int layer = 0;   // 1..4 inside a layer block
    V(const char* i, double x) : id(i), v(x) {}
    V(const char* i, std::string_view l) : id(i), label(l) {}
};
using Vs = std::vector<V>;
inline Vs operator+(Vs a, const Vs& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

// ---- layer pieces
inline Vs wave(const char* w, int oct = 0, int uni = 1, double det = 22, double spread = 80) {
    return {{"osc.type", "Analog"}, {"osc.wave", w}, {"osc.octave", static_cast<double>(oct)}, {"osc.unison", static_cast<double>(uni)},
            {"osc.detune", det}, {"osc.spread", spread}};
}
inline Vs saw(int oct = 0, int uni = 1, double det = 22, double spread = 80) { return wave("Saw", oct, uni, det, spread); }
inline Vs pulse(double pw, int oct = 0, int uni = 1, double det = 22, double spread = 80) { return wave("Square", oct, uni, det, spread) + Vs{{"osc.pw", pw}}; }
inline Vs wt(const char* table, double pos, int oct = 0, int uni = 1, double det = 22, double spread = 80) {
    return {{"osc.type", "Wavetable"}, {"wt.table", table}, {"wt.pos", pos}, {"osc.octave", static_cast<double>(oct)}, {"osc.unison", static_cast<double>(uni)},
            {"osc.detune", det}, {"osc.spread", spread}};
}
inline Vs fm(const char* ratio, double index, double decay = 10000, double fb = 0, int oct = 0, int uni = 1, double det = 22, double spread = 80) {
    return {{"osc.type", "FM"}, {"fm.ratio", ratio}, {"fm.index", index}, {"fm.decay", decay}, {"fm.feedback", fb}, {"osc.octave", static_cast<double>(oct)},
            {"osc.unison", static_cast<double>(uni)}, {"osc.detune", det}, {"osc.spread", spread}};
}
inline Vs smp(const char* id, int oct = 0, int uni = 1, double spread = 80) {
    return {{"osc.type", "Sample"}, {"smp.id", id}, {"osc.octave", static_cast<double>(oct)}, {"osc.unison", static_cast<double>(uni)}, {"osc.spread", spread}};
}
inline Vs flt(const char* type, double cutoff, double res = 0, double env = 0, double drive = 0, double key = 50) {
    return {{"flt.type", type}, {"flt.cutoff", cutoff}, {"flt.res", res}, {"flt.env", env}, {"flt.drive", drive}, {"flt.key", key}};
}
inline Vs open() { return flt("LP 12", 20000); }   // the filter out of the way
inline Vs amp(double a, double d, double s, double r, double vel = 50) { return {{"amp.a", a}, {"amp.d", d}, {"amp.s", s}, {"amp.r", r}, {"vel", vel}}; }
inline Vs fenv(double a, double d, double s, double r) { return {{"fenv.a", a}, {"fenv.d", d}, {"fenv.s", s}, {"fenv.r", r}}; }
inline Vs lvl(double db, double pan = 0) { return {{"level", db}, {"pan", pan}}; }
inline Vs grav(double g) { return {{"osc.gravity", g}}; }

// ---- a preset under construction
enum FxBit { kDrive = 0, kChorus, kDelay, kReverb, kEq };
struct B {
    std::vector<V> v;
    bool fresh = false;              // N(): effects not configured are switched off at done()
    bool fx[5] = {false, false, false, false, false};
    B& g(std::initializer_list<V> x) { v.insert(v.end(), x); return *this; }
    B& l(int n, std::initializer_list<V> x) { for (V e : x) { e.layer = n; v.push_back(e); } return *this; }
    B& L(int n, const Vs& x) {       // a layer (2..4 are switched on)
        if (n > 1) { V on("on", 1.0); on.layer = n; v.push_back(on); }
        for (V e : x) { e.layer = n; v.push_back(e); }
        return *this;
    }
    B& mono(double glide = 0) { v.push_back({"mode", "Mono"}); if (glide > 0) v.push_back({"glide", glide}); return *this; }
    B& legato(double glide) { v.push_back({"mode", "Legato"}); v.push_back({"glide", glide}); return *this; }
    B& drv(double amount, double tone = 60, double mix = 100) { fx[kDrive] = true; return g({{"fx.drive.on", 1}, {"fx.drive.amount", amount}, {"fx.drive.tone", tone}, {"fx.drive.mix", mix}}); }
    B& cho(double rate, double depth, double mix) { fx[kChorus] = true; return g({{"fx.chorus.on", 1}, {"fx.chorus.rate", rate}, {"fx.chorus.depth", depth}, {"fx.chorus.mix", mix}}); }
    B& dly(const char* time, double fb, double mix) { fx[kDelay] = true; return g({{"fx.delay.on", 1}, {"fx.delay.time", time}, {"fx.delay.feedback", fb}, {"fx.delay.mix", mix}}); }
    B& rev(double size, double mix, double damp = 40) { fx[kReverb] = true; return g({{"fx.reverb.on", 1}, {"fx.reverb.size", size}, {"fx.reverb.damp", damp}, {"fx.reverb.mix", mix}}); }
    B& eq(double low, double mid, double high) { fx[kEq] = true; return g({{"fx.eq.on", 1}, {"fx.eq.low", low}, {"fx.eq.mid", mid}, {"fx.eq.high", high}}); }
    // LFO n (1, 2): free running at a rate (Hz), or synced to a note length ("1/16" ...); Note = restart with the first key
    B& lfo(int n, const char* shape, double rate, double orbit = 0) {
        static const char* const id[2][3] = {{"lfo1.shape", "lfo1.rate", "lfo1.orbit"}, {"lfo2.shape", "lfo2.rate", "lfo2.orbit"}};
        return g({{id[n - 1][0], shape}, {id[n - 1][1], rate}, {id[n - 1][2], orbit}});
    }
    B& lfoSync(int n, const char* shape, const char* sync, bool note = false, double orbit = 0) {
        static const char* const id[2][4] = {{"lfo1.shape", "lfo1.sync", "lfo1.trigger", "lfo1.orbit"}, {"lfo2.shape", "lfo2.sync", "lfo2.trigger", "lfo2.orbit"}};
        return g({{id[n - 1][0], shape}, {id[n - 1][1], sync}, {id[n - 1][2], note ? "Note" : "Free"}, {id[n - 1][3], orbit}});
    }
    B& mod(int slot, const char* src, const char* dst, double amount) {
        static const char* const id[8][3] = {{"mod1.src", "mod1.dst", "mod1.amount"}, {"mod2.src", "mod2.dst", "mod2.amount"}, {"mod3.src", "mod3.dst", "mod3.amount"},
                                             {"mod4.src", "mod4.dst", "mod4.amount"}, {"mod5.src", "mod5.dst", "mod5.amount"}, {"mod6.src", "mod6.dst", "mod6.amount"},
                                             {"mod7.src", "mod7.dst", "mod7.amount"}, {"mod8.src", "mod8.dst", "mod8.amount"}};
        return g({{id[slot - 1][0], src}, {id[slot - 1][1], dst}, {id[slot - 1][2], amount}});
    }
    // the arpeggiator (2026-10-10): mode ("Up", "Down", "Up-Down", "Order", "Random"), rate ("1/8", "1/16", "1/16 T", "1/32"), octaves,
    // note length %, swing %; then, optionally, the steps' velocity row (0 = a rest) and pitch row (-12, 0, 7, 12) from step 1
    B& arp(const char* mode, const char* rate, int octaves, double length = 70, double swing = 0) {
        return g({{"arp.on", 1.0}, {"arp.mode", mode}, {"arp.rate", rate}, {"arp.octaves", static_cast<double>(octaves)}, {"arp.length", length}, {"arp.swing", swing}});
    }
    B& arpVel(std::initializer_list<double> row) {
        static const char* const id[16] = {"arp.vel1", "arp.vel2", "arp.vel3", "arp.vel4", "arp.vel5", "arp.vel6", "arp.vel7", "arp.vel8",
                                           "arp.vel9", "arp.vel10", "arp.vel11", "arp.vel12", "arp.vel13", "arp.vel14", "arp.vel15", "arp.vel16"};
        int i = 0;
        for (double x : row) if (i < 16) v.push_back({id[i++], x});
        return *this;
    }
    B& arpPitch(std::initializer_list<double> row) {
        static const char* const id[16] = {"arp.pitch1", "arp.pitch2", "arp.pitch3", "arp.pitch4", "arp.pitch5", "arp.pitch6", "arp.pitch7", "arp.pitch8",
                                           "arp.pitch9", "arp.pitch10", "arp.pitch11", "arp.pitch12", "arp.pitch13", "arp.pitch14", "arp.pitch15", "arp.pitch16"};
        int i = 0;
        for (double x : row) if (i < 16) v.push_back({id[i++], x});
        return *this;
    }
    // the trance gate: rate ("1/8", "1/16", "1/32"), depth %, the 16 steps (1 open, 0 shut)
    B& gate(const char* rate, double depth, std::initializer_list<int> steps) {
        static const char* const id[16] = {"gate.step1", "gate.step2", "gate.step3", "gate.step4", "gate.step5", "gate.step6", "gate.step7", "gate.step8",
                                           "gate.step9", "gate.step10", "gate.step11", "gate.step12", "gate.step13", "gate.step14", "gate.step15", "gate.step16"};
        g({{"gate.on", 1.0}, {"gate.rate", rate}, {"gate.depth", depth}});
        int i = 0;
        for (int x : steps) if (i < 16) v.push_back({id[i++], static_cast<double>(x)});
        return *this;
    }
    B& fly(const char* mode, double depth, double time, double nearness, const char* side = "Alternate") {
        return g({{"flyby.mode", mode}, {"flyby.depth", depth}, {"flyby.time", time}, {"flyby.near", nearness}, {"flyby.side", side}});
    }
    void done() {
        if (!fresh) return;
        static const char* const off[5] = {"fx.drive.on", "fx.chorus.on", "fx.delay.on", "fx.reverb.on", "fx.eq.on"};
        for (int i = 0; i < 5; ++i) if (!fx[i]) v.push_back({off[i], 0.0});
    }
};
inline B N() { B b; b.fresh = true; return b; }
struct Raw { const char* name; const char* cat; B b; };
inline Raw R(const char* name, const char* cat, B b) { b.done(); return {name, cat, std::move(b)}; }

const std::vector<Raw>& morePresets();   // presets_more.cpp

}  // namespace sw::in07::dsl
