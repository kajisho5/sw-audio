#include "doctest.h"
#include "rs07/rs07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rs07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// words: a vowel-like tone at -16 dBFS for 0.5 s, a gap of 0.5 s with a -60 dBFS room tone
std::vector<float> speechLike(double seconds) {
    const size_t n = static_cast<size_t>(seconds * kFs); auto a = sine(-16, seconds, 180), b = sine(-24, seconds, 540), room = noise(-60, seconds, 3); std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i) { const bool word = (i / 24000) % 2 == 0; y[i] = room[i] + (word ? a[i] + b[i] : 0.0f); }
    return y;
}
bool inGap(size_t i) { return (i / 24000) % 2 == 1; }
// a click of `w` samples (random, sigma `amp`) at s
void addClick(std::vector<float>& y, size_t s, int w, float amp, unsigned seed) { Gauss g(seed); for (int k = 0; k < w; ++k) y[s + static_cast<size_t>(k)] += amp * static_cast<float>(g.gauss()); }
double errAt(const std::vector<float>& y, const std::vector<float>& x, const std::vector<size_t>& at, int w) { double s = 0; for (size_t p : at) for (int k = -2; k < w + 2; ++k) { const double d = double(y[p + static_cast<size_t>(k + 0) + kLatency]) - x[p + static_cast<size_t>(k + 0)]; s += d * d; } return 10 * std::log10(s / static_cast<double>(at.size()) + 1e-30); }
double errAtRaw(const std::vector<float>& d, const std::vector<float>& x, const std::vector<size_t>& at, int w) { double s = 0; for (size_t p : at) for (int k = -2; k < w + 2; ++k) { const double e = double(d[p + static_cast<size_t>(k)]) - x[p + static_cast<size_t>(k)]; s += e * e; } return 10 * std::log10(s / static_cast<double>(at.size()) + 1e-30); }
}

TEST_CASE("RS07 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rs07.sens", "rs07.size", "rs07.skew", "rs07.fade"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 1);
    CHECK(s[ClickSize].labels == std::vector<std::string>{"Small", "Medium", "Large"}); CHECK(s[ClickSize].def == 0);
    CHECK(s[FreqSkew].min == -50); CHECK(s[FreqSkew].max == 50); CHECK(s[FreqSkew].def == 0);
    CHECK(s[Fade].min == 0.5); CHECK(s[Fade].max == 10); CHECK(s[Fade].def == 2); CHECK(s[Fade].curve == Curve::Log);
}
TEST_CASE("RS07 reports 512 samples; silence is silence; clean speech-like signal passes untouched") {
    Processor q; CHECK(q.latencySamples() == 512);
    { auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f); }
    const auto x = speechLike(6.0); auto p = make(); const auto y = run(p, x);
    for (size_t i = 24000; i + kLatency < y.size(); i += 53) NEAR(y[i + kLatency], x[i], 1e-6);
    CHECK(p.clicksRepaired() == 0);
}
TEST_CASE("RS07 repairs smacks in the gaps and leaves the same clicks inside the words") {
    const auto x = speechLike(8.0); auto d = x; std::vector<size_t> gap, word;
    for (size_t s = 24000 + 6000, k = 0; s + 3000 < d.size(); s += 24000, ++k) { if (inGap(s)) { addClick(d, s, 14, 0.05f, 10 + static_cast<unsigned>(k)); gap.push_back(s); } }
    for (size_t s = 3000, k = 0; s + 3000 < d.size(); s += 48000, ++k) { if (!inGap(s)) { addClick(d, s, 14, 0.05f, 50 + static_cast<unsigned>(k)); word.push_back(s); } }
    REQUIRE(gap.size() >= 2); REQUIRE(word.size() >= 2);
    auto p = make({{ClickSize, 1}}); const auto y = run(p, d);
    CHECK(errAt(y, x, gap, 14) < errAtRaw(d, x, gap, 14) - 15.0);        // repaired
    CHECK(errAt(y, x, word, 14) > errAtRaw(d, x, word, 14) - 1.0);       // inside a word: untouched
    CHECK(p.clicksRepaired() >= static_cast<int>(gap.size()));
}
TEST_CASE("RS07 Click size: the longest click that is repaired") {
    const auto x = speechLike(8.0); auto d = x; std::vector<size_t> at;
    for (size_t s = 24000 + 6000, k = 0; s + 3000 < d.size(); s += 48000, ++k) { addClick(d, s, 24, 0.05f, 10 + static_cast<unsigned>(k)); at.push_back(s); }   // 1 ms
    auto err = [&](double size) { auto p = make({{ClickSize, size}}); const auto y = run(p, d); return errAt(y, x, at, 24); };
    CHECK(err(1) < err(0) - 8.0);   // Medium (0.6 ms) takes a 0.5 ms click, Small (0.3 ms) does not
}
TEST_CASE("RS07 Sensitivity: High catches weaker smacks than Low") {
    const auto x = speechLike(8.0); auto d = x;
    for (size_t s = 24000 + 3000, k = 0; s + 3000 < d.size(); s += 24000, ++k) if (inGap(s)) addClick(d, s, 1, 0.0035f, 5 + static_cast<unsigned>(k));
    for (size_t s = 24000 + 9000, k = 0; s + 3000 < d.size(); s += 24000, ++k) if (inGap(s)) addClick(d, s, 1, 0.0035f, 25 + static_cast<unsigned>(k));
    auto cnt = [&](double sens) { auto p = make({{Sensitivity, sens}}); run(p, d); return p.clicksRepaired(); };
    CHECK(cnt(2) >= cnt(0));
}
TEST_CASE("RS07 Freq skew tilts what the detector sees") {
    // a one-sample spike is high-frequency, a smooth 24-sample bump is low-frequency
    auto spikeIn = [&](double skew, bool bump) {
        const auto x = speechLike(8.0); auto d = x;
        for (size_t s = 24000 + 3000; s + 3000 < d.size(); s += 48000) { if (!bump) d[s] += 0.004f; else for (int k = 0; k < 24; ++k) d[s + static_cast<size_t>(k)] += 0.006f * static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * (k + 0.5) / 24)); }
        auto p = make({{Sensitivity, 1}, {ClickSize, 1}, {FreqSkew, skew}}); run(p, d); return p.clicksRepaired(); };
    CHECK(spikeIn(50, false) >= spikeIn(-50, false));
    CHECK(spikeIn(-50, true) >= spikeIn(50, true));
}
TEST_CASE("RS07 Fade widens the repaired span") {
    const auto x = speechLike(8.0); auto d = x; addClick(d, 24000 + 6000, 14, 0.05f, 3);
    auto changed = [&](double fade) { auto p = make({{ClickSize, 1}, {Fade, fade}}); const auto y = run(p, d); int n = 0; for (size_t i = 24000; i < 24000 + 12000; ++i) if (std::abs(double(y[i + kLatency]) - d[i]) > 1e-9) ++n; return n; };
    CHECK(changed(10) > changed(0.5));
}
TEST_CASE("RS07 loud input stays finite; stereo channels are separate") {
    auto p = make({{Sensitivity, 2}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make(); const auto a = speechLike(2.0), b = sine(-30, 2.0, 330); const auto r = run2(q, a, b);
    for (size_t i = 24000; i + kLatency < a.size(); i += 97) { NEAR(r.first[i + kLatency], a[i], 1e-5); NEAR(r.second[i + kLatency], b[i], 1e-5); }
}
