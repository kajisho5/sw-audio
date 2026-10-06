#include "doctest.h"
#include "rv01/rv01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
struct Ir { std::vector<float> l, r; };
Ir impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; auto o = run2(p, x, x); return {o.first, o.second}; }
// Schroeder backward integration: time (s) at which the decay curve crosses -db
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double rt60(const std::vector<float>& h) { return 3.0 * (crossing(h, 30.0) - crossing(h, 10.0)); }   // T20 from -10 to -30 dB, extrapolated to 60 dB
double energy(const std::vector<float>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += double(h[i]) * h[i]; return e; }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
// a noise burst of `sec` seconds followed by silence up to `total`
std::vector<float> burst(double rmsDbfs, double sec, double total, unsigned seed = 1) { auto x = noise(rmsDbfs, sec, seed); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
}

TEST_CASE("RV01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv01.algorithm", "rv01.predelay", "rv01.size", "rv01.decay", "rv01.diffusion", "rv01.damping", "rv01.lowcut", "rv01.highcut", "rv01.erlate", "rv01.width", "rv01.mix", "rv01.freeze", "rv01.monolowend", "rv01.duck"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Algorithm].labels == std::vector<std::string>{"Hall", "Room", "Chamber", "Plate", "Ambience"}); CHECK(s[Algorithm].def == 0);
    CHECK(s[PreDelay].max == 500); CHECK(s[PreDelay].def == 24); CHECK(s[PreDelay].curve == Curve::Skew); CHECK(s[PreDelay].skew == 2);
    CHECK(s[Size].def == 74); CHECK(s[Decay].min == 0.2); CHECK(s[Decay].max == 20); CHECK(s[Decay].def == 2.8); CHECK(s[Decay].curve == Curve::Log);
    CHECK(s[Diffusion].def == 82); CHECK(s[Damping].min == 1000); CHECK(s[Damping].max == 20000); CHECK(s[Damping].def == 6500);
    CHECK(s[LowCut].min == 20); CHECK(s[LowCut].max == 1000); CHECK(s[LowCut].def == 120); CHECK(s[HighCut].min == 1000); CHECK(s[HighCut].max == 20000); CHECK(s[HighCut].def == 9000);
    CHECK(s[ErLate].def == 40); CHECK(s[Width].max == 150); CHECK(s[Width].def == 100); CHECK(s[Mix].def == 22);
    CHECK(s[Freeze].def == 0); CHECK(s[MonoLow].def == 0); CHECK(s[Duck].min == -18); CHECK(s[Duck].max == 0); CHECK(s[Duck].def == -6);
}
TEST_CASE("RV01 no delay is reported; silence gives silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV01 Decay sets the reverberation time") {
    double prev = 0;
    for (double d : {0.6, 1.5, 3.0, 6.0}) {
        auto p = make({{Decay, d}, {Damping, 20000}, {HighCut, 20000}, {LowCut, 20}, {PreDelay, 0}, {ErLate, 0}}); const auto h = impulse(p, std::max(3.0, 2.5 * d));
        const double rt = rt60(h.l); NEAR(rt / d, 1.0, 0.25);
        CHECK(rt > prev); prev = rt;
    }
}
TEST_CASE("RV01 the tail is smooth and dense (no isolated echo after 150 ms)") {
    auto p = make(); const auto h = impulse(p, 3.0);
    double early = 0, late = 0; for (size_t i = 0; i < static_cast<size_t>(0.15 * kFs); ++i) early = std::max(early, double(std::abs(h.l[i])));
    for (size_t i = static_cast<size_t>(0.15 * kFs); i < static_cast<size_t>(1.0 * kFs); ++i) late = std::max(late, double(std::abs(h.l[i])));
    CHECK(late < 0.6 * early);
    double e = 0; for (float v : h.l) e += double(v) * v; CHECK(e > 1e-4);
}
TEST_CASE("RV01 Pre-delay: nothing before it") {
    auto p = make({{PreDelay, 100}}); const auto h = impulse(p, 1.0);
    double pk = 0; for (size_t i = 0; i < static_cast<size_t>(0.099 * kFs); ++i) pk = std::max(pk, double(std::abs(h.l[i])));
    CHECK(pk < 1e-6);
    double after = 0; for (size_t i = static_cast<size_t>(0.1 * kFs); i < static_cast<size_t>(0.3 * kFs); ++i) after = std::max(after, double(std::abs(h.l[i])));
    CHECK(after > 1e-3);
}
TEST_CASE("RV01 Damping: the highs die faster") {
    auto bright = make({{Damping, 20000}, {HighCut, 20000}}); auto dark = make({{Damping, 2000}, {HighCut, 20000}});
    const auto x = burst(-20, 1.0, 3.0), yb = run(bright, x), yd = run(dark, x);
    const size_t a = static_cast<size_t>(1.4 * kFs), b = static_cast<size_t>(2.4 * kFs);
    const double diffHi = band(yb, 6000, 10000, a, b) - band(yd, 6000, 10000, a, b), diffLo = band(yb, 300, 600, a, b) - band(yd, 300, 600, a, b);
    CHECK(diffHi > diffLo + 6.0);
}
TEST_CASE("RV01 Size: a larger room builds up later") {
    auto small = make({{Size, 10}}); auto big = make({{Size, 100}});
    const auto hs = impulse(small, 2.0), hb = impulse(big, 2.0);
    const double fs = energy(hs.l, 0.0, 0.08) / energy(hs.l, 0.0, 2.0), fb = energy(hb.l, 0.0, 0.08) / energy(hb.l, 0.0, 2.0);
    CHECK(fb < fs);
}
TEST_CASE("RV01 Freeze holds the tail") {
    auto p = make({{Decay, 1.0}}); const auto x = burst(-20, 0.5, 8.0); 
    std::vector<float> first(x.begin(), x.begin() + 36000); auto y1 = run(p, first); p.setParam(Freeze, 1);
    std::vector<float> rest(x.begin() + 36000, x.end()); const auto y2 = run(p, rest);
    const double l1 = rmsDb(y2, static_cast<size_t>(1.0 * kFs), static_cast<size_t>(1.5 * kFs)), l2 = rmsDb(y2, y2.size() - 24000, y2.size());
    CHECK(l2 > l1 - 2.0);
    auto q = make({{Decay, 1.0}}); const auto z = run(q, x);
    CHECK(rmsDb(z, z.size() - 24000, z.size()) < rmsDb(z, 24000 * 2, 24000 * 2 + 24000) - 40.0);
}
TEST_CASE("RV01 Width: 0 is mono, 100 is wide") {
    auto m = make({{Width, 0}}); auto w = make({{Width, 100}});
    const auto x = burst(-20, 0.3, 1.0); const auto l0 = run2(m, x, x), l1 = run2(w, x, x);
    for (size_t i = 0; i < x.size(); ++i) CHECK(std::abs(l0.first[i] - l0.second[i]) < 1e-6);
    double sl = 0, sr = 0, sx = 0; for (size_t i = 24000; i < x.size(); ++i) { sl += double(l1.first[i]) * l1.first[i]; sr += double(l1.second[i]) * l1.second[i]; sx += double(l1.first[i]) * l1.second[i]; }
    CHECK(std::abs(sx) / std::sqrt(sl * sr) < 0.5);
}
TEST_CASE("RV01 Mono low end keeps the lows centred") {
    auto off = make({{MonoLow, 0}, {Width, 150}, {LowCut, 20}}); auto on = make({{MonoLow, 1}, {Width, 150}, {LowCut, 20}});
    const auto x = burst(-20, 1.0, 2.0); const auto a = run2(off, x, x), b = run2(on, x, x);
    std::vector<float> sa(x.size()), sb(x.size()); for (size_t i = 0; i < x.size(); ++i) { sa[i] = a.first[i] - a.second[i]; sb[i] = b.first[i] - b.second[i]; }
    const size_t t0 = 24000, t1 = x.size();
    CHECK(band(sb, 40, 80, t0, t1) < band(sa, 40, 80, t0, t1) - 15.0);
    NEAR(band(sb, 1000, 3000, t0, t1) - band(sa, 1000, 3000, t0, t1), 0.0, 1.5);
}
TEST_CASE("RV01 Low cut and High cut shape the wet signal") {
    auto flat = make({{LowCut, 20}, {HighCut, 20000}, {Damping, 20000}}); auto cut = make({{LowCut, 1000}, {HighCut, 2000}, {Damping, 20000}});
    const auto x = burst(-20, 1.0, 2.0), a = run(flat, x), b = run(cut, x); const size_t t0 = 24000, t1 = x.size();
    CHECK(band(b, 80, 160, t0, t1) < band(a, 80, 160, t0, t1) - 20.0);
    CHECK(band(b, 8000, 12000, t0, t1) < band(a, 8000, 12000, t0, t1) - 20.0);
}
TEST_CASE("RV01 Duck lowers the reverb while the source is loud and lets it back in the gaps") {
    auto d0 = make({{Duck, 0}}); auto d6 = make({{Duck, -12}});
    const auto x = burst(-8, 1.0, 3.0), a = run(d0, x), b = run(d6, x);
    const double loud = rmsDb(b, 24000, 48000) - rmsDb(a, 24000, 48000);
    CHECK(loud < -6.0); CHECK(loud > -16.0);
    NEAR(rmsDb(b, static_cast<size_t>(2.2 * kFs), static_cast<size_t>(2.7 * kFs)) - rmsDb(a, static_cast<size_t>(2.2 * kFs), static_cast<size_t>(2.7 * kFs)), 0.0, 1.5);
}
TEST_CASE("RV01 the algorithms differ: Ambience is mostly early, Hall is mostly late") {
    auto h = make({{Algorithm, 0}, {PreDelay, 0}}); auto a = make({{Algorithm, 4}, {PreDelay, 0}});
    const auto hh = impulse(h, 2.0), ha = impulse(a, 2.0);
    CHECK(energy(ha.l, 0.0, 0.05) / energy(ha.l, 0.0, 2.0) > energy(hh.l, 0.0, 0.05) / energy(hh.l, 0.0, 2.0) * 1.5);
    for (int alg = 0; alg < 5; ++alg) { auto p = make({{Algorithm, double(alg)}, {Decay, 20}, {Freeze, 1}}); for (float v : run(p, burst(-6, 0.5, 2.0))) CHECK(std::isfinite(v)); }
}
