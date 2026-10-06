#include "doctest.h"
#include "sa01/sa01.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::sa01;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
double gain(Set s, double f, double db = -40, double sec = 2) { auto p = make(s); return rmsDb(run(p, sine(db, sec, f))) - db; }
// phase wobble (rad, std dev) of a tone around its mean frequency: complex demodulation + 20 ms smoothing
double phaseWobble(const std::vector<float>& y, double f) {
    std::vector<double> ph; std::complex<double> acc; const int w = 960;
    for (size_t i = 0; i < y.size(); ++i) { acc += static_cast<double>(y[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * i / kFs)); if (i % w == w - 1) { ph.push_back(std::arg(acc)); acc = 0; } }
    for (size_t i = 1; i < ph.size(); ++i) { while (ph[i] - ph[i - 1] > kPi) ph[i] -= 2 * kPi; while (ph[i] - ph[i - 1] < -kPi) ph[i] += 2 * kPi; }
    // remove the linear trend (a constant frequency offset), then the std dev
    const size_t n = ph.size(); double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) { sx += i; sy += ph[i]; sxx += i * double(i); sxy += i * ph[i]; }
    const double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n; double v = 0;
    for (size_t i = 0; i < n; ++i) { const double r = ph[i] - (a + b * i); v += r * r; }
    return std::sqrt(v / n);
}
}

