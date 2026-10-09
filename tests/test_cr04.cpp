#include "doctest.h"
#include "cr04/cr04.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::cr04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a tone for `on` seconds, then silence; Freeze goes On at `freezeAt` (s)
std::vector<float> toneThenFreeze(Processor& p, double hz, double on, double total, double freezeAt, double freezeOff = 1e9) {
    std::vector<float> x = sine(-18, on, hz); x.resize(static_cast<size_t>(total * kFs), 0.0f); std::vector<float> l = x, r = x; bool setOn = false, setOff = false;
    for (size_t off = 0; off < l.size(); off += 256) {
        if (!setOn && off >= freezeAt * kFs) { p.setParam(Freeze, 1); setOn = true; }
        if (!setOff && off >= freezeOff * kFs) { p.setParam(Freeze, 0); setOff = true; }
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    return l; }
}

TEST_CASE("CR04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"cr04.trigger", "cr04.freeze", "cr04.blur", "cr04.drift", "cr04.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Trigger].labels == std::vector<std::string>{"Hold", "Momentary", "Auto"}); CHECK(s[Trigger].def == 0);
    CHECK(s[Freeze].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Freeze].def == 0);
    CHECK(s[Blur].def == 40); CHECK(s[Drift].labels == std::vector<std::string>{"Off", "Slow", "Fast"}); CHECK(s[Drift].def == 1); CHECK(s[Mix].def == 50);
}
TEST_CASE("CR04 no delay; silence is silence; with Freeze Off the signal passes untouched") {
    Processor q; CHECK(q.latencySamples() == 0);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    const auto x = noise(-18, 2.0, 3); auto p = make(); const auto y = run(p, x); for (size_t i = 0; i < x.size(); i += 13) CHECK(y[i] == x[i]);
}
TEST_CASE("CR04 a frozen tone keeps playing: same pitch and level, steady") {
    for (double drift : {0.0, 1.0, 2.0}) {
        auto p = make({{Mix, 100}, {Blur, 0}, {Drift, drift}}); const auto y = toneThenFreeze(p, 440.0, 1.0, 5.0, 0.9);
        NEAR(peakHz(y, 420, 460, 2 * 48000, 4 * 48000), 440.0, drift == 2.0 ? 4.0 : 1.5);
        NEAR(rmsDb(y, 2 * 48000, 4 * 48000), -18.0, 3.0);
        // steady: the level of the 0.25 s pieces stays within 3 dB
        double mn = 1e9, mx = -1e9; for (size_t s = 2 * 48000; s + 12000 <= 4 * 48000; s += 12000) { const double l = rmsDb(y, s, s + 12000); mn = std::min(mn, l); mx = std::max(mx, l); }
        CHECK(mx - mn < (drift == 2.0 ? 4.0 : 2.5));
    }
}
TEST_CASE("CR04 Mix: 0 is the dry signal; the freeze fades out when Freeze goes Off") {
    const auto x = noise(-18, 2.0, 3); { auto p = make({{Mix, 0}}); p.setParam(Freeze, 1); const auto y = run(p, x); for (size_t i = 0; i < x.size(); i += 17) CHECK(y[i] == x[i]); }
    auto hold = make({{Mix, 100}, {Trigger, Hold}}); const auto yh = toneThenFreeze(hold, 440.0, 1.0, 5.0, 0.9, 3.0);
    CHECK(rmsDb(yh, 2 * 48000, 3 * 48000) > -25.0); CHECK(rmsDb(yh, 4 * 48000 + 24000, 5 * 48000) < -70.0);
    auto mom = make({{Mix, 100}, {Trigger, Momentary}}); const auto ym = toneThenFreeze(mom, 440.0, 1.0, 5.0, 0.9, 3.0);
    CHECK(rmsDb(ym, 3 * 48000 + 6000, 3 * 48000 + 12000) < -60.0);                     // Momentary is gone within 125 ms
    CHECK(rmsDb(yh, 3 * 48000 + 6000, 3 * 48000 + 12000) > -45.0);                     // Hold still rings (400 ms release)
}
TEST_CASE("CR04 Blur spreads the spectrum") {
    auto side = [&](double blur) { auto p = make({{Mix, 100}, {Blur, blur}, {Drift, 0}}); const auto y = toneThenFreeze(p, 440.0, 1.0, 5.0, 0.9);
        double e = 0, all = 0; for (double f = 300; f <= 600; f += 3) { const double v = std::pow(10.0, binDb(y, f, 2 * 48000, 4 * 48000) / 10.0); all += v; if (std::abs(f - 440) > 30) e += v; } return e / all; };
    CHECK(side(100) > side(0) * 3.0); CHECK(side(100) > 0.05);
}
TEST_CASE("CR04 Drift moves the pitch around a little; Off does not") {
    auto spread = [&](double drift) { auto p = make({{Mix, 100}, {Blur, 0}, {Drift, drift}}); const auto y = toneThenFreeze(p, 440.0, 1.0, 8.0, 0.9);
        double mn = 1e9, mx = -1e9; for (size_t s = 2 * 48000; s + 24000 <= 7 * 48000; s += 24000) { const double f = peakHz(y, 425, 455, s, s + 24000); mn = std::min(mn, f); mx = std::max(mx, f); } return mx - mn; };
    CHECK(spread(0) < 1.0); CHECK(spread(2) > spread(0));
}
TEST_CASE("CR04 Auto: every onset makes a new capture") {
    auto p = make({{Mix, 100}, {Trigger, AutoTrig}, {Blur, 0}, {Drift, 0}, {Freeze, 1}});
    std::vector<float> x = sine(-18, 0.5, 330); { auto t = std::vector<float>(static_cast<size_t>(1.0 * kFs), 0.0f); x.insert(x.end(), t.begin(), t.end()); auto u = sine(-18, 0.5, 550); x.insert(x.end(), u.begin(), u.end()); auto z = std::vector<float>(static_cast<size_t>(2.0 * kFs), 0.0f); x.insert(x.end(), z.begin(), z.end()); }
    const auto y = run(p, x);
    NEAR(peakHz(y, 300, 360, 48000 * 1, 48000 * 1.4), 330.0, 4.0);          // the first tone is held
    NEAR(peakHz(y, 520, 580, 48000 * 3, 48000 * 4), 550.0, 3.0);            // the second one has replaced it
    CHECK(p.captures() >= 2);
}
TEST_CASE("CR04 loud input stays finite; stereo channels are separate") {
    auto p = make({{Mix, 100}, {Blur, 100}, {Drift, 2}}); p.setParam(Freeze, 1); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make({{Mix, 100}}); std::vector<float> a = sine(-18, 3.0, 300), b = sine(-18, 3.0, 700), l = a, r = b;
    for (size_t off = 0; off < l.size(); off += 256) { if (off >= 24000) q.setParam(Freeze, 1); if (off >= 48000) { std::fill(l.begin() + static_cast<long>(off), l.end(), 0.0f); std::fill(r.begin() + static_cast<long>(off), r.end(), 0.0f); } const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; q.process(c, 2, n); }
    NEAR(peakHz(l, 280, 320, 96000, 140000), 300.0, 2.0); NEAR(peakHz(r, 680, 720, 96000, 140000), 700.0, 2.0);
}

TEST_CASE("CR04 MIDI: while Freeze is On a note-on makes a new capture (in every trigger mode); with Freeze Off it does nothing") {
    for (int trig : {0, 1, 2}) {
        auto p = make({{Trigger, trig}, {Freeze, 0}});
        const auto x = sine(-18, 1.0, 440.0); std::vector<float> l = x, r = x;
        auto go1 = [&](size_t a, size_t b) { for (size_t off = a; off < b; off += 256) { const int n = static_cast<int>(std::min<size_t>(256, b - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } };
        go1(0, 12000); p.noteOn(); go1(12000, 24000); CHECK(p.captures() == 0);       // Freeze is Off: the note is ignored
        p.setParam(Freeze, 1); go1(24000, 36000); const int before = p.captures();
        p.noteOn(); go1(36000, 48000); CHECK(p.captures() == before + 1);               // one more capture, made through the 15 ms cross-fade
    }
}
