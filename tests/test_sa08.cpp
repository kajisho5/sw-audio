#include "doctest.h"
#include "sa08/sa08.hpp"
#include "tu.hpp"
#include <set>
using namespace sw;
using namespace sw::sa08;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// no hold, no filters, no dither: only the quantiser is active
Set quant(double bits) { return {{Bits, bits}, {Rate, 48000}, {Jitter, 0}, {PreFilter, 0}, {PostFilter, 0}, {Dither, 0}}; }
Set hold(double rate) { return {{Bits, 24}, {Rate, rate}, {Jitter, 0}, {PreFilter, 0}, {PostFilter, 0}, {Dither, 0}}; }
std::vector<float> tone(double amp, double f, double sec) {
    std::vector<float> x(static_cast<size_t>(sec * kFs)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * static_cast<double>(i) / kFs)); return x;
}
std::vector<size_t> runs(const std::vector<float>& y) {   // lengths of constant runs
    std::vector<size_t> r; size_t n = 1;
    for (size_t i = 1; i < y.size(); ++i) { if (y[i] == y[i - 1]) ++n; else { r.push_back(n); n = 1; } }
    return r;
}
}

TEST_CASE("SA08 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa08.bits", "sa08.rate", "sa08.jitter", "sa08.mix", "sa08.prefilter", "sa08.postfilter", "sa08.dither", "sa08.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Bits].min == 1); CHECK(s[Bits].max == 24); CHECK(s[Bits].def == 8);
    CHECK(s[Rate].min == 200); CHECK(s[Rate].max == 48000); CHECK(s[Rate].def == 11000); CHECK(s[Rate].curve == Curve::Log);
    CHECK(s[Jitter].max == 100); CHECK(s[Jitter].def == 2); CHECK(s[Mix].def == 70);
    CHECK(s[PreFilter].def == 1); CHECK(s[PostFilter].def == 0); CHECK(s[Dither].def == 0); CHECK(s[TempoLock].def == 0);
    for (int i : {PreFilter, PostFilter, Dither, TempoLock}) CHECK(s[static_cast<size_t>(i)].curve == Curve::Step);
}
TEST_CASE("SA08 no delay") { Processor p; CHECK(p.latencySamples() == 0); }
TEST_CASE("SA08 Bits: level count and signal-to-quantisation-noise ratio") {
    auto p = make(quant(8)); const auto x = tone(0.7, 997, 1.0); const auto y = run(p, x);
    std::set<float> levels(y.begin(), y.end()); CHECK(levels.size() <= 257); CHECK(levels.size() > 100);
    for (float v : y) { const double k = static_cast<double>(v) * 128.0; CHECK(std::abs(k - std::round(k)) < 1e-4); }
    double sp = 0, ne = 0; for (size_t i = 0; i < x.size(); ++i) { sp += double(x[i]) * x[i]; const double e = double(y[i]) - x[i]; ne += e * e; }
    const double snr = 10 * std::log10(sp / ne);
    CHECK(snr > 6.02 * 8 + 1.76 - 4.0); CHECK(snr < 6.02 * 8 + 1.76 + 1.0);   // full-scale sine ~49.9 dB; 0.7 amplitude is 3.1 dB lower, so the window is [45.9, 50.9]
}
TEST_CASE("SA08 Bits 24 is transparent, Bits 1 gives three levels") {
    auto p = make(quant(24)); const auto x = tone(0.5, 440, 0.5); const auto y = run(p, x);
    for (size_t i = 0; i < x.size(); ++i) CHECK(std::abs(y[i] - x[i]) < 1e-6);
    auto q = make(quant(1)); const auto z = run(q, tone(0.9, 440, 0.2));
    for (float v : z) CHECK((v == -1.0f || v == 0.0f || v == 1.0f));
}
TEST_CASE("SA08 Bits is an integer") {
    Processor a = make(quant(7.6)), b = make(quant(8)); const auto x = tone(0.4, 300, 0.2);
    CHECK(run(a, x) == run(b, x));
}
TEST_CASE("SA08 Rate: the output holds each value for fs/Rate samples") {
    auto p = make(hold(4800)); const auto y = run(p, tone(0.5, 440, 0.5));
    for (size_t i = 0; i + 9 < y.size(); i += 10) for (size_t k = 1; k < 10; ++k) CHECK(y[i + k] == y[i]);
    const auto r = runs(y); size_t long10 = 0; for (size_t n : r) if (n >= 10) ++long10; CHECK(long10 > r.size() / 2);
    auto q = make(hold(48000)); const auto x = tone(0.5, 440, 0.2); const auto z = run(q, x); for (size_t i = 0; i < x.size(); ++i) CHECK(std::abs(z[i] - x[i]) < 1e-6);   // Rate = fs: no hold
}
TEST_CASE("SA08 Pre filter removes the aliases that the hold would fold down") {
    // 10 kHz at Rate 8 kHz folds to 2 kHz
    const auto x = tone(0.5, 10000, 1.0);
    auto off = hold(8000); auto on = hold(8000); on.push_back({PreFilter, 1});
    auto a = make(off); auto b = make(on);
    const auto ya = run(a, x), yb = run(b, x);
    const double da = binDb(ya, 2000, ya.size() / 2, ya.size()), db = binDb(yb, 2000, yb.size() / 2, yb.size());
    CHECK(da > -20.0); CHECK(db < da - 25.0);
}
TEST_CASE("SA08 Post filter removes the images of the hold") {
    // 1 kHz at Rate 8 kHz: images at 7 kHz and 9 kHz
    const auto x = tone(0.5, 1000, 1.0);
    auto off = hold(8000); auto on = hold(8000); on.push_back({PostFilter, 1});
    auto a = make(off); auto b = make(on);
    const auto ya = run(a, x), yb = run(b, x);
    const double da = binDb(ya, 7000, ya.size() / 2, ya.size()), db = binDb(yb, 7000, yb.size() / 2, yb.size());
    CHECK(da > -25.0); CHECK(db < da - 15.0);
    CHECK(binDb(yb, 1000, yb.size() / 2, yb.size()) > binDb(ya, 1000, ya.size() / 2, ya.size()) - 1.0);   // the wanted tone stays
}
TEST_CASE("SA08 Jitter varies the hold length; the mean stays; renders repeat") {
    const auto x = tone(0.5, 440, 1.0);
    auto s0 = make(hold(4800)); const auto r0 = runs(run(s0, x)); std::set<size_t> d0(r0.begin(), r0.end());
    CHECK(d0.size() <= 2);
    auto j = hold(4800); j.push_back({Jitter, 100});
    auto s1 = make(j); const auto y1 = run(s1, x); const auto r1 = runs(y1); std::set<size_t> d1(r1.begin(), r1.end());
    CHECK(d1.size() >= 5);
    double mean = 0; for (size_t n : r1) mean += static_cast<double>(n); mean /= static_cast<double>(r1.size());
    CHECK(mean > 9.0); CHECK(mean < 11.0);
    auto s2 = make(j); CHECK(run(s2, x) == y1);
}
TEST_CASE("SA08 Dither: a signal below one LSB survives only with dither") {
    const double lsb = 1.0 / 128.0; const auto x = tone(0.25 * lsb, 1000, 2.0);
    auto off = quant(8); auto on = quant(8); on.push_back({Dither, 1});
    auto a = make(off); auto b = make(on);
    const auto ya = run(a, x), yb = run(b, x);
    CHECK(binDb(ya, 1000, ya.size() / 2, ya.size()) < -100.0);
    const double want = 20 * std::log10(0.25 * lsb), got = binDb(yb, 1000, yb.size() / 2, yb.size());
    CHECK(got > want - 3.0); CHECK(got < want + 3.0);
}
TEST_CASE("SA08 Tempo lock moves Rate to an integer multiple of the beat frequency") {
    Processor p = make({{Rate, 11000}}); p.setTempo(133.0); CHECK(p.effectiveRate() == doctest::Approx(11000.0));
    p.setParam(TempoLock, 1);
    const double fb = 133.0 / 60.0; const double n = std::round(11000.0 / fb);
    CHECK(p.effectiveRate() == doctest::Approx(n * fb)); CHECK(std::abs(p.effectiveRate() - 11000.0) <= fb / 2 + 1e-9);
    p.setTempo(90.0); const double fb2 = 1.5; CHECK(std::fmod(p.effectiveRate() / fb2 + 1e-9, 1.0) < 1e-6);
    Processor q = make({{Rate, 11000}, {TempoLock, 1}}); CHECK(q.effectiveRate() == doctest::Approx(11000.0));   // no tempo given
}
