// SW IN07 SWINGBY: the morph ("swing-by" between presets; 2026-10-10, the client's "7以外ぜんぶ！"; README「IN07 のモーフ」). Four planets
// at the corners of a square: A (top left) is the sound as it is (the host's values), B, C and D are factory presets chosen by three
// parameters. A probe at (x, y) is pulled by each planet with the inverse square of its distance (Shepard weights): on a planet, that
// sound exactly; between them, a blend. Continuous values blend on their own scale (a cutoff in octaves); a choice (a wave, a filter type, an
// effect's order) is the strongest planet's; a layer or an effect only some planets use fades in (its level from -60 dB, its mix from 0);
// a modulation slot takes the strongest planet's route and blends the amounts of those with the same route. The host's values never change
// (the morph is under them); Mode and Voices stay the host's.
#include "doctest.h"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
constexpr double kFs = 48000.0;
int idOf(const std::string& id) {
    const auto& s = specs();
    for (int i = 0; i < kNumParams; ++i) if (id == s[static_cast<size_t>(i)].id) return i;
    return -1;
}
int presetIndex(const std::string& n) {
    const auto& names = presetNames();
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return static_cast<int>(i);
    return -1;
}
std::vector<float> play(Processor& p, size_t n) {
    std::vector<float> L(n), R(n);
    for (size_t off = 0; off < n; off += 256) {
        const int k = static_cast<int>(std::min<size_t>(256, n - off));
        float* c[2] = {L.data() + off, R.data() + off};
        p.process(c, 2, k);
    }
    return L;
}
double norm(int id, double v) { return specs()[static_cast<size_t>(id)].toNorm(v); }
void run64(Processor& p) { std::vector<float> L(64), R(64); float* c[2] = {L.data(), R.data()}; p.process(c, 2, 64); }   // a few control ticks
}  // namespace

TEST_CASE("IN07 MORPH: the parameters (appended after the planets; off, the probe on A, the planets B..D empty)") {
    const auto& s = specs();
    CHECK(MorphOn == RocheTime + 1);
    CHECK(idOf("in07.morph.on") == MorphOn);
    CHECK(idOf("in07.morph.x") == MorphX);
    CHECK(idOf("in07.morph.y") == MorphY);
    CHECK(idOf("in07.morph.b") == MorphB);
    CHECK(idOf("in07.morph.c") == MorphC);
    CHECK(idOf("in07.morph.d") == MorphD);
    CHECK(kNumParams == MorphD + 1);
    CHECK(s[static_cast<size_t>(MorphOn)].def == 0.0);
    CHECK(s[static_cast<size_t>(MorphX)].def == 0.0); CHECK(s[static_cast<size_t>(MorphY)].def == 0.0);
    for (int id : {MorphB, MorphC, MorphD}) {
        const auto& sp = s[static_cast<size_t>(id)];
        REQUIRE(sp.labels.size() == factoryPresets().size() + 1);
        CHECK(sp.labels[0] == "None");
        CHECK(sp.labels[1] == factoryPresets()[0].name);
        CHECK(sp.def == 0.0);
        CHECK(sp.automatable);
    }
    for (int id = MorphOn; id <= MorphD; ++id) CHECK(s[static_cast<size_t>(id)].automatable);
}

TEST_CASE("IN07 MORPH: on a planet, that planet's sound exactly; on A (the default), the host's own") {
    const int a = presetIndex("Glass Horizon"), b = presetIndex("Velvet Keys");   // both Poly (the morph keeps the host's Mode)
    REQUIRE(a >= 0); REQUIRE(b >= 0);
    auto make = [&](bool on, double x, double y) {
        Processor p;
        applyPreset(p, a);
        p.setParam(MorphB, b + 1);
        p.setParam(MorphOn, on ? 1 : 0); p.setParam(MorphX, x); p.setParam(MorphY, y);
        p.prepare(kFs, 256);
        for (int k : {48, 55, 60}) p.noteOn(k, 0.8);
        return play(p, 24000);
    };
    CHECK(make(true, 0, 0) == make(false, 0, 0));   // on A: nothing changes
    // on B (the top right corner): the same as playing preset B
    Processor q;
    applyPreset(q, b);
    q.prepare(kFs, 256);
    for (int k : {48, 55, 60}) q.noteOn(k, 0.8);
    const auto ref = play(q, 24000), got = make(true, 100, 0);
    double d = 0, e = 0;
    for (size_t i = 0; i < ref.size(); ++i) { d += (ref[i] - got[i]) * (ref[i] - got[i]); e += ref[i] * ref[i]; }
    CHECK(e > 0);
    CHECK(10 * std::log10(d / e + 1e-30) < -80.0);
    // the host's values are still A's
    Processor h;
    applyPreset(h, a);
    h.setParam(MorphB, b + 1); h.setParam(MorphOn, 1); h.setParam(MorphX, 100);
    h.prepare(kFs, 256);
    run64(h);
    Processor ha; applyPreset(ha, a);
    for (int id = 0; id < PresetSelect; ++id) CHECK(h.param(id) == ha.param(id));
    CHECK(h.live(lp(0, Cutoff)) == doctest::Approx(presetPlain(b)[static_cast<size_t>(lp(0, Cutoff))]));
}

