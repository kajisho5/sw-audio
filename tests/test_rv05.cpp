#include "doctest.h"
#include "rv05/rv05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
struct Ir { std::vector<float> l, r; };
Ir impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; auto o = run2(p, x, x); return {o.first, o.second}; }
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double rt60(const std::vector<float>& h) { return 3.0 * (crossing(h, 30.0) - crossing(h, 10.0)); }
double energy(const std::vector<float>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += double(h[i]) * h[i]; return e; }
size_t peakAt(const std::vector<float>& h, double a, double b) { size_t best = static_cast<size_t>(a * kFs); for (size_t i = best; i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) if (std::abs(h[i]) > std::abs(h[best])) best = i; return best; }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
std::vector<float> burst(double rmsDbfs, double sec, double total, unsigned seed = 1) { auto x = noise(rmsDbfs, sec, seed); x.resize(static_cast<size_t>(total * kFs), 0.0f); return x; }
}

TEST_CASE("RV05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv05.room", "rv05.decay", "rv05.micdistance", "rv05.speakertilt", "rv05.tone", "rv05.mix", "rv05.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Room].labels == std::vector<std::string>{"Small", "Medium", "Large"}); CHECK(s[Room].def == 1);
    CHECK(s[Decay].min == 0); CHECK(s[Decay].max == 10); CHECK(s[Decay].def == 5);
    CHECK(s[MicDistance].def == 50); CHECK(std::string(s[MicDistance].minLabel) == "Near"); CHECK(std::string(s[MicDistance].maxLabel) == "Far");
    CHECK(s[Tilt].def == 5); CHECK(s[Tone].def == 50); CHECK(std::string(s[Tone].minLabel) == "Dark"); CHECK(std::string(s[Tone].maxLabel) == "Bright"); CHECK(s[Mix].def == 30);
}
TEST_CASE("RV05 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV05 the room: sizes, microphone distance, decay mapping") {
    const auto s = roomDims(0), m = roomDims(1), l = roomDims(2);
    CHECK(s.lx * s.ly * s.lz < m.lx * m.ly * m.lz); CHECK(m.lx * m.ly * m.lz < l.lx * l.ly * l.lz);
    NEAR(micDistanceMeters(1, 0), 0.5, 1e-9); CHECK(micDistanceMeters(1, 100) > 4.0); CHECK(micDistanceMeters(1, 100) < m.lx);
    NEAR(decaySeconds(0), 0.4, 1e-9); NEAR(decaySeconds(10), 4.0, 1e-9); NEAR(decaySeconds(5), 0.4 * std::sqrt(10.0), 1e-6);
}
TEST_CASE("RV05 image sources: the direct sound first, then 24 reflections, later for a larger room") {
    for (int room = 0; room < 3; ++room) {
        const auto t = earlyReflections(room, 50); REQUIRE(t.size() == 25);
        double first = 1e9; int direct = 0; for (size_t i = 0; i < t.size(); ++i) { if (t[i].delaySeconds < first) { first = t[i].delaySeconds; direct = static_cast<int>(i); } }
        CHECK(direct == 0);   // the direct sound is the first entry and the earliest
        for (const auto& x : t) { CHECK(x.gain > 0.0); CHECK(std::abs(x.pan) <= 1.0); CHECK(x.delaySeconds >= t[0].delaySeconds); }
        CHECK(t[0].gain > t[1].gain);   // the direct sound is the strongest
    }
    // the first reflection arrives later in a larger room (Near microphone)
    auto firstRefl = [](int room) { auto t = earlyReflections(room, 0); double best = 1e9; for (size_t i = 1; i < t.size(); ++i) best = std::min(best, t[i].delaySeconds); return best; };
    CHECK(firstRefl(2) > firstRefl(0) * 1.5);
}
TEST_CASE("RV05 Decay sets the reverberation time") {
    for (auto [k, rt] : std::vector<std::pair<double, double>>{{0, 0.4}, {5, 1.26}, {10, 4.0}}) {
        auto p = make({{Decay, k}, {Tone, 100}}); const auto h = impulse(p, std::max(2.0, 2.5 * rt)); NEAR(rt60(h.l) / rt, 1.0, 0.3);
    }
}
TEST_CASE("RV05 Mic distance: near is mostly direct sound and early reflections, far is mostly reverb; the direct sound arrives later") {
    auto n = make({{MicDistance, 0}}); auto f = make({{MicDistance, 100}});
    const auto hn = impulse(n, 2.0), hf = impulse(f, 2.0);
    auto ratio = [](const Ir& h) { return 10 * std::log10(energy(h.l, 0.0, 0.012) / energy(h.l, 0.1, 2.0)); };
    CHECK(ratio(hn) > ratio(hf) + 10.0);
    CHECK(peakAt(hf.l, 0.0, 0.1) > peakAt(hn.l, 0.0, 0.1) * 3);
}
TEST_CASE("RV05 Mic distance: farther away, fewer highs (air)") {
    auto n = make({{MicDistance, 0}}); auto f = make({{MicDistance, 100}});
    const auto x = burst(-20, 1.0, 1.5), a = run(n, x), b = run(f, x);
    const size_t t0 = static_cast<size_t>(0.1 * kFs), t1 = static_cast<size_t>(0.9 * kFs);
    CHECK(band(a, 8000, 12000, t0, t1) - band(a, 800, 1200, t0, t1) > band(b, 8000, 12000, t0, t1) - band(b, 800, 1200, t0, t1) + 3.0);
}
TEST_CASE("RV05 Speaker tilt: 10 is brighter than 0") {
    auto d = make({{Tilt, 0}}); auto b = make({{Tilt, 10}});
    const auto x = burst(-20, 1.0, 1.5), yd = run(d, x), yb = run(b, x);
    const size_t t0 = static_cast<size_t>(0.1 * kFs), t1 = static_cast<size_t>(0.9 * kFs);
    CHECK((band(yb, 4000, 8000, t0, t1) - band(yb, 200, 400, t0, t1)) - (band(yd, 4000, 8000, t0, t1) - band(yd, 200, 400, t0, t1)) > 6.0);
}
TEST_CASE("RV05 Tone: Dark loses the highs of the tail") {
    auto dark = make({{Tone, 0}}); auto bright = make({{Tone, 100}});
    const auto x = burst(-20, 0.5, 3.0), yd = run(dark, x), yb = run(bright, x);
    const size_t a = static_cast<size_t>(1.0 * kFs), b = static_cast<size_t>(2.0 * kFs);
    CHECK(band(yb, 6000, 10000, a, b) - band(yd, 6000, 10000, a, b) > band(yb, 300, 600, a, b) - band(yd, 300, 600, a, b) + 6.0);
}
TEST_CASE("RV05 the level is sensible whatever the room; the extremes stay finite") {
    for (int room = 0; room < 3; ++room) { auto p = make({{Room, double(room)}, {MicDistance, 50}}); const auto h = impulse(p, 6.0); double e = 0; for (float v : h.l) e += double(v) * v; CHECK(e > 0.3); CHECK(e < 20.0); }
    for (double d : {0.0, 10.0}) for (double mic : {0.0, 100.0}) for (int room = 0; room < 3; ++room) { auto q = make({{Room, double(room)}, {Decay, d}, {MicDistance, mic}, {Tilt, 10}}); for (float v : run(q, burst(-3, 0.5, 1.5))) CHECK(std::isfinite(v)); }
}
