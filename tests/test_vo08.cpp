#include "doctest.h"
#include "vo08/vo08.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::vo08;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// a phrase (a 220 Hz tone with 3 harmonics, -16 dBFS rms), a breath of `breathDb` noise (0.5 s), a second phrase
std::vector<float> scene(double breathDb, double phraseSec = 2.0) {
    auto ph = [&](double sec) { auto a = sine(-20, sec, 220), b = sine(-28, sec, 440), c = sine(-34, sec, 660); for (size_t i = 0; i < a.size(); ++i) a[i] += b[i] + c[i]; return a; };
    std::vector<float> x = ph(phraseSec); const auto br = noise(breathDb, 0.5, 5); x.insert(x.end(), br.begin(), br.end()); const auto p2 = ph(phraseSec); x.insert(x.end(), p2.begin(), p2.end()); return x;
}
double breathLevel(const std::vector<float>& y, double phraseSec = 2.0, int lat = kLatency) {   // the middle of the breath, as it comes out
    const size_t a = static_cast<size_t>(phraseSec * kFs) + static_cast<size_t>(lat) + 6000, b = static_cast<size_t>((phraseSec + 0.5) * kFs) + static_cast<size_t>(lat) - 2400; return rmsDb(y, a, b);
}
}

TEST_CASE("VO08 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo08.mode", "vo08.reduction", "vo08.sensitivity", "vo08.keep", "vo08.fade"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Mode].labels == std::vector<std::string>{"Reduce", "Remove", "Mark only"}); CHECK(s[Mode].def == 0);
    CHECK(s[Reduction].min == -40); CHECK(s[Reduction].max == 0); CHECK(s[Reduction].def == -12);
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 1);
    CHECK(s[Keep].labels == std::vector<std::string>{"Natural", "Less", "None"}); CHECK(s[Keep].def == 0);
    CHECK(s[Fade].min == 1); CHECK(s[Fade].max == 50); CHECK(s[Fade].def == 10); CHECK(s[Fade].curve == Curve::Log);
}
TEST_CASE("VO08 reports its 1024-sample look-ahead; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 1024);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("VO08 the signal comes out delayed by the look-ahead, phrases untouched") {
    const auto x = scene(-45); auto p = make(); const auto y = run(p, x);
    for (size_t i = 6000; i < 90000; i += 997) NEAR(y[i + 1024], x[i], 1e-5);   // the first phrase, sample for sample
}
TEST_CASE("VO08 Reduce lowers the breath by Reduction x Keep") {
    const auto x = scene(-45);
    auto at = [&](Set set) { auto p = make(set); return breathLevel(run(p, x)) - rmsDb(x, 96000 + 6000, 120000 - 2400); };
    NEAR(at({{Reduction, -12}, {Keep, Less}}), -12.0, 1.5);
    NEAR(at({{Reduction, -12}, {Keep, Natural}}), -7.2, 1.5);
    NEAR(at({{Reduction, -20}, {Keep, None}}), -30.0 > -40.0 ? -30.0 : -40.0, 2.0);
    NEAR(at({{Reduction, 0}}), 0.0, 0.3);
    // the phrase after the breath is back at its level
    auto p = make({{Reduction, -30}, {Keep, Less}}); const auto y = run(p, x); const size_t a = static_cast<size_t>(2.5 * kFs) + 1024 + 24000;
    NEAR(rmsDb(y, a, a + 48000), rmsDb(x, a - 1024, a - 1024 + 48000), 0.3);
}
TEST_CASE("VO08 Remove goes down by 60 dB; Mark only leaves the audio and marks the breath") {
    const auto x = scene(-45);
    { auto p = make({{Mode, Remove}}); const auto y = run(p, x); CHECK(breathLevel(y) < rmsDb(x, 102000, 117000) - 50.0); }
    auto p = make({{Mode, MarkOnly}}); std::vector<float> l = x, r = x; bool seen = false; int before = -1;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); if (p.breathActive()) seen = true; if (off >= 90000 && before < 0) before = p.breathCount(); }
    CHECK(seen); CHECK(before == 0); CHECK(p.breathCount() == 1);
    for (size_t i = 6000; i < l.size() - 1024; i += 997) NEAR(l[i + 1024], x[i], 1e-5);   // nothing changed
}
TEST_CASE("VO08 Sensitivity: a breath only 12 dB under the phrase is caught by High only; a loud one by all") {
    auto lowered = [&](double breathDb, int sens) { const auto x = scene(breathDb); auto p = make({{Reduction, -20}, {Keep, Less}, {Sensitivity, double(sens)}}); const auto y = run(p, x); return breathLevel(y) - rmsDb(x, 96000 + 6000, 120000 - 2400); };
    CHECK(lowered(-45, 0) < -10.0); CHECK(lowered(-45, 1) < -10.0); CHECK(lowered(-45, 2) < -10.0);
    CHECK(lowered(-31, 0) > -2.0); CHECK(lowered(-31, 2) < -10.0);
}
TEST_CASE("VO08 a voiced soft note or a low-noise-free tone is not a breath") {
    auto p = make({{Reduction, -30}, {Keep, Less}, {Sensitivity, 2}});
    std::vector<float> x = sine(-16, 2.0, 220); const auto soft = sine(-48, 0.5, 220); x.insert(x.end(), soft.begin(), soft.end()); const auto ph = sine(-16, 1.0, 220); x.insert(x.end(), ph.begin(), ph.end());
    const auto y = run(p, x); NEAR(rmsDb(y, 96000 + 1024 + 6000, 120000 + 1024 - 2400), rmsDb(x, 96000 + 6000, 120000 - 2400), 0.3);
}
TEST_CASE("VO08 Fade: the time the gain takes") {
    // the gain is linear in dB at 24 dB / Fade: with 50 ms it takes 50 ms for the whole 24 dB, with 1 ms it is immediate
    auto ramp = [&](double fadeMs) {
        auto p = make({{Reduction, -24}, {Keep, Less}, {Fade, fadeMs}}); const auto x = scene(-45); std::vector<float> l = x, r = x; int first = -1, last = -1;
        for (size_t off = 0; off < l.size(); off += 64) { const int n = static_cast<int>(std::min<size_t>(64, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n);
            const double g = p.gainDb(); const int t = static_cast<int>(off); if (g < -3.0 && first < 0) first = t; if (g < -21.0 && last < 0) last = t; }
        return (last - first) / kFs * 1000.0;
    };
    NEAR(ramp(50), 50.0 * 18.0 / 24.0, 6.0); CHECK(ramp(1) < 3.0); CHECK(ramp(10) > ramp(1)); CHECK(ramp(50) > ramp(10));
}
TEST_CASE("VO08 loud input stays finite; a stereo input keeps its channels") {
    auto p = make({{Reduction, -40}, {Keep, None}, {Sensitivity, 2}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : run(p, x)) CHECK(std::isfinite(v));
    auto q = make(); std::vector<float> l = sine(-12, 0.5, 300), r = sine(-18, 0.5, 700); const auto l0 = l, r0 = r; float* c[2] = {l.data(), r.data()}; q.process(c, 2, static_cast<int>(l.size()));
    for (size_t i = 3000; i < l.size() - 1024; i += 211) { NEAR(l[i + 1024], l0[i], 1e-5); NEAR(r[i + 1024], r0[i], 1e-5); }
}
