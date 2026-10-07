#include "doctest.h"
#include "lv14/lv14.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv14;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV14 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Delay].min == 0); CHECK(s[Delay].max == 500); CHECK(s[Delay].def == 0); CHECK(s[Delay].curve == Curve::Skew); CHECK(s[Delay].skew == 2);
    CHECK(s[AirTemp].min == -10); CHECK(s[AirTemp].max == 40); CHECK(s[AirTemp].def == 22);
    CHECK(s[Polarity].labels == std::vector<std::string>{"Normal", "Invert"});
    CHECK(speedOfSound(22) == doctest::Approx(344.7)); CHECK(speedOfSound(0) == 331.5);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV14 Delay 0 passes the signal bit for bit; the delay is exact on whole samples") {
    const auto x = noise(-20, 1.0, 3); { auto p = make(); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]); }
    auto p = make({{Delay, 10.0}}); const auto y = run(p, x); const size_t D = 480;
    for (size_t i = D + 10; i < x.size(); i += 17) REQUIRE(y[i] == x[i - D]);
}
TEST_CASE("LV14 fractional delays interpolate; 0.01 ms steps; maximum 500 ms") {
    auto p = make({{Delay, 0.5}}); const auto x = sine(-20, 1.0, 1000), y = run(p, x);   // 24 samples
    for (size_t i = 200; i < x.size(); i += 31) NEAR(y[i], x[i - 24], 1e-6);
    auto q = make({{Delay, 0.51}}); const auto z = run(q, x);   // 24.48 samples: between the neighbours
    for (size_t i = 200; i < 400; i += 7) { const double e = 1.0 / 48000.0 * 0.0; (void)e; NEAR(z[i], 0.52 * x[i - 24] + 0.48 * x[i - 25], 1e-6); }
    auto m = make({{Delay, 500.0}}); const auto w = run(m, std::vector<float>(48000 * 2, 0.5f)); CHECK(w[23990] == 0.0f); CHECK(w[24100] == 0.5f);
}
TEST_CASE("LV14 Polarity inverts; Distance follows Delay and Air temp") {
    auto p = make({{Polarity, 1}}); const auto x = sine(-20, 0.5, 700), y = run(p, x); for (size_t i = 100; i < x.size(); i += 13) NEAR(y[i], -x[i], 1e-7);
    auto d = make({{Delay, 10.0}, {AirTemp, 22}}); CHECK(d.distanceM() == doctest::Approx(3.447).epsilon(1e-4));
    auto c = make({{Delay, 10.0}, {AirTemp, 0}}); CHECK(c.distanceM() == doctest::Approx(3.315).epsilon(1e-4));
    auto z = make(); CHECK(z.distanceM() == 0.0);
}
TEST_CASE("LV14 a delay change glides instead of jumping") {
    auto p = make({{Delay, 0.0}}); auto x = sine(-20, 2.0, 500); std::vector<float> a(x.begin(), x.begin() + 24000); run(p, a);
    p.setParam(Delay, 20.0); std::vector<float> b(x.begin() + 24000, x.end()); const auto y = run(p, b);
    double maxStep = 0; for (size_t i = 1; i < 9600; ++i) maxStep = std::max(maxStep, static_cast<double>(std::abs(y[i] - y[i - 1]))); CHECK(maxStep < 0.12);   // a sine at -20 dBFS steps by at most 0.044 per sample; a jump would be far bigger
    CHECK(p.latencySamples() == 0);
}
TEST_CASE("LV14 Measure finds the acoustic delay between the main system and the mic") {
    for (double ms : {37.5, 120.0, 7.25}) {
        const auto ref = noise(-20, 3.2, 8); const size_t lag = static_cast<size_t>(std::lround(ms * 48.0)); std::vector<float> mic(ref.size(), 0.0f); const auto room = noise(-45, 3.2, 9);
        for (size_t i = 0; i < ref.size(); ++i) mic[i] = room[i] + (i >= lag ? 0.5f * ref[i - lag] : 0.0f);
        auto p = make(); p.startMeasure(); CHECK(p.measureState() == Collecting);
        for (size_t off = 0; off < mic.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, mic.size() - off)); std::vector<float> l(mic.begin() + off, mic.begin() + off + n), r = l, s0(ref.begin() + off, ref.begin() + off + n); float* c[2] = {l.data(), r.data()}; const float* sc[1] = {s0.data()}; p.processWithSidechain(c, 2, n, sc, 1); }
        CHECK(p.measureState() == Ready); REQUIRE(p.analyse()); CHECK(p.measureState() == Done); CHECK(std::abs(p.foundMs() - ms) < 0.05); CHECK(p.confidence() > 0.3);
        int id; double v; REQUIRE(p.takeParamWrite(id, v)); CHECK(id == Delay); CHECK(std::abs(v - ms) < 0.06); CHECK_FALSE(p.takeParamWrite(id, v));
    }
}
TEST_CASE("LV14 Measure fails on silence and on unrelated signals") {
    auto p = make(); p.startMeasure(); const auto mic = noise(-30, 3.2, 3); const auto ref = noise(-30, 3.2, 4);
    for (size_t off = 0; off < mic.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, mic.size() - off)); std::vector<float> l(mic.begin() + off, mic.begin() + off + n), r = l, s0(ref.begin() + off, ref.begin() + off + n); float* c[2] = {l.data(), r.data()}; const float* sc[1] = {s0.data()}; p.processWithSidechain(c, 2, n, sc, 1); }
    CHECK_FALSE(p.analyse()); CHECK(p.measureState() == Failed); int id; double v; CHECK_FALSE(p.takeParamWrite(id, v));
    auto q = make(); q.startMeasure(); std::vector<float> z(static_cast<size_t>(3.2 * kFs), 0.0f); for (size_t off = 0; off < z.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, z.size() - off)); float* c[2] = {z.data() + off, z.data() + off}; q.process(c, 2, n); }
    CHECK_FALSE(q.analyse()); CHECK(q.measureState() == Failed);
    auto r = make(); CHECK_FALSE(r.analyse());
}
TEST_CASE("LV14 mono, odd blocks, before prepare") {
    auto p = make({{Delay, 3.0}}); std::vector<float> l = noise(-20, 1.0, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
