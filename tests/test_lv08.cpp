#include "doctest.h"
#include "lv08/lv08.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv08;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
constexpr int L = Processor::kLatency;
// harmonic voice at f0 140 Hz, 12 harmonics, -26 dBFS rms, on for 0.5 s and off for 0.5 s; plus noise `room` dBFS
std::vector<float> speech(double seconds, double room, unsigned seed = 3) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> y(n); double ph = 0; const auto nz = noise(room, seconds, seed);
    double sq = 0; for (int h = 1; h <= 12; ++h) sq += 0.5 / (h * h); const double a0 = std::pow(10.0, -26.0 / 20.0) / std::sqrt(sq);
    for (size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / kFs; ph += 2 * kPi * 140.0 * (1 + 0.03 * std::sin(2 * kPi * 4 * t)) / kFs;
        double s = 0; if (std::fmod(t, 1.0) < 0.5) for (int h = 1; h <= 12; ++h) s += std::sin(h * ph) / h; y[i] = static_cast<float>(a0 * s + nz[i]); }
    return y;
}
}

TEST_CASE("LV08 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Reduction].min == -30); CHECK(s[Reduction].max == 0); CHECK(s[Reduction].def == -18);
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 1);
    CHECK(s[VoiceGuard].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[VoiceGuard].def == 2);
    CHECK(s[Keyboard].def == 1); CHECK(s[Hvac].def == 1);
    Processor q; CHECK(q.latencySamples() == 256);
}
TEST_CASE("LV08 Reduction 0 passes the signal, delayed by 256 samples") {
    auto p = make({{Reduction, 0}}); const auto x = noise(-20, 2.0, 4), y = run(p, x);
    double e = 0, r = 0; for (size_t i = 2000; i + L < x.size(); ++i) { const double d = double(y[i + L]) - x[i]; e += d * d; r += double(x[i]) * x[i]; } CHECK(10 * std::log10(e / r) < -60.0);
}
TEST_CASE("LV08 steady noise is cut by up to Reduction; Reduction limits it") {
    const auto x = noise(-50, 6.0, 5);
    auto p = make({{Reduction, -18}}); const auto y = run(p, x); const double cut = rmsDb(x, 3 * 48000, 6 * 48000) - rmsDb(y, 3 * 48000, 6 * 48000); CHECK(cut > 10.0); CHECK(cut < 20.5);
    auto q = make({{Reduction, -6}}); const auto z = run(q, x); const double cut6 = rmsDb(x, 3 * 48000, 6 * 48000) - rmsDb(z, 3 * 48000, 6 * 48000); CHECK(cut6 > 3.5); CHECK(cut6 < 6.7);
    auto o = make({{Hvac, 0}, {Keyboard, 0}}); const auto w = run(o, x); CHECK(std::abs(rmsDb(w, 3 * 48000, 6 * 48000) - rmsDb(x, 3 * 48000, 6 * 48000)) < 0.1);
}
TEST_CASE("LV08 the voice stays, the room between the words goes") {
    const auto x = speech(10.0, -48); auto p = make(); const auto y = run(p, x);
    // 8.1 .. 8.4 s is a word (0.5 s on), 8.6 .. 8.9 s a gap; the output is delayed by 256 samples
    const size_t w0 = 8 * 48000 + 4800, w1 = 8 * 48000 + 19200, g0 = 8 * 48000 + 28800, g1 = 8 * 48000 + 43200;
    CHECK(std::abs(rmsDb(y, w0 + L, w1 + L) - rmsDb(x, w0, w1)) < 2.0);
    CHECK(rmsDb(y, g0 + L, g1 + L) < rmsDb(x, g0, g1) - 8.0);
}
TEST_CASE("LV08 Voice guard High keeps more of the room under the voice than Low") {
    const auto x = speech(10.0, -40);
    auto hi = make({{VoiceGuard, 2}}); const auto a = run(hi, x); auto lo = make({{VoiceGuard, 0}}); const auto b = run(lo, x);
    const size_t w0 = 8 * 48000 + 4800, w1 = 8 * 48000 + 19200; CHECK(rmsDb(a, w0 + L, w1 + L) >= rmsDb(b, w0 + L, w1 + L) - 0.01);
}
TEST_CASE("LV08 Keyboard: strikes between the words are lowered") {
    auto x = speech(8.0, -60); const auto k = noise(-6, 8.0, 9); std::vector<size_t> at;
    for (size_t s = 24000 + 7200; s + 2000 < x.size(); s += 48000) { for (size_t j = 0; j < 480; ++j) x[s + j] += k[s + j] * std::exp(-static_cast<float>(j) / 120.0f); at.push_back(s); }
    auto on = make({{Hvac, 0}, {Keyboard, 1}}), off = make({{Hvac, 0}, {Keyboard, 0}}); const auto a = run(on, x), b = run(off, x);
    double ea = 0, eb = 0; for (size_t s : at) { if (s < 3 * 48000) continue; for (size_t j = 0; j < 1500; ++j) { ea += double(a[s + j + L]) * a[s + j + L]; eb += double(b[s + j + L]) * b[s + j + L]; } }
    CHECK(10 * std::log10(ea / eb) < -5.0);
}
TEST_CASE("LV08 Learn noise fits the profile and cuts it") {
    // coloured room: low-passed noise
    auto col = noise(-40, 8.0, 7); double lp = 0; for (auto& v : col) { lp = 0.95 * lp + 0.05 * v; v = static_cast<float>(lp * 4.0); }
    auto p = make({{Reduction, -24}}); CHECK_FALSE(p.hasLearned()); p.learnNoise(); CHECK(p.learning());
    run(p, std::vector<float>(col.begin(), col.begin() + 3 * 48000)); CHECK(p.hasLearned()); CHECK_FALSE(p.learning());
    const std::vector<float> rest(col.begin() + 3 * 48000, col.end()); const auto y = run(p, rest);
    CHECK(rmsDb(y, 48000, 4 * 48000) < rmsDb(rest, 48000, 4 * 48000) - 12.0);
    p.clearLearned(); CHECK_FALSE(p.hasLearned());
}
TEST_CASE("LV08 mono, odd blocks, loud input, silence, before prepare") {
    auto p = make(); std::vector<float> l = speech(2.0, -40); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    auto q = make(); for (float v : run(q, std::vector<float>(48000, 0.0f))) REQUIRE(v == 0.0f);
    auto r = make(); std::vector<float> loud = noise(6, 2.0, 3); for (float v : run(r, loud)) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
