#include "doctest.h"
#include "md07/md07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::md07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
}

TEST_CASE("MD07 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"md07.voices", "md07.spread", "md07.rate", "md07.depth", "md07.tone", "md07.mix", "md07.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Voices].labels == std::vector<std::string>{"2", "3", "4", "6"}); CHECK(s[Voices].def == 4);
    CHECK(s[Spread].def == 6); CHECK(s[Rate].def == 4); CHECK(s[Depth].def == 5); CHECK(s[Tone].def == 50); CHECK(s[Mix].def == 50);
    for (int i : {Spread, Rate, Depth}) { CHECK(s[static_cast<size_t>(i)].min == 0); CHECK(s[static_cast<size_t>(i)].max == 10); }
}
TEST_CASE("MD07 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("MD07 the voices' sweeps add up to zero: the construction is mono compatible") {
    for (int voices : {2, 3, 4, 6}) {
        auto p = make({{Voices, double(voices)}, {Depth, 10}, {Rate, 7}});
        std::vector<float> l(96000, 0.0f), r = l; double worst = 0, dev = 0;
        for (size_t off = 0; off < l.size(); off += 16) {
            float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 16);
            double sum = 0; for (int v = 0; v < voices; ++v) { const double d = p.voiceDelaySamples(v) - 0.010 * kFs; sum += d; dev = std::max(dev, std::abs(d)); }
            worst = std::max(worst, std::abs(sum));
        }
        CHECK(worst < 0.01 * 4.0 * 0.001 * kFs);   // below 1 % of the sweep (4 ms)
        CHECK(dev > 4.0 * 0.001 * kFs * 0.9); CHECK(dev < 4.0 * 0.001 * kFs * 1.26);   // the sweep itself is there: +-4 ms slow, up to a quarter more fast
    }
}
TEST_CASE("MD07 Rate: a slow sweep and one 12 times faster, a quarter of the depth") {
    NEAR(slowHz(0), 0.15, 1e-12); NEAR(slowHz(10), 3.0, 1e-9);
    auto p = make({{Voices, 2}, {Depth, 10}, {Rate, 5}});
    std::vector<float> d; std::vector<float> l(48000 * 20, 0.0f), r = l;
    for (size_t off = 0; off < l.size(); off += 16) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 16); d.push_back(static_cast<float>(p.voiceDelaySamples(0) - 0.010 * kFs)); }
    const double fs = kFs / 16.0, sl = slowHz(5);
    const double a1 = binDb(d, sl, 0, d.size(), fs), a2 = binDb(d, 12.0 * sl, 0, d.size(), fs);
    NEAR(a1, 20 * std::log10(0.004 * kFs), 1.0);   // +-4 ms in samples
    NEAR(a2 - a1, 20 * std::log10(0.25), 1.5);
}
TEST_CASE("MD07 Spread places the voices; the output keeps unit power") {
    for (int v = 0; v < 4; ++v) { NEAR(voicePan(v, 4, 0), 0.5, 1e-12); }
    NEAR(voicePan(0, 4, 10), 0.0, 1e-12); NEAR(voicePan(3, 4, 10), 1.0, 1e-12); NEAR(voicePan(1, 4, 10), 1.0 / 3.0, 1e-12);
    NEAR(voicePan(0, 2, 5), 0.25, 1e-12); NEAR(voicePan(1, 2, 5), 0.75, 1e-12);
    // two voices, full spread: the left channel is voice 0 alone, the right voice 1 alone (different sweeps)
    auto p = make({{Voices, 2}, {Spread, 10}, {Depth, 0}}); auto l = noise(-18, 1.0, 3), r = l; go(p, l, r);
    NEAR(rmsDb(l, 24000, 48000), rmsDb(r, 24000, 48000), 0.5);
    for (int voices : {2, 3, 4, 6}) for (double spread : {0.0, 5.0, 10.0}) {
        auto q = make({{Voices, double(voices)}, {Spread, spread}, {Depth, 5}, {Tone, 100}}); auto a = noise(-18, 4.0, 5), b = a; go(q, a, b);
        const double pw = 10 * std::log10(0.5 * (std::pow(10.0, rmsDb(a, 48000, 192000) / 10) + std::pow(10.0, rmsDb(b, 48000, 192000) / 10)));
        NEAR(pw, -18.0 - 2.7, 1.2);   // the 14 kHz low-pass takes the top of white noise (measured -2.7 dB)
    }
}
TEST_CASE("MD07 at full Spread the wobble of one channel cancels in the mono sum") {
    auto sidebands = [&](bool mono) {
        auto p = make({{Voices, 4}, {Spread, 10}, {Depth, 0.2}, {Rate, 10}, {Tone, 100}});   // a small sweep (+-0.08 ms slow): first-order theory holds
        auto l = sine(-12, 6.0, 1000), r = l; go(p, l, r);
        std::vector<float> m(l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = mono ? 0.5f * (l[i] + r[i]) : l[i];
        return std::max(binDb(m, 1003, 72000, 288000), binDb(m, 997, 72000, 288000)) - binDb(m, 1000, 72000, 288000);
    };
    const double left = sidebands(false), mono = sidebands(true);
    CHECK(left > -30.0);               // one channel does wobble (a sideband 3 Hz away)
    CHECK(mono < left - 20.0);          // and the first-order wobble cancels in L + R
}
TEST_CASE("MD07 Tone sets the band limit") {
    auto hi = [&](double tone) { auto p = make({{Tone, tone}, {Depth, 0}}); auto l = sine(-12, 1.0, 9000), r = l; go(p, l, r); return rmsDb(l, 24000, 48000); };
    CHECK(hi(100) > hi(0) + 12.0);
}
TEST_CASE("MD07 loud noise at the extremes stays finite") {
    for (int voices : {2, 6}) for (double spread : {0.0, 10.0}) for (double depth : {0.0, 10.0}) {
        auto p = make({{Voices, double(voices)}, {Spread, spread}, {Depth, depth}, {Rate, 10}});
        auto l = noise(0, 2.0, 4), r = l; go(p, l, r); for (float v : l) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 10.0f); }
    }
}
