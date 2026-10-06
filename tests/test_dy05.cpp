#include "doctest.h"
#include "dy05/dy05.hpp"
#include "sw/svf.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy05;
using namespace tu;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// "s" sound: noise band-passed 8-12 kHz (well above Freq 6.5 kHz and its crossover transition)
std::vector<float> sibilant(double rmsDbfs, double seconds, unsigned seed = 3) {
    auto x = noise(0, seconds, seed); Svf hp[2], lp[2];
    for (auto& f : hp) f.setup(Svf::Mode::HighPass, 8000, kFs, 0.7071, 0); for (auto& f : lp) f.setup(Svf::Mode::LowPass, 12000, kFs, 0.7071, 0);
    for (auto& v : x) { double d = v; for (auto& f : hp) d = f.process(d); for (auto& f : lp) d = f.process(d); v = static_cast<float>(d); }
    const double g = std::pow(10.0, rmsDbfs / 20) / std::pow(10.0, rmsDb(x, 0, x.size()) / 20); for (auto& v : x) v = static_cast<float>(v * g); return x;
}
std::vector<float> pulses(double f0, double seconds, double amp) { std::vector<float> x(static_cast<size_t>(seconds * kFs), 0.0f); const double per = kFs / f0; for (double t = 0; t < static_cast<double>(x.size()); t += per) x[static_cast<size_t>(t)] = static_cast<float>(amp); return x; }
std::vector<float> mixed(const std::vector<float>& a, const std::vector<float>& b) { std::vector<float> o(a.size()); for (size_t i = 0; i < a.size(); ++i) o[i] = a[i] + b[i]; return o; }
}

