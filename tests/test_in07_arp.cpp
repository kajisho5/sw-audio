// SW IN07 SWINGBY: the arpeggiator and the trance gate (README「IN07 のアルペジエーターとトランスゲート」). Parameters appended after
// the preset selector; the arp plays the keys held (and the pedal's) as notes of its own, step by step on the host's beat (or its own
// clock from the first key when the transport stops); the gate opens and closes the voices' sum on a 16-step pattern before the effects.
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

// a quiet, simple patch: one sine layer, short envelopes, no effects (the timing is what is checked)
void simple(Processor& p) {
    applyInit(p);
    p.setParam(lp(0, Wave), Sine);
    p.setParam(lp(0, AmpA), 0.5); p.setParam(lp(0, AmpD), 1); p.setParam(lp(0, AmpS), 100); p.setParam(lp(0, AmpR), 1);
    for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);
}

// one sample at a time: when each key's note starts and stops (held by the arp), up to n samples
struct Hit { int key; size_t on, off; };
std::vector<Hit> run(Processor& p, size_t n) {
    std::vector<Hit> hits;
    std::vector<long> open(128, -1);
    float l = 0, r = 0;
    float* c[2] = {&l, &r};
    for (size_t t = 0; t < n; ++t) {
        p.process(c, 2, 1);
        for (int k = 0; k < 128; ++k) {
            const bool h = p.keyHeld(k);
            if (h && open[static_cast<size_t>(k)] < 0) open[static_cast<size_t>(k)] = static_cast<long>(t);
            if (!h && open[static_cast<size_t>(k)] >= 0) { hits.push_back({k, static_cast<size_t>(open[static_cast<size_t>(k)]), t}); open[static_cast<size_t>(k)] = -1; }
        }
    }
    for (int k = 0; k < 128; ++k) if (open[static_cast<size_t>(k)] >= 0) hits.push_back({k, static_cast<size_t>(open[static_cast<size_t>(k)]), n});
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.on < b.on; });
    return hits;
}
std::vector<int> keys(const std::vector<Hit>& h) { std::vector<int> k; for (const auto& x : h) k.push_back(x.key); return k; }

// 120 bpm: a 1/16 step is 0.125 s = 6000 samples
constexpr size_t kStep = 6000;
}  // namespace

TEST_CASE("IN07 ARP: the parameters (appended after the preset selector, arp and gate off)") {
    const auto& s = specs();
    CHECK(idOf("in07.preset") == PresetSelect);
    CHECK(idOf("in07.arp.on") == ArpOn);
    CHECK(ArpOn == PresetSelect + 1);
    CHECK(s[static_cast<size_t>(ArpOn)].def == 0.0);
    CHECK(s[static_cast<size_t>(GateOn)].def == 0.0);
    CHECK(std::string(s[static_cast<size_t>(ArpMode)].labels[3]) == "Order");
    CHECK(s[static_cast<size_t>(ArpRate)].labels.size() == 4);
    CHECK(s[static_cast<size_t>(ArpSteps)].def == 16.0);
    for (int i = 0; i < kArpSteps; ++i) {
        CHECK(idOf("in07.arp.vel" + std::to_string(i + 1)) == arpVel(i));
        CHECK(idOf("in07.arp.pitch" + std::to_string(i + 1)) == arpPitch(i));
        CHECK(idOf("in07.gate.step" + std::to_string(i + 1)) == gateStep(i));
        CHECK(s[static_cast<size_t>(arpVel(i))].def == 100.0);
        CHECK(s[static_cast<size_t>(arpPitch(i))].def == 0.0);
    }
    CHECK(ArpAlign == gateStep(kArpSteps - 1) + 1);   // the planets were appended after the gate (2026-10-10)
    // every factory preset leaves both off unless it says so (the table is the defaults plus each preset's values)
    Processor p;
    applyPreset(p, 0);
    CHECK(p.param(ArpOn) == 0.0);
    CHECK(p.param(GateOn) == 0.0);
}

TEST_CASE("IN07 ARP: Up, 1/16 at 120 bpm, its own clock from the first key: one note per step, 70 % long") {
    Processor p;
    simple(p);
    p.setParam(ArpOn, 1);
    p.prepare(kFs, 256);
    p.setTempo(120);
    p.noteOn(64, 0.8); p.noteOn(60, 0.8); p.noteOn(67, 0.8);
    const auto h = run(p, 7 * kStep);
    REQUIRE(h.size() >= 7);
    CHECK(keys(h) == std::vector<int>{60, 64, 67, 60, 64, 67, 60});
    for (size_t i = 0; i < 7; ++i) {
        CHECK(h[i].on == i * kStep);
        CHECK(h[i].off == i * kStep + static_cast<size_t>(0.7 * kStep));
    }
}

