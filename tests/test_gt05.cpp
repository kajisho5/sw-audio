#include "doctest.h"
#include "gt05/gt05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::gt05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> ir(Processor& p, size_t n = 16384) { std::vector<float> x(n, 0.0f); x[0] = 1.0f; return run(p, x); }
double respDb(const std::vector<float>& h, double f) {
    std::complex<double> a; for (size_t i = 0; i < h.size(); ++i) a += static_cast<double>(h[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / kFs));
    return 20 * std::log10(std::abs(a) + 1e-12);
}
double peakHz(const std::vector<float>& h, double lo = 800, double hi = 14000) { double best = -1e9, bf = 0; for (double f = lo; f < hi; f *= 1.01) { const double d = respDb(h, f); if (d > best) { best = d; bf = f; } } return bf; }
double peakDb(const std::vector<float>& h) { double best = -1e9; for (double f = 800; f < 14000; f *= 1.01) best = std::max(best, respDb(h, f)); return best; }
Set rig(double imp, double cable, double pickup) { return {{Level, 0}, {Output, 0}, {Impedance, imp}, {Cable, cable}, {Pickup, pickup}, {PickupSwap, 0}}; }
}

TEST_CASE("GT05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"gt05.level", "gt05.impedance", "gt05.cable", "gt05.pickup", "gt05.output", "gt05.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Level].min == -20); CHECK(s[Level].max == 10); CHECK(s[Level].def == 0);
    CHECK(s[Impedance].steps == std::vector<double>{10000, 47000, 100000, 1000000}); CHECK(s[Impedance].labels == std::vector<std::string>{"10k", "47k", "100k", "1M"}); CHECK(s[Impedance].def == 1000000);
    CHECK(s[Cable].min == 100); CHECK(s[Cable].max == 1000); CHECK(s[Cable].curve == Curve::Log); CHECK(std::string(s[Cable].minLabel) == "Short"); CHECK(std::string(s[Cable].maxLabel) == "Long");
    CHECK(s[Pickup].def == 50); CHECK(std::string(s[Pickup].minLabel) == "Single"); CHECK(std::string(s[Pickup].maxLabel) == "Hum");
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10); CHECK(s[Output].def == 0);
}
TEST_CASE("GT05 the processor follows the circuit's transfer function") {
    for (auto cfg : std::vector<std::array<double, 3>>{{1000000, 100, 0}, {1000000, 300, 50}, {100000, 1000, 100}, {47000, 100, 50}, {10000, 100, 0}}) {
        auto p = make(rig(cfg[0], cfg[1], cfg[2])); const auto h = ir(p);
        for (double f : {100.0, 1000.0, 2500.0, 4000.0, 6000.0}) NEAR(respDb(h, f), pickupResponseDb(f, cfg[0], cfg[1], cfg[2]), 1.0);   // bilinear warp near a Q 6 peak: up to 0.9 dB measured
    }
}
TEST_CASE("GT05 no delay; Level and Output are plain gains") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make({{Level, 6}, {Output, -4}, {Impedance, 1000000}, {Cable, 100}, {Pickup, 0}}); const auto x = sine(-30, 1.0, 100), y = run(p, x);
    NEAR(binDb(y, 100, 24000, 48000) - binDb(x, 100, 24000, 48000), 2.0 + pickupResponseDb(100, 1000000, 100, 0), 0.2);
}
TEST_CASE("GT05 the peak: 2-7 kHz, lower with a longer cable and a humbucker, flattened by a low load impedance") {
    auto a = make(rig(1000000, 100, 0)); const auto ha = ir(a);
    CHECK(peakDb(ha) - respDb(ha, 100) > 8.0); CHECK(peakHz(ha) > 5000.0); CHECK(peakHz(ha) < 8500.0);
    auto b = make(rig(1000000, 1000, 100)); const auto hb = ir(b);
    CHECK(peakHz(hb) > 1500.0); CHECK(peakHz(hb) < 2400.0);
    double prev = 1e9; for (double c : {100.0, 300.0, 1000.0}) { auto p = make(rig(1000000, c, 0)); const double f = peakHz(ir(p)); CHECK(f < prev); prev = f; }
    auto d = make(rig(10000, 100, 0)); const auto hd = ir(d);
    CHECK(peakDb(hd) - respDb(hd, 100) < 1.0);
    auto m = make(rig(47000, 100, 0)); const auto hm = ir(m);
    CHECK(peakDb(hm) - respDb(hm, 100) < peakDb(ha) - respDb(ha, 100) - 3.0);
}
TEST_CASE("GT05 the load takes some level at low frequencies") {
    auto lo = make(rig(10000, 100, 50)); auto hi = make(rig(1000000, 100, 50));
    const double d = respDb(ir(lo), 100) - respDb(ir(hi), 100);
    CHECK(d < -3.5); CHECK(d > -6.5);
}
TEST_CASE("GT05 Pickup swap: the DI's own resonance is found and cut") {
    // a 'DI' with a resonance at 3.5 kHz (Q 3): white noise through a resonant low-pass
    const double w = 2 * kPi * 3500 / kFs, q = 3.0, al = std::sin(w) / (2 * q), c = std::cos(w);
    const double b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    std::vector<float> x(static_cast<size_t>(8 * kFs)); Gauss g(3); double z1 = 0, z2 = 0;
    for (auto& v : x) { const double n = 0.1 * g.gauss(); const double y = (b0 * n + z1) / a0; z1 = b1 * n - a1 * y + z2; z2 = b2 * n - a2 * y; v = static_cast<float>(y); }
    Set on = rig(1000000, 100, 0); on.push_back({PickupSwap, 1}); Set off = rig(1000000, 100, 0);
    auto pon = make(on); auto poff = make(off); const auto yon = run(pon, x), yoff = run(poff, x);
    CHECK(pon.estimatedHz() > 3000.0); CHECK(pon.estimatedHz() < 4100.0); CHECK(pon.estimatedPeakDb() > 3.0);
    const size_t a = x.size() * 3 / 4;
    auto level = [&](const std::vector<float>& y, double f) { return binDb(y, f, a, y.size()); };
    // the lows are untouched, the 3.5 kHz band comes down
    NEAR(level(yon, 300) - level(yoff, 300), 0.0, 1.0);
    CHECK(level(yon, 3500) < level(yoff, 3500) - 4.0);
    Processor idle = make(off); CHECK(idle.estimatedHz() == 0.0);
}
TEST_CASE("GT05 silence, extremes, and independent channels") {
    auto p = make(rig(1000000, 1000, 100)); std::vector<float> z(24000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
    for (double imp : {10000.0, 1000000.0}) for (double cab : {100.0, 1000.0}) for (double pk : {0.0, 100.0}) { auto q = make([&] { auto s = rig(imp, cab, pk); s.push_back({PickupSwap, 1}); return s; }()); for (float v : run(q, noise(-10, 0.3))) CHECK(std::isfinite(v)); }
    auto r = make(rig(1000000, 100, 50)); const auto l = noise(-20, 0.3); std::vector<float> sil(l.size(), 0.0f); const auto o = run2(r, l, sil); for (float v : o.second) CHECK(v == 0.0f);
}
