// CS03 "set the input level" (EVO, class A): it listens for 5 s and writes the Gain so that the level after the pre-amp is -18 dBFS RMS with the peak not over -6 dBFS
#include "doctest.h"
#include "cs03/cs03.hpp"
#include "tu.hpp"
using namespace tu;
using namespace sw::cs03;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> runMono(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
    return x;
}
// a bass-like source: 55 Hz and its 2nd / 3rd harmonics, steady
std::vector<float> bass(double rmsDb, double seconds) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> x(n); double s2 = 0;
    for (size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / kFs; x[i] = static_cast<float>(std::sin(2 * kPi * 55 * t) + 0.5 * std::sin(2 * kPi * 110 * t + 0.7) + 0.25 * std::sin(2 * kPi * 165 * t + 1.9)); s2 += x[i] * x[i]; }
    const double k = std::pow(10.0, rmsDb / 20.0) / std::sqrt(s2 / static_cast<double>(n)); for (auto& v : x) v = static_cast<float>(v * k);
    return x;
}
// a drum-like source: a 180 Hz thump decaying in 25 ms every 0.4 s over a noise floor (-50 dBFS RMS)
std::vector<float> thumps(double peakDb, double seconds) {
    const size_t n = static_cast<size_t>(seconds * kFs); auto x = noise(-50.0, seconds, 5); const double a = std::pow(10.0, peakDb / 20.0);
    for (size_t i = 0; i < n; ++i) { const double t = std::fmod(static_cast<double>(i) / kFs, 0.4); x[i] += static_cast<float>(a * std::exp(-t / 0.025) * std::sin(2 * kPi * 180 * t)); }
    return x;
}
double outRms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i]; return 10 * std::log10(s / static_cast<double>(b - a) + 1e-30); }
double outPeak(const std::vector<float>& x, size_t a, size_t b) { double pk = 0; for (size_t i = a; i < b; ++i) pk = std::max(pk, static_cast<double>(std::fabs(x[i]))); return 20 * std::log10(pk + 1e-30); }
double learnGain(Processor& p, const std::vector<float>& x, size_t block = 256) {
    p.learn(); runMono(p, x, block);
    int id = -1; double v = 0; double g = -1000; int n = 0;
    while (p.takeParamWrite(id, v) == 7) { ++n; if (id == Gain) g = v; }
    return n == 1 ? g : -1000;
}
}  // namespace

TEST_CASE("CS03 Learn: a quiet bass is brought to -18 dBFS RMS after the pre-amp, its peak under -6 dBFS") {
    auto p = make();
    const auto x = bass(-38.0, 6.0);
    p.learn(); CHECK(p.learning());
    runMono(p, x);
    CHECK(!p.learning());   // 5 s are up: it stopped by itself and applied the result
    CHECK(p.learnedOk());
    int id = -1; double v = 0; CHECK(p.takeParamWrite(id, v) == 7); CHECK(id == Gain);
    INFO("Gain " << v << " (scale 30 = 0 dB)");
    CHECK(v > 30.0 + 15.0); CHECK(v < 30.0 + 25.0);   // about +20 dB
    CHECK(p.takeParamWrite(id, v) == 0);
    // the core uses it at once: the same source through it
    auto r = make(); const double g = learnGain(r, x); REQUIRE(g > 0);
    auto s = make({{Gain, g}}); const auto y = runMono(s, x);
    const size_t a = static_cast<size_t>(2 * kFs), b = x.size();
    INFO("after the pre-amp: RMS " << outRms(y, a, b) << " dBFS, peak " << outPeak(y, a, b) << " dBFS");
    NEAR(outRms(y, a, b), -18.0, 1.0); CHECK(outPeak(y, a, b) < -6.0 + 0.7);
}

TEST_CASE("CS03 Learn: a hot source is turned down; a peaky one is held by the peak ceiling") {
    {   auto p = make(); const double g = learnGain(p, bass(-10.0, 6.0)); REQUIRE(g > -999);
        NEAR(g, 30.0 - 8.0, 0.3);
        auto s = make({{Gain, g}}); const auto y = runMono(s, bass(-10.0, 6.0)); NEAR(outRms(y, static_cast<size_t>(2 * kFs), y.size()), -18.0, 1.0); }
    {   auto p = make(); const auto x = thumps(-26.0, 6.0); const double g = learnGain(p, x); REQUIRE(g > -999);
        auto s = make({{Gain, g}}); const auto y = runMono(s, x);
        const size_t a = static_cast<size_t>(2 * kFs);
        INFO("Gain " << g << ": RMS " << outRms(y, a, y.size()) << ", peak " << outPeak(y, a, y.size()));
        NEAR(outPeak(y, a, y.size()), -6.0, 0.7);        // the peak is what holds it
        CHECK(outRms(y, a, y.size()) < -18.0 - 3.0); }    // so the average stays under the target
}

TEST_CASE("CS03 Learn: silence changes nothing and writes nothing; pressing again stops and applies; the same Gain whatever the block size") {
    { auto p = make(); p.learn(); runMono(p, std::vector<float>(static_cast<size_t>(6 * kFs), 0.0f)); CHECK(!p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 0); }
    { auto p = make(); p.learn(); runMono(p, bass(-30.0, 2.0)); CHECK(p.learning()); p.learn(); CHECK(!p.learning()); CHECK(p.learnedOk()); int id; double v; CHECK(p.takeParamWrite(id, v) == 7); NEAR(v, 30.0 + 12.0, 0.5); }
    { auto a = make(), b = make(); const auto x = bass(-33.0, 6.0); const double ga = learnGain(a, x, 256), gb = learnGain(b, x, 37); REQUIRE(ga > -999); CHECK(ga == gb); }
}
