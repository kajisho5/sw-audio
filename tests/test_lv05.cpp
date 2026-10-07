#include "doctest.h"
#include "lv05/lv05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// program through the main input, key through the sidechain; blocks of at most 256; returns the output and the gain (dB) at the end of each block
struct Out { std::vector<float> y; std::vector<double> g; };
Out runKey(Processor& p, const std::vector<float>& prog, const std::vector<float>* key) {
    Out o; o.y = prog; std::vector<float> r = prog; std::vector<float> k0, k1; if (key) { k0 = *key; k1 = *key; }
    for (size_t off = 0; off < prog.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, prog.size() - off)); float* c[2] = {o.y.data() + off, r.data() + off};
        if (key) { const float* sc[2] = {k0.data() + off, k1.data() + off}; p.processWithSidechain(c, 2, n, sc, 2); } else p.process(c, 2, n);
        o.g.push_back(p.gainDb());
    }
    return o;
}
// voiced speech-like: 10 harmonics of f0 with a 4 Hz vibrato, level `lvl` dBFS rms, switched on in [on0, on1) seconds
std::vector<float> voice(double seconds, double lvl, double on0, double on1, double f0 = 140) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> y(n, 0.0f); double ph = 0; const double a = std::pow(10.0, lvl / 20) * std::sqrt(2.0) / 2.2;
    for (size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / kFs; const double f = f0 * (1 + 0.03 * std::sin(2 * kPi * 4 * t)); ph += 2 * kPi * f / kFs;
        if (t >= on0 && t < on1) { double s = 0; for (int h = 1; h <= 10; ++h) s += std::sin(h * ph) / h; y[i] = static_cast<float>(a * s); } }
    return y;
}
}

TEST_CASE("LV05 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Depth].min == -40); CHECK(s[Depth].max == 0); CHECK(s[Depth].def == -12);
    CHECK(s[Attack].min == 1); CHECK(s[Attack].max == 500); CHECK(s[Attack].def == 80);
    CHECK(s[Hold].min == 0); CHECK(s[Hold].max == 5); CHECK(s[Hold].def == 1.2);
    CHECK(s[Release].min == 0.1); CHECK(s[Release].max == 10); CHECK(s[Release].def == 2.0);
    CHECK(s[VoiceOnly].def == 1); CHECK(s[HoldToDuck].automatable == false);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV05 without a key signal nothing is ducked") {
    auto p = make(); const auto x = sine(-20, 2.0, 500); const auto o = runKey(p, x, nullptr); for (size_t i = 0; i < x.size(); ++i) REQUIRE(o.y[i] == x[i]);
}
TEST_CASE("LV05 a voice on the key ducks to Depth, then releases after Hold") {
    auto p = make({{Attack, 20}, {Hold, 0.5}, {Release, 0.3}, {Depth, -15}}); const auto prog = sine(-20, 6.0, 500), key = voice(6.0, -25, 0.5, 2.0);
    const auto o = runKey(p, prog, &key);
    auto at = [&](double t) { return o.g[static_cast<size_t>(t * kFs / 256)]; };
    CHECK(at(0.4) > -0.5);                    // before the voice
    CHECK(std::abs(at(1.5) - (-15.0)) < 0.8); // ducked
    CHECK(at(2.3) < -14.0);                   // the voice ended at 2.0: held until 2.5
    CHECK(at(5.5) > -0.5);                    // back
}
TEST_CASE("LV05 Voice only ignores claps and steady noise; Off follows any loud key") {
    // claps: 15 ms noise bursts every 0.7 s at -8 dBFS; room noise at -40 dBFS
    std::vector<float> claps(static_cast<size_t>(6 * kFs), 0.0f); const auto nz = noise(-8, 6.0, 4); for (size_t s = 24000; s + 800 < claps.size(); s += 33600) for (size_t k = 0; k < 720; ++k) claps[s + k] = nz[s + k];
    const auto hum = noise(-40, 6.0, 5), prog = sine(-20, 6.0, 500);
    { auto p = make({{Attack, 10}}); const auto o = runKey(p, prog, &claps); double mn = 0; for (double g : o.g) mn = std::min(mn, g); CHECK(mn > -1.0); }
    { auto p = make({{Attack, 10}}); const auto o = runKey(p, prog, &hum); double mn = 0; for (double g : o.g) mn = std::min(mn, g); CHECK(mn > -1.0); }
    { auto p = make({{Attack, 10}, {VoiceOnly, 0}}); const auto o = runKey(p, prog, &claps); double mn = 0; for (double g : o.g) mn = std::min(mn, g); CHECK(mn < -8.0); }
}
TEST_CASE("LV05 male, female and sung pitches count as a voice") {
    for (double f0 : {90.0, 140.0, 220.0, 330.0}) { auto r = make({{Attack, 20}}); const auto key = voice(3.0, -25, 0.2, 3.0, f0); const auto o3 = runKey(r, sine(-20, 3.0, 500), &key); CHECK(o3.g.back() < -10.0); }
}
TEST_CASE("LV05 Hold to duck forces the duck") {
    auto p = make({{Attack, 10}, {HoldToDuck, 1}}); const auto key = std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f); const auto o = runKey(p, sine(-20, 3.0, 500), &key);
    CHECK(std::abs(o.g.back() - (-12.0)) < 0.5);
}
TEST_CASE("LV05 mono, odd blocks, before prepare, silence") {
    auto p = make(); std::vector<float> l = sine(-20, 1.0, 500), k = voice(1.0, -25, 0.0, 1.0); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; const float* s[1] = {k.data() + off}; p.processWithSidechain(c, 1, n, s, 1); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
    auto q = make(); const auto o = runKey(q, std::vector<float>(48000, 0.0f), nullptr); for (float v : o.y) REQUIRE(v == 0.0f);
}
