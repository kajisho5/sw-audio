#include "doctest.h"
#include "cr06/cr06.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::cr06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gainAt(Set set, double hz, double db = -24.0) { auto p = make(set); const auto y = run(p, sine(db, 1.5, hz)); return rmsDb(y, 36000, 72000) - db; }
}

TEST_CASE("CR06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"cr06.effect", "cr06.amount", "cr06.mix", "cr06.out", "cr06.macro"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Effect].labels == std::vector<std::string>{"Wide", "Warm", "Air", "Punch", "Space", "Lo-fi"}); CHECK(s[Effect].def == 2);
    CHECK(s[Amount].min == 0); CHECK(s[Amount].max == 10); CHECK(s[Amount].def == 0); CHECK(s[Mix].def == 100);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10); CHECK(s[Output].def == 0); CHECK(s[Macro].def == 0); CHECK_FALSE(s[Macro].automatable);
}
TEST_CASE("CR06 no delay; silence is silence; Amount 0 is the dry signal for every effect") {
    Processor q; CHECK(q.latencySamples() == 0);
    for (int fx = 0; fx < 6; ++fx) { auto p = make({{Effect, double(fx)}, {Amount, 0}}); std::vector<float> z(24000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
        const auto x = noise(-18, 1.0, 3); auto r = make({{Effect, double(fx)}, {Amount, 0}}); const auto y = run(r, x); for (size_t i = 0; i < x.size(); i += 11) CHECK(y[i] == x[i]); }
}
TEST_CASE("CR06 Wide: the side grows, the mid stays") {
    auto sideMid = [&](double amount) { auto p = make({{Effect, Wide}, {Amount, amount}}); auto a = noise(-24, 2.0, 1), b = noise(-24, 2.0, 2); std::vector<float> l = a, r = b;
        for (size_t i = 0; i < l.size(); ++i) { l[i] = 0.6f * a[i] + 0.4f * b[i]; r[i] = 0.4f * a[i] + 0.6f * b[i]; }
        for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
        double m = 0, s = 0; for (size_t i = 24000; i < l.size(); ++i) { const double mm = 0.5 * (l[i] + r[i]), ss = 0.5 * (l[i] - r[i]); m += mm * mm; s += ss * ss; } return std::make_pair(10 * std::log10(m / (l.size() - 24000.0)), 10 * std::log10(s / (l.size() - 24000.0))); };
    const auto a0 = sideMid(0), a10 = sideMid(10);
    NEAR(a10.first, a0.first, 0.2); CHECK(a10.second > a0.second + 8.0);
}
TEST_CASE("CR06 Warm: harmonics and a warmer balance") {
    auto h3 = [&](double amount) { auto p = make({{Effect, Warm}, {Amount, amount}}); const auto y = run(p, sine(-12, 2.0, 300)); return harmDb(y, 300, 3); };
    CHECK(h3(10) > h3(0) + 20.0);
    CHECK(gainAt({{Effect, Warm}, {Amount, 10}}, 80) > gainAt({{Effect, Warm}, {Amount, 10}}, 12000) + 4.0);
}
TEST_CASE("CR06 Air lifts the top") {
    CHECK(gainAt({{Effect, Air}, {Amount, 10}}, 14000) > 5.0); NEAR(gainAt({{Effect, Air}, {Amount, 10}}, 300), 0.0, 0.7);
    CHECK(gainAt({{Effect, Air}, {Amount, 5}}, 14000) < gainAt({{Effect, Air}, {Amount, 10}}, 14000) - 1.5);
}
TEST_CASE("CR06 Punch accentuates the attack") {
    // a decaying 100 Hz hit every 0.5 s: the peak against the level 0.2 s later
    auto crest = [&](double amount) { auto p = make({{Effect, Punch}, {Amount, amount}}); std::vector<float> x(static_cast<size_t>(4 * kFs), 0.0f);
        for (size_t b = 0; b < 8; ++b) for (size_t i = 0; i < 24000; ++i) x[b * 24000 + i] = static_cast<float>(0.5 * std::sin(2 * kPi * 100.0 * i / kFs) * std::exp(-static_cast<double>(i) / 4000.0));
        const auto y = run(p, x); double pk = 0, sus = 0; size_t c = 0; for (size_t b = 3; b < 8; ++b) { double m = 0; for (size_t i = 0; i < 600; ++i) m = std::max(m, double(std::abs(y[b * 24000 + i]))); pk += m; double e = 0; for (size_t i = 6000; i < 7200; ++i) e += double(y[b * 24000 + i]) * y[b * 24000 + i]; sus += std::sqrt(e / 1200.0); ++c; }
        return 20 * std::log10((pk / c) / (sus / c + 1e-12)); };
    CHECK(crest(10) > crest(0) + 3.0);
}
TEST_CASE("CR06 Space adds a tail") {
    auto tail = [&](double amount) { auto p = make({{Effect, Space}, {Amount, amount}}); std::vector<float> x(static_cast<size_t>(2 * kFs), 0.0f); for (int i = 0; i < 2400; ++i) x[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(2 * kPi * 440.0 * i / kFs)); const auto y = run(p, x); return rmsDb(y, 24000, 72000); };
    CHECK(tail(0) < -140.0); CHECK(tail(10) > -60.0); CHECK(tail(10) > tail(4) + 3.0);
}
TEST_CASE("CR06 Lo-fi: aliasing, quantisation and a dark top") {
    auto thd = [&](double amount) { auto p = make({{Effect, Lofi}, {Amount, amount}}); const auto y = run(p, sine(-6, 2.0, 1000)); return 20 * std::log10(std::pow(10.0, harmDb(y, 1000, 2) / 20) + std::pow(10.0, harmDb(y, 1000, 3) / 20) + std::pow(10.0, harmDb(y, 1000, 5) / 20)); };
    CHECK(thd(10) > thd(0) + 20.0);
    CHECK(gainAt({{Effect, Lofi}, {Amount, 10}}, 6000) < gainAt({{Effect, Lofi}, {Amount, 0.3}}, 6000) - 6.0);
}
TEST_CASE("CR06 Macro tells the screen the inner values; loud input stays finite") {
    auto p = make({{Effect, Air}, {Amount, 10}}); NEAR(p.macroValue(0), 6.0, 1e-9);
    for (int fx = 0; fx < 6; ++fx) { auto q = make({{Effect, double(fx)}, {Amount, 10}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(q, x)) CHECK(std::isfinite(v)); }
}