TEST_CASE("DY05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dy05.mode", "dy05.freq", "dy05.thresh", "dy05.range", "dy05.lookahead", "dy05.listen", "dy05.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Wide", "Split"}); CHECK(s[Mode].def == 1);
    CHECK(s[Freq].min == 2000); CHECK(s[Freq].max == 16000); CHECK(s[Freq].def == 6500); CHECK(s[Freq].curve == Curve::Log);
    CHECK(s[Threshold].min == -60); CHECK(s[Threshold].max == 0); CHECK(s[Threshold].def == -24);
    CHECK(s[Range].min == -24); CHECK(s[Range].max == 0); CHECK(s[Range].def == -8);
    CHECK(s[Lookahead].min == 0); CHECK(s[Lookahead].max == 5); CHECK(s[Lookahead].def == 2);
    CHECK(s[Listen].def == 0); CHECK_FALSE(s[Listen].automatable);
    CHECK(s[Pitch].def == 1); CHECK(s[Pitch].automatable);
}
TEST_CASE("DY05 latency is the Lookahead: 2 ms = 96 samples @48 kHz, up to 240, 0 for 0 (applied at prepare)") {
    Processor p; CHECK(p.latencySamples() == 96);
    p.setParam(Lookahead, 5); CHECK(p.latencySamples() == 240);
    p.setParam(Lookahead, 0); CHECK(p.latencySamples() == 0);
    p.prepare(96000.0, 256); p.setParam(Lookahead, 2); CHECK(p.latencySamples() == 192);
}
TEST_CASE("DY05 below the threshold the level is unchanged (flat crossover sum), and Wide is bit-exact") {
    for (double f : {100.0, 1000.0, 6500.0, 12000.0}) { auto p = make({{Threshold, 0}}); NEAR(rmsDb(run(p, sine(-30, 1, f))), -30.0, 0.1); }
    auto p = make({{Threshold, 0}, {Mode, 0}});
    const auto x = sibilant(-30, 1); const auto y = run(p, x);
    const int L = p.latencySamples();
    for (size_t i = 2000; i < x.size(); ++i) REQUIRE(y[i] == x[i - static_cast<size_t>(L)]);
}
TEST_CASE("DY05 Split: lowers the sibilance by Range, leaves a vowel alone, independent of loudness") {
    for (double lvl : {-20.0, -40.0}) {
        auto p = make({{Freq, 4000}, {Threshold, -60}, {Range, -12}, {Pitch, 0}});   // Freq 4 kHz: the 8-12 kHz band is an octave above it
        const auto x = sibilant(lvl, 2), y = run(p, x);
        const size_t a = x.size() / 2, L = static_cast<size_t>(p.latencySamples());
        NEAR(rmsDb(y, a, x.size()) - rmsDb(x, a - L, x.size() - L), -12.0, 2.0);
    }
    auto p = make({{Threshold, -60}, {Range, -12}, {Pitch, 0}});
    const auto v = sine(-10, 2, 300), y = run(p, v);
    NEAR(rmsDb(y, 48000, 96000) - rmsDb(v, 48000, 96000), 0.0, 0.3);
}
TEST_CASE("DY05 Wide lowers everything, Split only above Freq") {
    auto level = [](int mode, double f) {
        auto p = make({{Mode, static_cast<double>(mode)}, {Threshold, -60}, {Range, -12}, {Pitch, 0}});
        const auto s = sibilant(-20, 2), v = sine(-20, 2, f);
        const auto y = run(p, mixed(s, v)); return binDb(y, f, 48000, 96000);   // level of the added tone
    };
    NEAR(level(1, 300), -20.0 + 3.01, 1.0);                                   // Split: the 300 Hz tone is untouched (-20 dBFS RMS = -17 dBFS amplitude)
    CHECK(level(0, 300) < level(1, 300) - 8.0);                               // Wide: it goes down with the rest
}
TEST_CASE("DY05 Lookahead starts the reduction at the head of the sibilant") {
    auto head = [](double la) {
        auto p = make({{Lookahead, la}, {Threshold, -60}, {Range, -18}, {Pitch, 0}});
        std::vector<float> x(48000, 0.0f); const auto s = sibilant(-20, 0.5); std::copy(s.begin(), s.end(), x.begin() + 24000);
        const auto y = run(p, x); const size_t L = static_cast<size_t>(p.latencySamples());
        return peakDb(y, 24000 + L, 24000 + L + 48) - peakDb(x, 24000, 24000 + 48);   // first millisecond of the burst
    };
    CHECK(head(2) < head(0) - 3.0);
}
TEST_CASE("DY05 Listen outputs the detection band") {
    auto p = make({{Listen, 1}});
    CHECK(rmsDb(run(p, sine(-10, 1, 300))) < -60.0);
    auto q = make({{Listen, 1}});
    NEAR(rmsDb(run(q, sine(-10, 1, 10000))), -10.0, 1.0);
}
TEST_CASE("DY05 Pitch follow: a bright voiced sound is left alone, unvoiced hiss is still reduced") {
    auto gr = [](int pitch, const std::vector<float>& x) { auto p = make({{Pitch, static_cast<double>(pitch)}, {Threshold, -60}, {Range, -12}}); const auto y = run(p, x); return rmsDb(y, y.size() / 2, y.size()) - rmsDb(x, x.size() / 2 - static_cast<size_t>(p.latencySamples()), x.size() - static_cast<size_t>(p.latencySamples())); };
    const auto voiced = pulses(220, 3, 0.2);
    CHECK(gr(1, voiced) > gr(0, voiced) + 3.0);
    const auto hiss = sibilant(-20, 3);
    NEAR(gr(1, hiss), gr(0, hiss), 1.5);
}
TEST_CASE("DY05 stays finite and silent for silence and extreme input; other rates prepare") {
    auto p = make({{Range, -24}, {Threshold, -60}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    for (double fs : {44100.0, 96000.0, 192000.0}) { Processor q; q.prepare(fs, 256); std::vector<float> a(256, 0.1f), b = a; float* c[2] = {a.data(), b.data()}; q.process(c, 2, 256); CHECK(std::isfinite(a[255])); }
}
