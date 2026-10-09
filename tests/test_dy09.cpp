#include "doctest.h"
#include "dy09/dy09.hpp"
#include "os_helpers.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dy09;
using namespace tu;
namespace {
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// silence, then a decaying tone: hit at 0.2 s
std::vector<float> hit(double f, double amp, double tau = 0.1, double seconds = 1.0) {
    std::vector<float> x(static_cast<size_t>(seconds * kFs), 0.0f);
    for (size_t i = 9600; i < x.size(); ++i) { const double t = (i - 9600) / kFs; x[i] = static_cast<float>(amp * std::exp(-t / tau) * std::sin(2 * kPi * f * t)); }
    return x;
}
double head(const std::vector<float>& y) { return peakDb(y, 9600, 9600 + 480); }          // first 10 ms
double tail(const std::vector<float>& y) { return rmsDb(y, 9600 + 7200, 9600 + 12000); }   // 150..250 ms after the hit
}

TEST_CASE("DY09 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[Attack].id) == "dy09.attack"); CHECK(std::string(s[Sustain].id) == "dy09.sustain"); CHECK(std::string(s[Speed].id) == "dy09.speed");
    CHECK(std::string(s[Clip].id) == "dy09.clip"); CHECK(std::string(s[Mix].id) == "dy09.mix"); CHECK(std::string(s[Mode].id) == "dy09.mode");
    CHECK(std::string(s[B1Attack].id) == "dy09.b1.attack"); CHECK(std::string(s[B3Sustain].id) == "dy09.b3.sustain");
    for (int id : {Attack, Sustain, B1Attack, B1Sustain, B2Attack, B2Sustain, B3Attack, B3Sustain}) { CHECK(s[static_cast<size_t>(id)].min == -15); CHECK(s[static_cast<size_t>(id)].max == 15); CHECK(s[static_cast<size_t>(id)].def == 0); }
    CHECK(s[Speed].labels == std::vector<std::string>{"Fast", "Medium", "Slow"}); CHECK(s[Speed].def == 1);
    CHECK(s[Clip].labels == std::vector<std::string>{"Off", "Soft", "Hard"}); CHECK(s[Clip].def == 0);
    CHECK(s[Mix].def == 100);
    CHECK(s[Mode].labels == std::vector<std::string>{"Smooth", "Split bands"}); CHECK(s[Mode].def == 0);
    for (const auto& p : s) CHECK(p.automatable);
}
TEST_CASE("DY09 flat settings leave a steady tone and a hit alone") {
    auto p = make(); NEAR(rmsDb(run(p, sine(-20, 2, 500))), -20.0, 0.2);
    auto q = make(); const auto x = hit(200, 0.5); const auto y = run(q, x);
    NEAR(head(y), head(x), 0.5); NEAR(tail(y), tail(x), 0.5);
}
TEST_CASE("DY09 Attack lifts or lowers the head, not the tail; independent of loudness") {
    const auto x = hit(200, 0.5);
    auto up = make({{Attack, 12}}); const auto yu = run(up, x);
    CHECK(head(yu) > head(x) + 6.0); NEAR(tail(yu), tail(x), 1.5);
    auto dn = make({{Attack, -12}}); const auto yd = run(dn, x);
    CHECK(head(yd) < head(x) - 6.0); NEAR(tail(yd), tail(x), 1.5);
    const auto quiet = hit(200, 0.005);   // 40 dB lower
    auto uq = make({{Attack, 12}}); const auto yq = run(uq, quiet);
    NEAR(head(yq) - head(quiet), head(yu) - head(x), 1.5);
}
TEST_CASE("DY09 Sustain lifts or lowers the tail, not the head") {
    const auto x = hit(200, 0.5);
    auto up = make({{Sustain, 12}}); const auto yu = run(up, x);
    CHECK(tail(yu) > tail(x) + 3.0); NEAR(head(yu), head(x), 1.5);
    auto dn = make({{Sustain, -12}}); const auto yd = run(dn, x);
    CHECK(tail(yd) < tail(x) - 2.0);
}
TEST_CASE("DY09 Speed sets how long the head lasts") {
    auto gainAt = [](int speed, size_t from, size_t to) { auto p = make({{Attack, 12}, {Speed, static_cast<double>(speed)}}); const auto x = hit(200, 0.5, 0.3), y = run(p, x); return peakDb(y, 9600 + from, 9600 + to) - peakDb(x, 9600 + from, 9600 + to); };
    CHECK(gainAt(2, 480, 720) > gainAt(0, 480, 720) + 2.0);   // 10..15 ms after the hit: Slow is still up, Fast has settled
}
TEST_CASE("DY09 Clip: Off passes, Soft and Hard hold the output to 0 dBFS") {
    const auto loud = sine(3, 1, 1000);   // peak +6 dBFS
    auto off = make({{Clip, 0}}); CHECK(peakDb(run(off, loud), 24000, 48000) > 5.0);
    auto soft = make({{Clip, 1}}); const auto ys = run(soft, loud); CHECK(peakDb(ys, 24000, 48000) <= 0.1);
    auto hard = make({{Clip, 2}}); const auto yh = run(hard, loud); CHECK(peakDb(yh, 24000, 48000) <= 0.1);
    auto soft2 = make({{Clip, 1}}); NEAR(rmsDb(run(soft2, sine(-30, 1))), -30.0, 0.1);   // transparent below
    CHECK(rmsDb(yh) > rmsDb(ys));   // hard keeps more energy (squarer)
}
TEST_CASE("DY09 Split bands: the three bands sum flat and are shaped separately") {
    for (double f : {60.0, 150.0, 1000.0, 4000.0, 12000.0}) { auto p = make({{Mode, 1}}); NEAR(rmsDb(run(p, sine(-20, 1, f))), -20.0, 0.15); }
    // low band head up, high band untouched
    // compared with Split at 0 dB: the crossover's all-pass phase alone changes the peak of a burst a little
    auto lowUp = [](double f) { auto p = make({{Mode, 1}, {B1Attack, 12}}), z = make({{Mode, 1}}); const auto x = hit(f, 0.5, 0.05); return head(run(p, x)) - head(run(z, x)); };
    CHECK(lowUp(60) > 5.0); NEAR(lowUp(8000), 0.0, 1.0);
    auto sus = [](double f) { auto p = make({{Mode, 1}, {B3Sustain, 12}}), z = make({{Mode, 1}}); const auto x = hit(f, 0.5, 0.1); return tail(run(p, x)) - tail(run(z, x)); };
    CHECK(sus(8000) > 3.0); NEAR(sus(60), 0.0, 1.0);
    auto smooth = make({{Mode, 0}, {B1Attack, 12}}); const auto x = hit(60, 0.5); NEAR(head(run(smooth, x)), head(x), 0.5);   // band values only count in Split
}
TEST_CASE("DY09 silence stays silent, extreme input finite, latency 0") {
    auto p = make({{Attack, 15}, {Sustain, 15}, {Clip, 2}, {Mode, 1}, {B2Attack, 15}});
    for (float v : run(p, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f);
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

namespace {
void feed(Processor& p, const std::vector<float>& x, size_t from, size_t to) {
    std::vector<float> l(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(to)), r = l;
    for (size_t off = 0; off < l.size(); off += 256) { float* b[2] = {l.data() + off, r.data() + off}; p.process(b, 2, static_cast<int>(std::min<size_t>(256, l.size() - off))); }
}
}
TEST_CASE("DY09 reports the Attack and Sustain parts and the gain (screen read-outs)") {
    const auto x = hit(200, 0.5, 0.1, 1.0);
    { auto p = make(); feed(p, x, 0, 9600 + 480); CHECK(p.attackPartDb() == 0.0); CHECK(p.sustainPartDb() == 0.0); CHECK(std::abs(p.gainDb()) < 0.01); }
    {   // Attack +12: the hit raises the Attack part (held after the hit), the Sustain part stays 0
        auto p = make({{Attack, 12}}); feed(p, x, 0, 9600 + 1200);
        CHECK(p.attackPartDb() > 3.0); CHECK(p.attackPartDb() <= 12.0); CHECK(p.sustainPartDb() == 0.0);
        feed(p, x, 9600 + 1200, 9600 + 1200 + 48000 / 2);   // half a second later the held peak is gone
        CHECK(std::abs(p.attackPartDb()) < 0.5);
    }
    {   // Sustain -12: the decay lowers the tail; the Attack part stays 0
        auto p = make({{Sustain, -12}}); feed(p, x, 0, 9600 + 14400);
        CHECK(p.sustainPartDb() < -0.5); CHECK(p.sustainPartDb() >= -12.0); CHECK(p.attackPartDb() == 0.0); CHECK(p.gainDb() <= 0.0);
    }
    {   // Split bands: the screen sees the largest band; a hit in the high band moves it
        auto p = make({{Mode, 1}, {B3Attack, 12}}); const auto h = hit(8000, 0.5, 0.05, 1.0); feed(p, h, 0, 9600 + 1200);
        CHECK(p.attackPartDb() > 3.0);
    }
}

// the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x)
TEST_CASE("DY09: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x, and Clip follows it") {
    const auto& s = specs();
    CHECK(std::string(s[Oversample].id) == "dy09.os"); CHECK(s[Oversample].steps == std::vector<double>{1, 2, 4}); CHECK(s[Oversample].def == 2.0); CHECK(Oversample == kNumParams - 1);
    for (int clip : {1, 2}) {
        const double amp = clip == 1 ? 1.0 : 1.2;
        auto alias = [&](int os, double f0, double bin) { auto p = make({{Clip, double(clip)}, {Oversample, double(os)}}); return ost::relDb(p, f0, bin, amp); };
        // 15 kHz: the 3rd harmonic (45 kHz) folds to 3 kHz at 1x;  13 kHz -> 5 kHz: a high harmonic that only 4x keeps out
        const double a1 = alias(1, 15000, 3000), a2 = alias(2, 15000, 3000), b2 = alias(2, 13000, 5000), b4 = alias(4, 13000, 5000);
        INFO((clip == 1 ? "Soft" : "Hard") << " clip, 15 kHz alias at 3 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB;  13 kHz alias at 5 kHz: 2x " << b2 << " dB, 4x " << b4 << " dB");
        CHECK(a1 > -40.0); CHECK(a2 < a1 - 4.0);
        if (clip == 1) CHECK(b4 < b2 - 15.0);   // a hard clip's corners keep making high harmonics that fold at any rate: there 4x is only no worse
        else CHECK(ost::notWorse(b4, b2));
    }
}