TEST_CASE("IN07 ARP: modes, octaves, rests and the pitch row") {
    auto play = [](int mode, int octaves, size_t steps, auto&& setup) {
        Processor p;
        simple(p);
        p.setParam(ArpOn, 1); p.setParam(ArpMode, mode); p.setParam(ArpOctaves, octaves);
        setup(p);
        p.prepare(kFs, 256);
        p.setTempo(120);
        p.noteOn(64, 0.8); p.noteOn(60, 0.8); p.noteOn(67, 0.8);
        return keys(run(p, steps * kStep));
    };
    auto none = [](Processor&) {};
    CHECK(play(ArpDown, 1, 4, none) == std::vector<int>{67, 64, 60, 67});
    CHECK(play(ArpUpDown, 1, 6, none) == std::vector<int>{60, 64, 67, 64, 60, 64});
    CHECK(play(ArpOrder, 1, 4, none) == std::vector<int>{64, 60, 67, 64});
    CHECK(play(ArpUp, 2, 7, none) == std::vector<int>{60, 64, 67, 72, 76, 79, 60});
    const auto r1 = play(ArpRandom, 1, 12, none), r2 = play(ArpRandom, 1, 12, none);
    CHECK(r1 == r2);   // the same playing gives the same notes
    for (int k : r1) CHECK((k == 60 || k == 64 || k == 67));
    // a step with no velocity is a rest; the order goes on with the next played step
    CHECK(play(ArpUp, 1, 4, [](Processor& p) { p.setParam(arpVel(1), 0); }) == std::vector<int>{60, 64, 67});
    // the pitch row: +12 on the second step, -12 on the third
    CHECK(play(ArpUp, 1, 3, [](Processor& p) { p.setParam(arpPitch(1), 12); p.setParam(arpPitch(2), -12); }) == std::vector<int>{60, 76, 55});
    // a 3-step pattern repeats its rests and pitches every 3 steps
    CHECK(play(ArpUp, 1, 6, [](Processor& p) { p.setParam(ArpSteps, 3); p.setParam(arpPitch(2), 12); }) == std::vector<int>{60, 64, 79, 60, 64, 79});
}

TEST_CASE("IN07 ARP: the step velocity scales the key's velocity; swing delays every second step") {
    Processor p;
    simple(p);
    p.setParam(ArpOn, 1); p.setParam(ArpSwing, 50); p.setParam(ArpLength, 50);
    p.prepare(kFs, 256);
    p.setTempo(120);
    p.noteOn(60, 1.0); p.noteOn(64, 1.0);
    const auto h = run(p, 4 * kStep);
    REQUIRE(h.size() >= 4);
    CHECK(h[0].on == 0);
    CHECK(h[1].on == kStep + kStep / 4);   // 50 % swing: a quarter of a step late
    CHECK(h[2].on == 2 * kStep);
    CHECK(h[3].on == 3 * kStep + kStep / 4);
    CHECK(h[1].off - h[1].on == kStep / 2);
}

TEST_CASE("IN07 ARP: on the host's beat when the transport plays; nothing after the keys are let go") {
    Processor p;
    simple(p);
    p.setParam(ArpOn, 1);
    p.prepare(kFs, 256);
    p.setTempo(120);
    p.setTransport(true, 10.1);   // in the middle of a step: the first note waits for the next one (10.25 beats)
    p.noteOn(60, 0.8);
    float l = 0, r = 0; float* c[2] = {&l, &r};
    size_t first = 0;
    for (size_t t = 0; t < kStep; ++t) { p.process(c, 2, 1); if (p.keyHeld(60)) { first = t; break; } }
    CHECK(first == static_cast<size_t>(std::lround(0.15 * 0.5 * kFs)));   // 0.15 beat at 120 bpm
    // let go: the note that sounds finishes its length, no new step starts
    p.noteOff(60);
    const auto h = run(p, 4 * kStep);
    CHECK(h.size() <= 1);
    int key, ch, id;
    int ends = 0;
    while (p.takeEnded(key, ch, id)) ++ends;
    CHECK(ends >= 1);   // the host's note was reported as ended
}

TEST_CASE("IN07 ARP: switched on and off with keys held; a patch change keeps the arp going") {
    Processor p;
    simple(p);
    p.prepare(kFs, 256);
    p.setTempo(120);
    p.noteOn(60, 0.8); p.noteOn(64, 0.8);
    std::vector<float> L(256), R(256); float* c[2] = {L.data(), R.data()};
    p.process(c, 2, 256);
    CHECK(p.keyHeld(60)); CHECK(p.keyHeld(64));
    p.setParam(ArpOn, 1);   // the held keys go to the arp: one note at a time
    const auto h = run(p, 3 * kStep);
    CHECK(keys(h).size() >= 3);
    p.setParam(ArpOn, 0);   // off: the keys still held sound as ordinary notes again
    p.process(c, 2, 256);
    CHECK(p.keyHeld(60)); CHECK(p.keyHeld(64));
    // a patch change with the arp on: it goes on with the same keys
    p.setParam(ArpOn, 1);
    run(p, kStep);
    p.beginPatch();
    simple(p);
    p.setParam(ArpOn, 1); p.setParam(ArpMode, ArpDown);
    p.endPatch();
    const auto h2 = run(p, 4 * kStep);
    REQUIRE(h2.size() >= 2);
    for (const auto& x : h2) CHECK((x.key == 60 || x.key == 64));
}

