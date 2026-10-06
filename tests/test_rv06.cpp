#include "doctest.h"
#include "rv06/rv06.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv06;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double rt60(const std::vector<float>& h) { return 3.0 * (crossing(h, 30.0) - crossing(h, 10.0)); }
std::vector<float> tone(double rms, double f, double sec, double total) { auto x = sine(rms, sec, f); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
double at(const std::vector<float>& y, double f, double a, double b) { return binDb(y, f, static_cast<size_t>(a * kFs), static_cast<size_t>(b * kFs)); }
std::vector<float> burst(double rmsDbfs, double sec, double total, unsigned seed = 1) { auto x = noise(rmsDbfs, sec, seed); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
Set base() { return {{Duck, 0}}; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
}

TEST_CASE("RV06 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv06.decay", "rv06.shimmer", "rv06.interval", "rv06.mix", "rv06.evo.on", "rv06.duck"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Decay].min == 1); CHECK(s[Decay].max == 60); CHECK(s[Decay].def == 12); CHECK(s[Decay].curve == Curve::Log);
    CHECK(s[Shimmer].def == 60); CHECK(s[Interval].labels == std::vector<std::string>{"Octave", "Fifth", "Both"}); CHECK(s[Interval].def == 0);
    CHECK(s[Mix].def == 35); CHECK(s[Freeze].def == 0); CHECK(s[Duck].def == 1);
}
TEST_CASE("RV06 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV06 Decay sets the reverberation time (Shimmer 0)") {
    for (double d : {1.0, 3.0, 6.0}) {
        auto p = make(with(base(), {{Decay, d}, {Shimmer, 0}})); std::vector<float> x(static_cast<size_t>(std::max(3.0, 2.5 * d) * kFs), 0.0f); x[0] = 1;
        const auto h = run(p, x); NEAR(rt60(h) / d, 1.0, 0.3);
    }
}
TEST_CASE("RV06 the tail climbs: an octave above (Octave), a fifth above (Fifth), both (Both)") {
    const auto x = tone(-18, 440.0, 0.3, 3.0);
    auto none = make(with(base(), {{Shimmer, 0}})); const auto y0 = run(none, x);
    auto oct = make(with(base(), {{Shimmer, 100}, {Interval, Octave}})); const auto yo = run(oct, x);
    auto fif = make(with(base(), {{Shimmer, 100}, {Interval, Fifth}})); const auto yf = run(fif, x);
    auto both = make(with(base(), {{Shimmer, 100}, {Interval, Both}})); const auto yb = run(both, x);
    const double a = 0.6, b = 2.0;
    CHECK(at(yo, 880, a, b) > at(y0, 880, a, b) + 20.0); CHECK(at(yo, 660, a, b) < at(yo, 880, a, b) - 10.0);
    CHECK(at(yf, 660, a, b) > at(y0, 660, a, b) + 20.0); CHECK(at(yf, 880, a, b) < at(yf, 660, a, b) - 10.0);
    CHECK(at(yb, 880, a, b) > at(y0, 880, a, b) + 15.0); CHECK(at(yb, 660, a, b) > at(y0, 660, a, b) + 15.0);
    CHECK(at(y0, 440, a, b) > at(y0, 880, a, b) + 30.0);   // without Shimmer the tail stays at the pitch that went in
}
TEST_CASE("RV06 Shimmer sets how much climbs") {
    const auto x = tone(-18, 440.0, 0.3, 3.0);
    auto lo = make(with(base(), {{Shimmer, 30}})); auto hi = make(with(base(), {{Shimmer, 100}}));
    CHECK(at(run(hi, x), 880, 0.6, 2.0) > at(run(lo, x), 880, 0.6, 2.0) + 6.0);
}
TEST_CASE("RV06 Freeze holds the tail as a pad") {
    auto fz = make(with(base(), {{Decay, 2.0}})); auto off = make(with(base(), {{Decay, 2.0}}));
    const auto x = burst(-20, 0.5, 9.0);
    std::vector<float> first(x.begin(), x.begin() + 28800), rest(x.begin() + 28800, x.end());
    run(fz, first); fz.setParam(Freeze, 1); const auto y = run(fz, rest);
    const double l1 = rmsDb(y, static_cast<size_t>(1.0 * kFs), static_cast<size_t>(1.5 * kFs)), l2 = rmsDb(y, y.size() - 24000, y.size());
    CHECK(l2 > l1 - 3.0);
    const auto z = run(off, x); CHECK(rmsDb(z, z.size() - 24000, z.size()) < rmsDb(z, 24000 * 2, 24000 * 2 + 24000) - 30.0);
}
TEST_CASE("RV06 Duck lowers the tail while the source is loud") {
    auto off = make({{Duck, 0}}); auto on = make({{Duck, 1}});
    const auto x = burst(-8, 1.0, 3.0), a = run(off, x), b = run(on, x);
    const double loud = rmsDb(b, 24000, 48000) - rmsDb(a, 24000, 48000);
    CHECK(loud < -3.0); CHECK(loud > -9.0);
    NEAR(rmsDb(b, static_cast<size_t>(2.3 * kFs), static_cast<size_t>(2.8 * kFs)) - rmsDb(a, static_cast<size_t>(2.3 * kFs), static_cast<size_t>(2.8 * kFs)), 0.0, 1.5);
}
TEST_CASE("RV06 the loop stays bounded at the longest decay and the strongest shimmer") {
    for (int iv = 0; iv < 3; ++iv) {
        auto p = make(with(base(), {{Decay, 60}, {Shimmer, 100}, {Interval, double(iv)}})); const auto y = run(p, burst(-3, 2.0, 20.0));
        double mx = 0; for (float v : y) { CHECK(std::isfinite(v)); mx = std::max(mx, double(std::abs(v))); } CHECK(mx < 4.0);
    }
}
