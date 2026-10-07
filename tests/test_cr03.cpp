#include "doctest.h"
#include "cr03/cr03.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::cr03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double hz(int midi) { return 440.0 * std::exp2((midi - 69) / 12.0); }
// the share of the energy in the pitch classes of `mask` (any octave; spectral peaks 100 Hz .. 2 kHz of the last 0.2 s windows)
double classShare(const std::vector<float>& y, size_t a, size_t b, int mask) {
    double in = 0, all = 0;
    for (int pc = 0; pc < 12; ++pc) for (int oct = 2; oct <= 6; ++oct) { const double f = 440.0 * std::exp2((12 * oct + pc - 69 - 12 + 12) / 12.0); if (f < 100 || f > 2000) continue; const double e = std::pow(10.0, binDb(y, f, a, b) / 10.0); all += e; if (mask & (1 << pc)) in += e; }
    return in / (all + 1e-30); }
}

TEST_CASE("CR03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"cr03.mode", "cr03.grain", "cr03.density", "cr03.spray", "cr03.pitch", "cr03.spread", "cr03.mix", "cr03.freeze", "cr03.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Cloud", "Scatter", "Glitch"}); CHECK(s[Mode].def == 0);
    CHECK(s[Grain].min == 5); CHECK(s[Grain].max == 500); CHECK(s[Grain].def == 60); CHECK(s[Grain].curve == Curve::Log);
    CHECK(s[Density].min == 1); CHECK(s[Density].max == 100); CHECK(s[Density].def == 40); CHECK(s[Density].curve == Curve::Log);
    CHECK(s[Spray].def == 30); CHECK(s[Pitch].min == -24); CHECK(s[Pitch].max == 24); CHECK(s[Pitch].def == 5);
    CHECK(s[Spread].labels == std::vector<std::string>{"Mono", "Narrow", "Wide"}); CHECK(s[Spread].def == 2);
    CHECK(s[Mix].def == 50); CHECK(s[Freeze].def == 0); CHECK(s[Harmony].def == 1);
}
TEST_CASE("CR03 no delay; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("CR03 Pitch moves the grains: 0 keeps the note, +12 doubles it") {
    for (double st : {0.0, 12.0, -12.0}) {
        auto p = make({{Pitch, st}, {Harmony, 0}, {Spread, 0}, {Spray, 0}, {Density, 80}, {Grain, 80}}); const auto y = run(p, sine(-18, 4.0, 440.0));
        const double f = 440.0 * std::exp2(st / 12.0);
        NEAR(peakHz(y, f * 0.93, f * 1.07, 48000, 192000), f, f * 0.02);
    }
}
TEST_CASE("CR03 the level does not depend on the density (the grains are normalised)") {
    auto lvl = [&](double dens) { auto p = make({{Pitch, 0}, {Harmony, 0}, {Density, dens}, {Grain, 80}}); const auto y = run(p, noise(-20, 4.0, 3)); return rmsDb(y, 96000, 192000); };
    NEAR(lvl(10), lvl(80), 3.0); NEAR(lvl(30), -20.0, 4.0);
}
TEST_CASE("CR03 Spread: Mono is the same in both channels, Wide is not") {
    auto corr = [&](double spread) { auto p = make({{Spread, spread}, {Harmony, 0}}); const auto x = noise(-20, 3.0, 5); std::vector<float> l = x, r = x;
        for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
        double a = 0, b = 0, ab = 0; for (size_t i = 48000; i < l.size(); ++i) { a += double(l[i]) * l[i]; b += double(r[i]) * r[i]; ab += double(l[i]) * r[i]; } return ab / std::sqrt(a * b + 1e-30); };
    NEAR(corr(0), 1.0, 1e-6); CHECK(corr(2) < 0.9); CHECK(corr(2) < corr(1));
}
TEST_CASE("CR03 Freeze keeps the grains going after the input stops") {
    auto tail = [&](double freeze) { auto p = make({{Harmony, 0}, {Pitch, 0}, {Density, 60}}); std::vector<float> x = sine(-18, 2.0, 330); const auto z = std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f); x.insert(x.end(), z.begin(), z.end());
        std::vector<float> l = x, r = x; for (size_t off = 0; off < l.size(); off += 256) { if (off >= 96000 && freeze > 0.5) p.setParam(Freeze, 1); const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
        return rmsDb(l, 4 * 48000, 5 * 48000); };
    CHECK(tail(1) > -35.0); CHECK(tail(0) < -80.0);
}
TEST_CASE("CR03 Scatter and Glitch make sound; Spray spreads the starts") {
    for (int mode : {1, 2}) { auto p = make({{Mode, double(mode)}, {Harmony, 0}, {Pitch, 0}}); const auto y = run(p, noise(-20, 4.0, 3)); CHECK(rmsDb(y, 96000, 192000) > -40.0); for (float v : y) CHECK(std::isfinite(v)); }
    // Cloud with Spray 0: the grains read the last moment (a tone that has just stopped is gone after the grain length); with Spray 100 the tail goes on up to 0.5 s longer
    auto tail = [&](double spray) { auto p = make({{Spray, spray}, {Harmony, 0}, {Pitch, 0}, {Density, 80}, {Grain, 40}}); std::vector<float> x = sine(-18, 2.0, 330); const auto z = std::vector<float>(static_cast<size_t>(1 * kFs), 0.0f); x.insert(x.end(), z.begin(), z.end()); const auto y = run(p, x); return rmsDb(y, 2 * 48000 + 9600, 2 * 48000 + 24000); };
    CHECK(tail(100) > tail(0) + 10.0);
}
TEST_CASE("CR03 Harmony pulls the grains' pitches to the chord tones of the input") {
    // C4, E4, G4 in turn, 0.5 s each, 8 s: the chord is C E G
    std::vector<float> x; for (int k = 0; k < 16; ++k) { const auto t = sine(-18, 0.5, hz(60 + (k % 3 == 0 ? 0 : (k % 3 == 1 ? 4 : 7)))); x.insert(x.end(), t.begin(), t.end()); }
    auto share = [&](double harmony) { auto p = make({{Harmony, harmony}, {Pitch, 5}, {Spread, 0}, {Density, 60}, {Grain, 80}}); const auto y = run(p, x); return std::make_pair(classShare(y, 4 * 48000, 8 * 48000, (1 << 0) | (1 << 4) | (1 << 7)), p.chordMask()); };
    const auto on = share(1), off = share(0);
    CHECK(on.second == ((1 << 0) | (1 << 4) | (1 << 7)));
    CHECK(on.first > 0.8); CHECK(off.first < 0.6); CHECK(on.first > off.first + 0.25);
}
TEST_CASE("CR03 loud input stays finite") {
    auto p = make({{Density, 100}, {Grain, 500}, {Pitch, 24}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
}
