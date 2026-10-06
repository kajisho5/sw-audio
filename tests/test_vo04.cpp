#include "doctest.h"
#include "vo04/vo04.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::vo04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::pair<std::vector<float>, std::vector<float>> go(Processor& p, std::vector<float> l) { return run2(p, l, l); }
}

TEST_CASE("VO04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo04.voices", "vo04.spread", "vo04.timing", "vo04.pitchvar", "vo04.tone", "vo04.mix"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Voices].labels == std::vector<std::string>{"1", "2", "4", "8"}); CHECK(s[Voices].def == 2);
    CHECK(s[Spread].def == 6); CHECK(s[Timing].def == 4); CHECK(s[PitchVar].def == 3); CHECK(s[Tone].def == 50); CHECK(s[Mix].def == 50);
    for (int i : {Spread, Timing, PitchVar}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); }
    CHECK(std::string(s[Tone].minLabel) == "Dark"); CHECK(std::string(s[Tone].maxLabel) == "Bright");
}
TEST_CASE("VO04 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("VO04 Timing sets the range of the wandering delays (0.5 ms .. 3 ms x Timing)") {
    for (double timing : {0.0, 4.0, 10.0}) {
        auto p = make({{Voices, 8}, {Timing, timing}, {PitchVar, 0}});
        std::vector<float> x(48000 * 40, 0.0f); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * static_cast<float>(std::sin(2 * kPi * 220.0 * i / kFs));
        double lo = 1e9, hi = 0, sum = 0; long cnt = 0;
        for (size_t off = 0; off < x.size(); off += 256) {
            std::vector<float> a(x.begin() + static_cast<long>(off), x.begin() + static_cast<long>(std::min<size_t>(x.size(), off + 256))), b = a;
            float* c[2] = {a.data(), b.data()}; p.process(c, 2, static_cast<int>(a.size()));
            if (off < 48000 * 10) continue;   // the first seconds grow in from the phrase start
            for (int v = 0; v < 8; ++v) { const double ms = p.voiceDelaySamples(v) * 1000.0 / kFs; lo = std::min(lo, ms); hi = std::max(hi, ms); sum += ms; ++cnt; }
        }
        CHECK(lo > 0.4); CHECK(hi < 0.5 + 3.0 * timing + 0.05);
        if (timing == 0.0) CHECK(hi < 0.6);
        if (timing == 10.0) { CHECK(hi > 20.0); CHECK(lo < 12.0); }   // the voices really differ and move
        if (timing == 4.0) CHECK(sum / static_cast<double>(cnt) > 3.0);
    }
}
TEST_CASE("VO04 the delays wander slowly: no periodic sweep, pitch change below 4 cents when Pitch var is 0") {
    auto p = make({{Voices, 8}, {Timing, 4}, {PitchVar, 0}});
    std::vector<float> x(48000 * 60, 0.0f); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * static_cast<float>(std::sin(2 * kPi * 220.0 * i / kFs));
    double prev[8] = {}, worst = 0; bool first = true;
    for (size_t off = 0; off < x.size(); off += 256) {
        std::vector<float> a(x.begin() + static_cast<long>(off), x.begin() + static_cast<long>(off + 256)), b = a;
        float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256);
        for (int v = 0; v < 8; ++v) { const double d = p.voiceDelaySamples(v); if (!first && off > 48000 * 20) worst = std::max(worst, std::abs(d - prev[v]) / 256.0); prev[v] = d; }
        first = false;
    }
    CHECK(worst * 1731.0 < 4.0);
}
TEST_CASE("VO04 Pitch var: the delay slope is 0.8 x Pitch var cents rms") {
    for (double pv : {2.0, 5.0, 10.0}) {
        auto p = make({{Voices, 4}, {Timing, 0}, {PitchVar, pv}});
        std::vector<float> x(48000 * 60, 0.0f); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * static_cast<float>(std::sin(2 * kPi * 220.0 * i / kFs));
        double s2 = 0; long cnt = 0; double prev[4] = {};
        for (size_t off = 0; off < x.size(); off += 16) {
            std::vector<float> a(x.begin() + static_cast<long>(off), x.begin() + static_cast<long>(off + 16)), b = a;
            float* c[2] = {a.data(), b.data()}; p.process(c, 2, 16);
            for (int v = 0; v < 4; ++v) { const double d = p.voiceDelaySamples(v); if (off > 0) { const double sl = (d - prev[v]) / 16.0; s2 += sl * sl; ++cnt; } prev[v] = d; }
        }
        const double cents = std::sqrt(s2 / static_cast<double>(cnt)) * 1731.234;
        NEAR(cents, 0.8 * pv, 0.8 * pv * 0.3);
    }
}
TEST_CASE("VO04 the start of a phrase: small delay after a pause, then a slow growth") {
    auto p = make({{Voices, 4}, {Timing, 10}, {PitchVar, 0}});
    auto burst = [&](double secs, bool on) { std::vector<float> x(static_cast<size_t>(secs * kFs), 0.0f); if (on) for (size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * static_cast<float>(std::sin(2 * kPi * 220.0 * i / kFs)); return x; };
    go(p, burst(20, true));
    double before = 0; for (int v = 0; v < 4; ++v) before = std::max(before, p.voiceDelaySamples(v) * 1000 / kFs);
    CHECK(before > 8.0);
    go(p, burst(1.0, false));
    for (int v = 0; v < 4; ++v) CHECK(p.voiceDelaySamples(v) * 1000 / kFs < 1.0);   // back to the smallest at once
    // phrase starts: the delay at the onset is small, grows at no more than 0.5 % slope
    double worst = 0, atOnset = 0, prev[4] = {}; bool init = false;
    for (int blk = 0; blk < 48000 / 16; ++blk) {
        std::vector<float> a(16), b;
        for (int i = 0; i < 16; ++i) a[static_cast<size_t>(i)] = 0.1f * static_cast<float>(std::sin(2 * kPi * 220.0 * (blk * 16 + i) / kFs));
        b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 16);
        for (int v = 0; v < 4; ++v) { const double d = p.voiceDelaySamples(v); if (init) worst = std::max(worst, std::abs(d - prev[v]) / 16.0); prev[v] = d; }
        if (!init) { for (int v = 0; v < 4; ++v) atOnset = std::max(atOnset, prev[v] * 1000 / kFs); init = true; }
    }
    CHECK(atOnset < 1.5); CHECK(worst <= 0.0051);
}
TEST_CASE("VO04 Spread and level: unit power for 1, 2, 4 and 8 voices") {
    for (int v = 0; v < 4; ++v) NEAR(voicePan(v, 4, 0), 0.5, 1e-12);
    NEAR(voicePan(0, 2, 10), 0.0, 1e-12); NEAR(voicePan(1, 2, 10), 1.0, 1e-12); NEAR(voicePan(0, 1, 10), 0.5, 1e-12);
    { auto p = make({{Voices, 2}, {Spread, 0}}); auto l = noise(-18, 1.0, 3); auto y = go(p, l); for (size_t i = 24000; i < 48000; ++i) CHECK(y.first[i] == y.second[i]); }
    { auto p = make({{Voices, 2}, {Spread, 10}, {Timing, 8}}); auto y = go(p, noise(-18, 1.0, 3)); double d = 0; for (size_t i = 24000; i < 48000; ++i) d = std::max(d, std::abs(double(y.first[i]) - y.second[i])); CHECK(d > 0.01); }
    for (int voices : {1, 2, 4, 8}) for (double spread : {0.0, 10.0}) {
        auto q = make({{Voices, double(voices)}, {Spread, spread}, {Timing, 10}, {Tone, 50}}); auto y = go(q, noise(-18, 4.0, 5));
        const double pw = 10 * std::log10(0.5 * (std::pow(10.0, rmsDb(y.first, 48000, 192000) / 10) + std::pow(10.0, rmsDb(y.second, 48000, 192000) / 10)));
        NEAR(pw, -18.0, 1.5);
    }
}
TEST_CASE("VO04 Tone: 50 % is flat, Dark / Bright tilt the doubles") {
    auto level = [&](double tone, double f) { auto p = make({{Voices, 1}, {Spread, 0}, {Timing, 0}, {PitchVar, 0}, {Tone, tone}}); auto y = go(p, sine(-18, 1.0, f)); return rmsDb(y.first, 24000, 48000); };
    for (double f : {100.0, 1000.0, 8000.0}) NEAR(level(50, f), -18.0, 0.3);
    NEAR(level(100, 8000) - level(0, 8000), 16.0, 2.5);
    NEAR(level(100, 100) - level(0, 100), -8.0, 2.0);
    NEAR(level(100, 1000) - level(0, 1000), 0.0, 3.5);
}
TEST_CASE("VO04 loud input stays finite; one voice at Timing 0 is a plain copy") {
    auto p = make({{Voices, 8}, {Timing, 10}, {PitchVar, 10}}); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; auto y = go(p, x);
    for (float v : y.first) CHECK(std::isfinite(v));
    auto q = make({{Voices, 1}, {Spread, 0}, {Timing, 0}, {PitchVar, 0}}); auto s = sine(-12, 1.0, 1000); auto z = go(q, s);
    NEAR(rmsDb(z.first, 24000, 48000), -12.0, 0.3);
}