TEST_CASE("SA01 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa01.speed", "sa01.formula", "sa01.input", "sa01.saturation", "sa01.wow", "sa01.flutter", "sa01.hiss", "sa01.output", "sa01.repro"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Speed].steps == std::vector<double>{7.5, 15, 30}); CHECK(s[Speed].def == 15);
    CHECK(s[Formula].labels == std::vector<std::string>{"A", "B", "C"}); CHECK(s[Formula].def == 0);
    CHECK(s[Input].min == -12); CHECK(s[Input].max == 12); CHECK(s[Input].def == 0);
    CHECK(s[Saturation].min == 0); CHECK(s[Saturation].max == 10); CHECK(s[Saturation].def == 3);
    CHECK(s[Wow].max == 10); CHECK(s[Wow].def == 0); CHECK(s[Flutter].max == 10); CHECK(s[Flutter].def == 0);
    CHECK(s[Hiss].min == -90); CHECK(s[Hiss].max == -50); CHECK(std::string(s[Hiss].minLabel) == "Off"); CHECK(std::string(s[Hiss].maxLabel) == "Max"); CHECK(s[Hiss].def == -90);
    CHECK(s[Output].min == -10); CHECK(s[Output].max == 10);
    CHECK(s[Repro].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[Repro].def == 1);
    for (const auto& p : s) CHECK(p.automatable);
}
TEST_CASE("SA01 the delay is fixed at 48 samples @48 kHz whatever Wow and Flutter do") {
    Processor p; CHECK(p.latencySamples() == 48);
    p.setParam(Wow, 10); p.setParam(Flutter, 10); CHECK(p.latencySamples() == 48);
    Processor q; q.prepare(96000.0, 256); CHECK(q.latencySamples() == 96);
    // a click comes out 48 samples late (centre of the modulated delay) when Wow / Flutter are 0
    auto r = make({{Repro, 0}, {Saturation, 0}}); std::vector<float> x(2000, 0.0f); x[500] = 0.01f;
    const auto y = run(r, x); size_t at = 0; for (size_t i = 0; i < y.size(); ++i) if (std::abs(y[i]) > std::abs(y[at])) at = i;
    CHECK(at >= 548); CHECK(at <= 556);   // + the 2x oversampler's few samples of group delay (not reported, like every OS stage)
}
TEST_CASE("SA01 Saturation: odd harmonics grow with it, nothing at 0; small signals pass at unity") {
    auto h3 = [](double s, double db = -18) { auto p = make({{Repro, 0}, {Saturation, s}}); return harmDb(run(p, sine(db, 2, 1000)), 1000, 3); };
    CHECK(h3(0) < -60.0); CHECK(h3(3) > -50.0); CHECK(h3(3) < -25.0); CHECK(h3(10) > h3(3) + 8.0); CHECK(h3(10) > -25.0);
    auto p = make({{Repro, 0}, {Saturation, 10}}); const auto y = run(p, sine(-18, 2, 1000));
    CHECK(harmDb(y, 1000, 3) > harmDb(y, 1000, 2) + 6.0);
    NEAR(gain({{Repro, 0}, {Saturation, 10}}, 1000, -50), 0.0, 0.2);
}
TEST_CASE("SA01 Formula changes the headroom: B is cleaner, C dirtier at the same level") {
    auto h3 = [](int f) { auto p = make({{Repro, 0}, {Saturation, 5}, {Formula, static_cast<double>(f)}}); return harmDb(run(p, sine(-18, 2, 1000)), 1000, 3); };
    CHECK(h3(1) < h3(0) - 2.0); CHECK(h3(2) > h3(0) + 2.0);
}
TEST_CASE("SA01 Repro: head bump at low speed settings, high-frequency loss at 7.5 ips; Off is flat") {
    auto rel = [](Set s, double f) { return gain(s, f) - gain(s, 1000); };
    CHECK(rel({{Saturation, 0}, {Speed, 15}}, 70) > 1.0);
    NEAR(rel({{Saturation, 0}, {Repro, 0}}, 70), 0.0, 0.3);
    CHECK(rel({{Saturation, 0}, {Speed, 7.5}}, 15000) < -3.0);
    CHECK(rel({{Saturation, 0}, {Speed, 30}}, 15000) > -1.5);
    CHECK(rel({{Saturation, 0}, {Speed, 7.5}}, 50) > 1.0); CHECK(rel({{Saturation, 0}, {Speed, 30}}, 50) < rel({{Saturation, 0}, {Speed, 7.5}}, 50));
}
TEST_CASE("SA01 Wow and Flutter wobble the pitch (variable delay)") {
    auto wob = [](double wow, double flut) { auto p = make({{Repro, 0}, {Saturation, 0}, {Wow, wow}, {Flutter, flut}}); return phaseWobble(run(p, sine(-20, 8, 1000)), 1000); };
    CHECK(wob(0, 0) < 0.002); CHECK(wob(10, 0) > 0.05); CHECK(wob(0, 10) > 0.003); CHECK(wob(10, 0) > wob(3, 0) * 1.5);
}
TEST_CASE("SA01 Hiss: Off is true silence, Max is -50 dBFS, in between scales") {
    auto lvl = [](double h) { auto p = make({{Hiss, h}}); return rmsDb(run(p, std::vector<float>(48000 * 2, 0.0f))); };
    { auto p = make(); for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    NEAR(lvl(-50), -50.0, 1.5); NEAR(lvl(-70), -70.0, 1.5);
}
TEST_CASE("SA01 Calibrate: 5 s of input sets Input so that the mean level sits at 0 VU = -18 dBFS") {
    auto p = make({{Repro, 0}});
    p.startCalibrate(); CHECK(p.calibrating());
    run(p, noise(-24, 6, 3));
    CHECK_FALSE(p.calibrating());
    NEAR(p.inputDb(), 6.0, 0.7);
    int id = -1; double v = 0; const int f = p.takeParamWrite(id, v);
    CHECK(f == 7); CHECK(id == Input); NEAR(v, 6.0, 0.7);
    auto q = make(); q.startCalibrate(); run(q, noise(-50, 6, 3)); NEAR(q.inputDb(), 12.0, 1e-9);   // clamped to +12
}
TEST_CASE("SA01 silence stays silent without Hiss; extreme input finite") {
    auto p = make({{Wow, 10}, {Flutter, 10}, {Saturation, 10}});
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
}