TEST_CASE("IN07 MORPH: between the planets, inverse-square weights; values on their own scale; a choice is the strongest planet's") {
    const int a = presetIndex("Anthem Supersaw"), b = presetIndex("Polar Bass");
    REQUIRE(a >= 0); REQUIRE(b >= 0);
    const auto& A = presetPlain(a);
    const auto& B = presetPlain(b);
    Processor p;
    applyPreset(p, a);
    p.setParam(MorphB, b + 1); p.setParam(MorphOn, 1); p.setParam(MorphX, 25); p.setParam(MorphY, 0);
    p.prepare(kFs, 256);
    run64(p);
    // A at distance 0.25, B at 0.75: weights 16 and 1.78 -> A 0.9, B 0.1
    const double wa = 16.0 / (16.0 + 1.0 / 0.5625), wb = 1.0 - wa;
    const int cut = lp(0, Cutoff);   // layer 1 is on in both
    REQUIRE(A[static_cast<size_t>(lp(0, On))] > 0.5); REQUIRE(B[static_cast<size_t>(lp(0, On))] > 0.5);
    CHECK(norm(cut, p.live(cut)) == doctest::Approx(wa * norm(cut, A[static_cast<size_t>(cut)]) + wb * norm(cut, B[static_cast<size_t>(cut)])).epsilon(1e-9));
    CHECK(p.live(Glide) == doctest::Approx(specs()[Glide].toValue(wa * norm(Glide, A[Glide]) + wb * norm(Glide, B[Glide]))));
    CHECK(p.live(lp(0, OscType)) == A[static_cast<size_t>(lp(0, OscType))]);   // nearer A: A's oscillator
    p.setParam(MorphX, 75);
    run64(p);
    CHECK(p.live(lp(0, OscType)) == B[static_cast<size_t>(lp(0, OscType))]);
    // Mode and Voices stay the host's
    CHECK(p.live(Mode) == A[Mode]);
    CHECK(p.live(Voices) == A[Voices]);
    // C and D: the four corners; at the middle all four pull alike
    const int cc = presetIndex("Velvet Keys"), dd = presetIndex("Radio Static");
    p.setParam(MorphC, cc + 1); p.setParam(MorphD, dd + 1); p.setParam(MorphX, 50); p.setParam(MorphY, 50);
    run64(p);
    const auto& C = presetPlain(cc);
    const auto& D = presetPlain(dd);
    const double avg = 0.25 * (norm(Level, A[Level]) + norm(Level, B[Level]) + norm(Level, C[Level]) + norm(Level, D[Level]));
    CHECK(norm(Level, p.live(Level)) == doctest::Approx(avg).epsilon(1e-9));
    // off again: the host's values
    p.setParam(MorphOn, 0);
    run64(p);
    for (int id = 0; id < PresetSelect; ++id) CHECK(p.live(id) == p.param(id));
}

