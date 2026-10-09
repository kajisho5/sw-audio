#include "doctest.h"
#include "gt03/gt03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::gt03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
// every pedal off, the gate off
Set clear() { Set s = {{NoiseGate, -80}}; for (int n = 0; n < kSlots; ++n) { s.push_back({slotParam(n, On), 0}); } return s; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
Set pedal(int slot, int type, double a = 5, double b = 5, double c = 5) { return {{slotParam(slot, Type), static_cast<double>(type)}, {slotParam(slot, On), 1}, {slotParam(slot, KnobA), a}, {slotParam(slot, KnobB), b}, {slotParam(slot, KnobC), c}}; }
}

TEST_CASE("GT03 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams)); CHECK(kNumParams == 4 + 8 * 5);
    CHECK(s[Input].min == -24); CHECK(s[Input].max == 24); CHECK(s[Output].min == -24); CHECK(s[Output].max == 24); CHECK(s[Input].def == 0);
    CHECK(s[NoiseGate].min == -80); CHECK(s[NoiseGate].max == -20); CHECK(s[NoiseGate].def == -60); CHECK(std::string(s[NoiseGate].minLabel) == "Off"); CHECK(s[BypassAll].def == 0);
    CHECK(s[static_cast<size_t>(slotParam(0, Type))].labels == std::vector<std::string>{"None", "Comp", "Drive", "Fuzz", "Chorus", "Delay", "Reverb"});
    const int defs[8] = {Comp, Drive, Fuzz, Chorus, Delay, Reverb, None, None}; for (int n = 0; n < 8; ++n) { CHECK(s[static_cast<size_t>(slotParam(n, Type))].def == defs[n]); CHECK(s[static_cast<size_t>(slotParam(n, On))].def == (n == 0 ? 1.0 : 0.0)); CHECK(s[static_cast<size_t>(slotParam(n, KnobA))].def == 5); }
    CHECK(std::string(s[static_cast<size_t>(slotParam(7, KnobC))].name) == "Pedal 8 C"); Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("GT03 no pedal on: the signal passes bit for bit; Bypass all too") {
    auto p = make(clear()); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    auto b = make({{BypassAll, 1}, {slotParam(0, On), 1}}); const auto z = run(b, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(z[i] == x[i]);
}
TEST_CASE("GT03 Input is a gain in front of the board (the shell's In is the panel switch, not a trim); Bypass all ignores it") {
    const auto x = noise(-30, 1.0, 5);
    auto up = make(with(clear(), {{Input, 12}})); const auto y = run(up, x);
    for (size_t i = 4800; i < x.size(); ++i) REQUIRE(std::abs(static_cast<double>(y[i]) - 3.98107 * x[i]) < 1e-5 + 1e-4 * std::abs(3.98107 * x[i]));
    auto dn = make(with(clear(), {{Input, -12}})); const auto z = run(dn, x);
    for (size_t i = 4800; i < x.size(); ++i) REQUIRE(std::abs(static_cast<double>(z[i]) - 0.251189 * x[i]) < 1e-5 + 1e-4 * std::abs(0.251189 * x[i]));
    auto by = make({{Input, 12}, {BypassAll, 1}}); const auto w = run(by, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(w[i] == x[i]);
    auto lv = make(with(clear(), {{Input, 0}})); const auto v = run(lv, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(v[i] == x[i]);   // 0 dB: untouched
    auto sw = make(clear()); CHECK(sw.latencySamples() == 0);
}
TEST_CASE("GT03 Comp: more Sustain, less level") {
    auto lo = make(with(clear(), pedal(0, Comp, 1, 5))), hi = make(with(clear(), pedal(0, Comp, 9, 5))); const auto x = sine(-8, 3.0, 700);
    const auto a = run(lo, x), b = run(hi, x); CHECK(rmsDb(a, 96000, 144000) > rmsDb(b, 96000, 144000) + 3.0); CHECK(rmsDb(b, 96000, 144000) < -8.0);
    auto lvl = make(with(clear(), pedal(0, Comp, 5, 8))); CHECK(rmsDb(run(lvl, x), 96000, 144000) > rmsDb(run(lo, x), 96000, 144000) - 3.0);
}
TEST_CASE("GT03 Drive and Fuzz add harmonics; the knobs move them") {
    const auto x = sine(-20, 2.0, 220);
    auto d = make(with(clear(), pedal(0, Drive, 8, 5, 5))); const auto y = run(d, x); CHECK(binDb(y, 660, 48000, 96000) > binDb(y, 220, 48000, 96000) - 40.0); CHECK(binDb(y, 660, 48000, 96000) > binDb(x, 660, 48000, 96000) + 40.0);
    auto f = make(with(clear(), pedal(0, Fuzz, 8, 5, 5))); const auto z = run(f, x); CHECK(binDb(z, 660, 48000, 96000) > binDb(x, 660, 48000, 96000) + 40.0);
    auto lo = make(with(clear(), pedal(0, Drive, 1, 5, 5))), hi = make(with(clear(), pedal(0, Drive, 9, 5, 5))); CHECK(binDb(run(hi, x), 660, 48000, 96000) > binDb(run(lo, x), 660, 48000, 96000) + 6.0);
    auto lv = make(with(clear(), pedal(0, Drive, 5, 5, 8))), lv0 = make(with(clear(), pedal(0, Drive, 5, 5, 2))); CHECK(rmsDb(run(lv, x), 48000, 96000) > rmsDb(run(lv0, x), 48000, 96000) + 6.0);
}
TEST_CASE("GT03 Chorus changes the sound; Mix 0 leaves it") {
    const auto x = sine(-20, 3.0, 440); auto on = make(with(clear(), pedal(0, Chorus, 5, 8, 10))), off = make(with(clear(), pedal(0, Chorus, 5, 8, 0)));
    const auto a = run(on, x), b = run(off, x); double dev = 0; for (size_t i = 96000; i < 144000; ++i) dev = std::max(dev, static_cast<double>(std::abs(a[i] - x[i]))); CHECK(dev > 0.01);
    for (size_t i = 100000; i < 100200; ++i) NEAR(b[i], x[i], 1e-6);
}
TEST_CASE("GT03 Delay: the echo sits at Time; Reverb has a tail") {
    std::vector<float> imp(48000 * 3, 0.0f); imp[0] = 0.5f;
    auto d = make(with(clear(), pedal(0, Delay, 5, 2, 10))); const auto y = run(d, imp); const double t = 50.0 * std::pow(1000.0 / 50.0, 0.5) * 0.001 * 48000.0;   // knob 5 = about 224 ms
    size_t pk = 1000; for (size_t i = 1000; i < 24000; ++i) if (std::abs(y[i]) > std::abs(y[pk])) pk = i; CHECK(std::abs(static_cast<double>(pk) - t) < 0.02 * t); CHECK(std::abs(y[pk]) > 0.05f);
    auto r = make(with(clear(), pedal(0, Reverb, 5, 5, 10))); const auto z = run(r, imp); CHECK(rmsDb(z, 24000, 48000) > -90.0); CHECK(rmsDb(z, 24000, 48000) > rmsDb(z, 120000, 144000) + 3.0);
    auto none = make(with(clear(), pedal(0, Reverb, 5, 5, 0))); const auto q = run(none, imp); for (size_t i = 24000; i < 24100; ++i) NEAR(q[i], imp[i], 1e-6);
}
TEST_CASE("GT03 the order matters; a third pedal of one type is dropped") {
    const auto x = sine(-12, 2.0, 220);
    auto ab = make(with(with(clear(), pedal(0, Comp, 9, 5)), pedal(1, Drive, 7, 5, 5))), ba = make(with(with(clear(), pedal(0, Drive, 7, 5, 5)), pedal(1, Comp, 9, 5)));
    const auto a = run(ab, x), b = run(ba, x); CHECK(std::abs(rmsDb(a, 48000, 96000) - rmsDb(b, 48000, 96000)) > 0.2);
    Set three = clear(); for (int n = 0; n < 3; ++n) { auto s = pedal(n, Comp, 9, 5); three.insert(three.end(), s.begin(), s.end()); } auto t3 = make(three); CHECK(t3.slotActive(0)); CHECK(t3.slotActive(1)); CHECK_FALSE(t3.slotActive(2));
    CHECK_FALSE(make(clear()).slotActive(0)); CHECK_FALSE(make(clear()).slotActive(9));
}
TEST_CASE("GT03 Noise gate: a quiet hiss is expanded, a note is not; Off at the lowest position") {
    auto g = make({{NoiseGate, -40}, {slotParam(0, On), 0}}); const auto hiss = noise(-60, 2.0, 3), y = run(g, hiss); CHECK(rmsDb(y, 48000, 96000) < rmsDb(hiss, 48000, 96000) - 20.0);
    const auto tone = sine(-20, 1.0, 330); auto g2 = make({{NoiseGate, -40}, {slotParam(0, On), 0}}); CHECK(std::abs(rmsDb(run(g2, tone), 24000, 48000) - (-20.0)) < 0.3);
    auto off = make({{NoiseGate, -80}, {slotParam(0, On), 0}}); const auto z = run(off, hiss); for (size_t i = 0; i < hiss.size(); ++i) REQUIRE(z[i] == hiss[i]);
}
TEST_CASE("GT03 Tuner reads the note and cents and never changes the sound") {
    auto p = make(clear()); const auto x = sine(-20, 1.0, 440.0); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    CHECK(std::abs(p.tunerHz() - 440.0) < 3.0); CHECK(p.tunerNote() == 69); CHECK(std::abs(p.tunerCents()) < 12.0);
    auto q = make(clear()); run(q, sine(-20, 1.0, 110.0 * std::pow(2.0, 30.0 / 1200.0))); CHECK(q.tunerNote() == 45); CHECK(std::abs(q.tunerCents() - 30.0) < 15.0);
    auto b = make(with(clear(), {{BypassAll, 1}})); run(b, sine(-20, 1.0, 196.0)); CHECK(b.tunerNote() == 55);   // the tuner runs while bypassed
    Processor z; CHECK(z.tunerHz() == 0.0); CHECK(z.tunerNote() == 0);
}
TEST_CASE("GT03 odd block sizes, mono, finite with everything on, before prepare") {
    Set all = clear(); const int types[6] = {Comp, Drive, Fuzz, Chorus, Delay, Reverb}; for (int n = 0; n < 6; ++n) { auto s = pedal(n, types[n], 7, 6, 6); all.insert(all.end(), s.begin(), s.end()); }
    auto p = make(all); std::vector<float> l = noise(-14, 2.0, 5); for (size_t off = 0; off < l.size(); off += 777) { const int n = static_cast<int>(std::min<size_t>(777, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(all); for (float v : run(q, noise(-8, 2.0, 6))) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
