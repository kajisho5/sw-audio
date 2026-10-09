#include "doctest.h"
#include "sa05/sa05.hpp"
#include "sw/svf.hpp"
#include "tu.hpp"
#include "os_helpers.hpp"
using namespace sw;
using namespace sw::sa05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// level of component f relative to the fundamental f0 (dB)
double rel(const std::vector<float>& y, double f, double f0) { return binDb(y, f, y.size() / 2, y.size()) - binDb(y, f0, y.size() / 2, y.size()); }
}

TEST_CASE("SA05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"sa05.tune", "sa05.harmonics", "sa05.mix", "sa05.lowdrive", "sa05.mode", "sa05.monolow", "sa05.evo.on", "sa05.os"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Tune].min == 1000); CHECK(s[Tune].max == 16000); CHECK(s[Tune].def == 4500); CHECK(s[Tune].curve == Curve::Log);
    CHECK(s[Harmonics].max == 100); CHECK(s[Harmonics].def == 35); CHECK(s[Mix].def == 25); CHECK(s[LowDrive].def == 0);
    CHECK(s[Mode].labels == std::vector<std::string>{"Even", "Odd", "Both"}); CHECK(s[Mode].def == 0);
    CHECK(s[MonoLow].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[MonoLow].def == 0);
    CHECK(s[AutoFill].def == 1);
}
TEST_CASE("SA05 Mode: Even adds the 2nd harmonic, Odd the 3rd; Harmonics sets the amount") {
    auto y = [](int mode, double harm) { auto p = make({{Mode, static_cast<double>(mode)}, {Harmonics, harm}, {AutoFill, 0}}); return run(p, sine(-20, 2, 6000)); };
    CHECK(rel(y(0, 100), 12000, 6000) > -12.0); CHECK(rel(y(0, 100), 18000, 6000) < -50.0);
    CHECK(rel(y(1, 100), 18000, 6000) > -12.0); CHECK(rel(y(1, 100), 12000, 6000) < -50.0);
    CHECK(rel(y(2, 100), 12000, 6000) > -12.0); CHECK(rel(y(2, 100), 18000, 6000) > -12.0);
    NEAR(rel(y(0, 100), 12000, 6000) - rel(y(0, 35), 12000, 6000), 20.0 * std::log10(100.0 / 35.0), 0.7);
    CHECK(rel(y(0, 0), 12000, 6000) < -80.0);
}
TEST_CASE("SA05 only above Tune; independent of the input level") {
    auto h2 = [](double f, double db) { auto p = make({{Harmonics, 100}, {AutoFill, 0}}); return rel(run(p, sine(db, 2, f)), 2 * f, f); };
    CHECK(h2(7000, -20) > h2(2000, -20) + 20.0);
    NEAR(h2(7000, -20), h2(7000, -45), 1.5);
}
TEST_CASE("SA05 Tune moves the starting point") {
    auto h2 = [](double tune, double f) { auto p = make({{Tune, tune}, {Harmonics, 100}, {AutoFill, 0}}); return rel(run(p, sine(-20, 2, f)), 2 * f, f); };
    CHECK(h2(2000, 3000) > h2(8000, 3000) + 20.0);
}
TEST_CASE("SA05 Low drive adds harmonics to the lows only when asked; Mono low keeps them centred") {
    auto y = [](double drive, int mono, bool leftOnly) {
        auto p = make({{LowDrive, drive}, {MonoLow, static_cast<double>(mono)}, {Harmonics, 0}, {AutoFill, 0}}); const auto t = sine(-20, 2, 100);
        auto [l, r] = run2(p, t, leftOnly ? std::vector<float>(t.size(), 0.0f) : t); return std::make_pair(l, r);
    };
    CHECK(rel(y(0, 0, false).first, 200, 100) < -70.0); CHECK(rel(y(100, 0, false).first, 200, 100) > -20.0);
    // left only: without Mono low the right channel gets nothing; with it the harmonic is shared equally
    const auto off = y(100, 0, true), on = y(100, 1, true);
    CHECK(binDb(off.second, 200, 48000, 96000) < -80.0);
    NEAR(binDb(on.second, 200, 48000, 96000), binDb(on.first, 200, 48000, 96000), 1.0); CHECK(binDb(on.second, 200, 48000, 96000) > -55.0);
}
TEST_CASE("SA05 Auto fill adds harmonics where the input lacks highs, leaves a balanced input alone") {
    // band-limited noise (flat to 8 kHz, nothing above) vs a pink-ish noise that reaches 16 kHz
    auto highEnergy = [](int fill, bool lowpassed) {
        auto x = noise(-24, 14, 3); Svf lp[2]; Svf tilt;
        if (lowpassed) { for (auto& f : lp) f.setup(Svf::Mode::LowPass, 8000.0, kFs, 0.7071, 0); for (auto& v : x) { double d = v; for (auto& f : lp) d = f.process(d); v = static_cast<float>(d); } }
        else { tilt.setup(Svf::Mode::HighShelf, 1000.0, kFs, 0.5, -9.0); for (auto& v : x) v = static_cast<float>(tilt.process(v)); }
        auto p = make({{Harmonics, 100}, {Mode, 2}, {AutoFill, static_cast<double>(fill)}}); const auto y = run(p, x);
        double e = 0; const size_t a = y.size() / 2; for (double f = 10000; f < 15000; f += 250) e += std::pow(10.0, binDb(y, f, a, y.size()) / 10.0); return 10.0 * std::log10(e);
    };
    CHECK(highEnergy(1, true) > highEnergy(0, true) + 2.0);
    NEAR(highEnergy(1, false), highEnergy(0, false), 2.0);
}
TEST_CASE("SA05 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Harmonics, 100}, {Mode, 2}, {LowDrive, 100}, {MonoLow, 1}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("SA05: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and the harmonic generator follows it") {
    const auto& s = specs();
    CHECK(std::string(s[Oversample].id) == "sa05.os"); CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
    auto alias = [](int os) { auto p = make({{Tune, 2000}, {Harmonics, 100}, {Mix, 100}, {Mode, 1}, {Oversample, double(os)}}); return ost::relDb(p, 9000, 3000, 0.3); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("9 kHz through the odd generator, alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a2 < a1 - 3.0); CHECK(ost::notWorse(a4, a2));
}