TEST_CASE("IN07 MORPH: a layer or an effect only one planet has fades in; a mod slot takes the strongest route") {
    // A: one layer, no delay; B: a second layer on and a delay
    Processor p;
    applyInit(p);
    p.setParam(lp(1, On), 0); p.setParam(FxDelayOn, 0);
    const int b = presetIndex("Detuned Lead");   // layer 2 on (a sine an octave up), a delay
    REQUIRE(b >= 0);
    const auto& B = presetPlain(b);
    REQUIRE(B[static_cast<size_t>(lp(1, On))] > 0.5); REQUIRE(B[FxDelayOn] > 0.5);
    p.setParam(MorphB, b + 1); p.setParam(MorphOn, 1); p.setParam(MorphX, 20);
    p.prepare(kFs, 256);
    run64(p);
    const double wb = (1.0 / 0.64) / (1.0 / 0.04 + 1.0 / 0.64);
    CHECK(p.live(lp(1, On)) == 1.0);                               // on: B pulls a little
    CHECK(p.live(lp(1, LayerLevel)) == doctest::Approx(wb * B[static_cast<size_t>(lp(1, LayerLevel))] + (1 - wb) * -60.0));
    CHECK(p.live(lp(1, Cutoff)) == B[static_cast<size_t>(lp(1, Cutoff))]);   // its other values: B's alone (A has no such layer)
    CHECK(p.live(FxDelayOn) == 1.0);
    CHECK(p.live(FxDelayMix) == doctest::Approx(wb * B[FxDelayMix]));   // A's delay counts as dry
    CHECK(p.live(FxDelayTime) == B[FxDelayTime]);
    // the mod matrix: slot 1 routes as the strongest planet does; the amount blends over the planets with that route (others count 0)
    CHECK(p.live(modId(0, ModSrc)) == p.param(modId(0, ModSrc)));   // A is the strongest here
    p.setParam(MorphX, 90);
    run64(p);
    CHECK(p.live(modId(0, ModSrc)) == B[static_cast<size_t>(modId(0, ModSrc))]);
    CHECK(p.live(modId(0, ModDst)) == B[static_cast<size_t>(modId(0, ModDst))]);
    const double wb9 = (1.0 / 0.01) / (1.0 / 0.81 + 1.0 / 0.01);
    CHECK(p.live(modId(0, ModAmount)) == doctest::Approx(wb9 * B[static_cast<size_t>(modId(0, ModAmount))]));
}

TEST_CASE("IN07 MORPH: a preset (factory, Init or a user file) changes the sound under the morph and keeps the morph's own settings") {
    Processor p;
    p.setParam(MorphOn, 1); p.setParam(MorphX, 30); p.setParam(MorphY, 70); p.setParam(MorphB, 5); p.setParam(MorphC, 9); p.setParam(MorphD, 2);
    auto kept = [&] { return p.param(MorphOn) == 1 && p.param(MorphX) == 30 && p.param(MorphY) == 70 && p.param(MorphB) == 5 && p.param(MorphC) == 9 && p.param(MorphD) == 2; };
    applyPreset(p, 12);
    CHECK(kept());
    applyInit(p);
    CHECK(kept());
    sw::presetfile::Meta m; m.name = "Mine";
    const std::string text = userPresetText(p, m);
    CHECK(text.find("in07.morph") == std::string::npos);   // a user preset does not carry them
    std::string err;
    CHECK(loadUserPreset(p, text, m, err));
    CHECK(kept());
    for (int id = 0; id < kNumParams; ++id) CHECK(presetPart(id) == (id != PresetSelect && (id < MorphOn || id > MorphD)));
}

TEST_CASE("IN07 MORPH: the probe moved by automation while a chord holds: the sound follows, finite, no patch fade") {
    const int a = presetIndex("Solar Wind"), b = presetIndex("Hoover Stab");
    Processor p;
    applyPreset(p, a);
    p.setParam(MorphB, b + 1); p.setParam(MorphOn, 1);
    p.prepare(kFs, 256);
    for (int k : {48, 55, 60, 63}) p.noteOn(k, 0.8);
    std::vector<float> L(256), R(256);
    double cut0 = p.live(lp(0, Cutoff)), last = cut0;
    bool finite = true, moved = false;
    for (int blk = 0; blk < 200; ++blk) {
        p.setParam(MorphX, blk / 2.0);   // 0 .. 100 over about a second
        float* c[2] = {L.data(), R.data()};
        p.process(c, 2, 256);
        for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(L[static_cast<size_t>(i)]) && std::isfinite(R[static_cast<size_t>(i)]);
        CHECK_FALSE(p.patchPending());
        if (p.live(lp(0, Cutoff)) != last) moved = true;
        last = p.live(lp(0, Cutoff));
    }
    CHECK(finite);
    CHECK(moved);
    CHECK(p.notes() == 4);   // the notes go on (no patch change)
}
