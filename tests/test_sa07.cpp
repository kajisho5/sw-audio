#include "doctest.h"
#include "sa07/sa07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::sa07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// the Era preset writes Crackle / Wow / Bandwidth; tests that want a clean slate set them afterwards
Set clean() { return {{Era, 2}, {Crackle, 0}, {Dust, 0}, {Wow, 0}, {Bandwidth, 20000}, {Mono, 0}}; }
Set with(Set a, Set b) { a.insert(a.end(), b.begin(), b.end()); return a; }
double wobble(const std::vector<float>& y, double f) {
    std::vector<double> ph; std::complex<double> acc; const int w = 960;
    for (size_t i = 0; i < y.size(); ++i) { acc += static_cast<double>(y[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); if (i % w == w - 1) { ph.push_back(std::arg(acc)); acc = 0; } }
    for (size_t i = 1; i < ph.size(); ++i) { while (ph[i] - ph[i - 1] > kPi) ph[i] -= 2 * kPi; while (ph[i] - ph[i - 1] < -kPi) ph[i] += 2 * kPi; }
    const size_t n = ph.size(); double sx = 0, sy = 0, sxx = 0, sxy = 0; for (size_t i = 0; i < n; ++i) { sx += i; sy += ph[i]; sxx += i * double(i); sxy += i * ph[i]; }
    const double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n; double v = 0; for (size_t i = 0; i < n; ++i) { const double r = ph[i] - (a + b * i); v += r * r; } return std::sqrt(v / n);
}
}