TEST_CASE("IN07 GATE: the 16-step pattern opens and closes the sound on the beat; depth; no clicks at the edges") {
    Processor p;
    simple(p);
    p.setParam(GateOn, 1); p.setParam(GateDepth, 100);
    for (int i = 0; i < kArpSteps; ++i) p.setParam(gateStep(i), i % 2 == 0 ? 1 : 0);
    p.prepare(kFs, 256);
    p.setTempo(120);
    p.noteOn(69, 0.8);
    std::vector<float> L, R, l(256), r(256); float* c[2] = {l.data(), r.data()};
    for (size_t t = 0; t < 4 * kStep; t += 256) { p.process(c, 2, 256); L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end()); }
    auto rms = [&](size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += L[i] * L[i]; return std::sqrt(s / static_cast<double>(b - a)); };
    const double open = rms(kStep / 4, kStep * 3 / 4), shut = rms(kStep + kStep / 4, kStep * 7 / 4);
    CHECK(open > 0.01);
    CHECK(shut < open * 1e-3);
    CHECK(rms(2 * kStep + kStep / 4, 2 * kStep + kStep * 3 / 4) == doctest::Approx(open).epsilon(0.05));
    // the edges ramp (2 ms): no jump bigger than the sine itself moves
    double jump = 0;
    for (size_t i = 1; i < L.size(); ++i) jump = std::max(jump, static_cast<double>(std::fabs(L[i] - L[i - 1])));
    CHECK(jump < 0.05);
    // depth 50 %: the closed steps keep half
    Processor q;
    simple(q);
    q.setParam(GateOn, 1); q.setParam(GateDepth, 50);
    for (int i = 0; i < kArpSteps; ++i) q.setParam(gateStep(i), i % 2 == 0 ? 1 : 0);
    q.prepare(kFs, 256);
    q.setTempo(120);
    q.noteOn(69, 0.8);
    std::vector<float> M;
    for (size_t t = 0; t < 2 * kStep; t += 256) { q.process(c, 2, 256); M.insert(M.end(), l.begin(), l.end()); }
    double a = 0, b = 0;
    for (size_t i = kStep / 4; i < kStep * 3 / 4; ++i) a += M[i] * M[i];
    for (size_t i = kStep + kStep / 4; i < kStep * 7 / 4; ++i) b += M[i] * M[i];
    CHECK(std::sqrt(b / a) == doctest::Approx(0.5).epsilon(0.03));
}

// the arp's own notes are not the host's: no CLAP note end for them; the host's key ends once, with its id, when it is let go (also when
// the arp took over a key already sounding, poly and mono). Before 2026-10-09 every arp step reported an end on channel 0 with id -1.
TEST_CASE("IN07 ARP: its own notes report no end; the host's key ends once, with its id, when let go (2026-10-09)") {
    auto ends = [](Processor& p) { std::vector<std::vector<int>> e; int k = 0, c = 0, i = 0; while (p.takeEnded(k, c, i)) e.push_back({k, c, i}); return e; };
    {
        Processor p; simple(p); p.prepare(kFs, 256); p.setTempo(120);
        p.setParam(ArpOn, 1); p.setParam(ArpOctaves, 2);
        p.noteOn(60, 0.8, 3, 77);
        const auto h = run(p, 4 * kStep);
        CHECK(h.size() >= 3);                 // it played (60 and 72)
        CHECK(ends(p).empty());
        p.noteOff(60, 3);
        run(p, 2 * kStep);
        const auto e = ends(p);
        REQUIRE(e.size() == 1);
        CHECK(e[0] == std::vector<int>{60, 3, 77});
    }
    for (const int mode : {static_cast<int>(Poly), static_cast<int>(Mono)}) {   // the arp switched on while the host's key sounds
        CAPTURE(mode);
        Processor p; simple(p); p.setParam(Mode, mode); p.prepare(kFs, 256); p.setTempo(120);
        p.noteOn(62, 0.8, 2, 55);
        run(p, kStep);
        CHECK(ends(p).empty());
        p.setParam(ArpOn, 1);
        run(p, 4 * kStep);
        CHECK(ends(p).empty());
        p.noteOff(62, 2);
        run(p, 2 * kStep);
        const auto e = ends(p);
        REQUIRE(e.size() == 1);
        CHECK(e[0] == std::vector<int>{62, 2, 55});
    }
}