TEST_CASE("SA07 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa07.era", "sa07.crackle", "sa07.dust", "sa07.wow", "sa07.bandwidth", "sa07.mono", "sa07.mix", "sa07.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Era].labels == std::vector<std::string>{"1950", "1970", "1990", "Tape"}); CHECK(s[Era].def == 1);
    CHECK(s[Crackle].max == 10); CHECK(s[Crackle].def == 0); CHECK(s[Dust].def == 0); CHECK(s[Wow].def == 0);
    CHECK(s[Bandwidth].min == 3000); CHECK(s[Bandwidth].max == 20000); CHECK(s[Bandwidth].def == 20000); CHECK(s[Bandwidth].curve == Curve::Log);
    CHECK(std::string(s[Bandwidth].minLabel) == "Narrow"); CHECK(std::string(s[Bandwidth].maxLabel) == "Full");
    CHECK(s[Mono].max == 100); CHECK(s[Mono].def == 0); CHECK(std::string(s[Mono].minLabel) == "Off"); CHECK(s[Mix].def == 100);
}
TEST_CASE("SA07 the delay is 48 samples @48 kHz whatever is set") {
    Processor p; CHECK(p.latencySamples() == 48); p.setParam(Wow, 10); CHECK(p.latencySamples() == 48);
    Processor q; q.prepare(96000.0, 256); CHECK(q.latencySamples() == 96);
    auto r = make(clean()); std::vector<float> x(2000, 0.0f); x[500] = 0.01f; const auto y = run(r, x);
    size_t at = 0; for (size_t i = 0; i < y.size(); ++i) if (std::abs(y[i]) > std::abs(y[at])) at = i;
    CHECK(at >= 548); CHECK(at <= 552);
}
TEST_CASE("SA07 Bandwidth is the upper limit; Full passes the highs") {
    auto g = [](double bw, double f) { auto p = make(with(clean(), {{Bandwidth, bw}})); return rmsDb(run(p, sine(-30, 2, f))) - -30.0; };
    NEAR(g(20000, 12000), 0.0, 0.8); CHECK(g(3000, 12000) < -25.0); NEAR(g(3000, 500), 0.0, 1.0); CHECK(g(8000, 12000) < -8.0);
}
TEST_CASE("SA07 Crackle and Dust come from nothing, scale with the knob, and are the same every time") {
    auto noiseOf = [](const char* what, double v) { auto p = make(with(clean(), {{what[0] == 'c' ? Crackle : Dust, v}})); return run(p, std::vector<float>(48000 * 4, 0.0f)); };
    for (float v : noiseOf("crackle", 0)) REQUIRE(v == 0.0f);
    const auto c10 = noiseOf("crackle", 10), c3 = noiseOf("crackle", 3), d10 = noiseOf("dust", 10);
    CHECK(rmsDb(c10, 0, c10.size()) > -65.0);   // about -61 dBFS RMS: pops, not a noise floor CHECK(peakDb(c10, 0, c10.size()) > -45.0); CHECK(rmsDb(c3, 0, c3.size()) < rmsDb(c10, 0, c10.size()) - 3.0);
    CHECK(rmsDb(d10, 0, d10.size()) > -75.0); CHECK(peakDb(d10, 0, d10.size()) < peakDb(c10, 0, c10.size()));
    CHECK(noiseOf("crackle", 10) == c10);
    int pops = 0; for (size_t i = 1; i < c10.size(); ++i) if (std::abs(c10[i]) > 0.01f && std::abs(c10[i - 1]) <= 0.01f) ++pops;
    CHECK(pops > 20); CHECK(pops < 4 * 200);   // some tens of pops per second at most
}
TEST_CASE("SA07 Wow wobbles the pitch") {
    auto w = [](double v) { auto p = make(with(clean(), {{Wow, v}})); return wobble(run(p, sine(-20, 8, 1000)), 1000); };
    CHECK(w(0) < 0.002); CHECK(w(10) > 0.05); CHECK(w(10) > w(3) * 1.5);
}
TEST_CASE("SA07 Mono folds the sides in") {
    auto side = [](double m) { auto p = make(with(clean(), {{Mono, m}})); const auto t = sine(-20, 2, 800); std::vector<float> r(t.size()); for (size_t i = 0; i < t.size(); ++i) r[i] = -t[i]; auto [yl, yr] = run2(p, t, r); return rmsDb(yl) - -20.0; };
    NEAR(side(0), 0.0, 0.8); CHECK(side(100) < -40.0); NEAR(side(50) - side(0), -6.0, 1.0);
}
TEST_CASE("SA07 Era writes Crackle, Wow and Bandwidth in one move") {
    Processor p; p.prepare(kFs, 256);
    p.setParam(Era, 0);   // 1950
    std::vector<std::pair<int, double>> w; int id = 0; double v = 0;
    for (int guard = 0; guard < 8; ++guard) { const int f = p.takeParamWrite(id, v); if (!f) break; if (f & 2) w.push_back({id, v}); }
    REQUIRE(w.size() == 3);
    bool c = false, wo = false, b = false; for (auto& e : w) { if (e.first == Crackle) c = e.second > 3.0; if (e.first == Wow) wo = e.second > 2.0; if (e.first == Bandwidth) b = e.second < 6000.0; }
    CHECK(c); CHECK(wo); CHECK(b);
    CHECK(p.crackle() > 3.0); CHECK(p.takeParamWrite(id, v) == 0);
    Processor q; q.prepare(kFs, 256); q.setParam(Era, 2); int n = 0; while (q.takeParamWrite(id, v)) ++n; CHECK(n >= 3);
    // a value set after the Era (same event batch, or by hand) cancels that preset write: only the other two are reported
    Processor r; r.prepare(kFs, 256); r.setParam(Era, 0); r.setParam(Crackle, 1.0);
    int m = 0; bool sawCrackle = false; while (r.takeParamWrite(id, v)) { ++m; if (id == Crackle) sawCrackle = true; } CHECK(m == 2); CHECK_FALSE(sawCrackle);
    p.setParam(Crackle, 1.0); CHECK(p.crackle() == doctest::Approx(1.0));
}
TEST_CASE("SA07 silence stays silent when nothing is added; extreme input finite") {
    auto p = make(clean()); for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    auto q = make(with(clean(), {{Wow, 10}, {Crackle, 10}, {Dust, 10}, {Mono, 100}, {Bandwidth, 3000}}));
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(q, x)) REQUIRE(std::isfinite(v));
}
