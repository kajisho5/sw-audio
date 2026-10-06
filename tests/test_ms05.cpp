#include "doctest.h"
#include "ms05/ms05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::ms05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("MS05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"ms05.target", "ms05.range", "ms05.speed", "ms05.gate", "ms05.source", "ms05.ride", "ms05.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Target].min == -40); CHECK(s[Target].max == -6); CHECK(s[Target].def == -18);
    CHECK(s[Range].min == 0); CHECK(s[Range].max == 24); CHECK(s[Range].def == 6);
    CHECK(s[Speed].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Speed].def == 1);
    CHECK(s[Gate].min == -80); CHECK(s[Gate].max == -20); CHECK(s[Gate].def == -50);
    CHECK(s[Source].labels == std::vector<std::string>{"Vocal", "Mix", "Bass"}); CHECK(s[Source].def == 0);
    CHECK(s[Ride].min == -24); CHECK(s[Ride].max == 24); CHECK(s[Ride].def == 0); CHECK(s[Ride].automatable);
    CHECK(s[Write].def == 0); CHECK_FALSE(s[Write].automatable);
}
TEST_CASE("MS05 Write automation On: the ride pulls the short-term loudness to Target, within Range") {
    // identical L/R sine: BS.1770 counts both channels, so RMS -26 dBFS is about -23.7 LUFS
    auto p = make({{Write, 1}, {Range, 12}});
    const auto y = run(p, sine(-26, 12));
    const double up = rmsDb(y, y.size() - 24000, y.size()) - -26.0;
    NEAR(up, 5.7, 1.0); NEAR(p.rideDb(), up, 0.3);
    auto q = make({{Write, 1}, {Range, 6}});
    run(q, sine(-10, 12)); NEAR(q.rideDb(), -6.0, 0.2);                 // wants -10.3, Range stops it at -6
    auto r = make({{Write, 1}, {Range, 0}}); run(r, sine(-26, 5)); NEAR(r.rideDb(), 0.0, 1e-9);
}
TEST_CASE("MS05 Speed: Fast < Medium < Slow") {
    auto t63 = [](int speed) {
        auto p = make({{Write, 1}, {Range, 12}, {Speed, static_cast<double>(speed)}}); const auto in = sine(-26, 20);
        const double goal = 0.632 * 5.7; int n = 0;
        for (size_t off = 0; off + 256 <= in.size(); off += 256) { std::vector<float> a(in.begin() + off, in.begin() + off + 256), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256); n += 256; if (p.rideDb() >= goal) return n / kFs; }
        return 99.0;
    };
    const double fast = t63(2), med = t63(1), slow = t63(0);
    CHECK(fast < med); CHECK(med < slow); CHECK(slow > fast * 2.5);
}
TEST_CASE("MS05 Gate: nothing moves below it") {
    auto ride = [](double gate) { auto p = make({{Write, 1}, {Gate, gate}, {Range, 24}}); run(p, sine(-64, 8)); return p.rideDb(); };
    NEAR(ride(-50), 0.0, 1e-9);
    CHECK(ride(-80) > 5.0);
}
TEST_CASE("MS05 Source weights the detector: Bass listens to the lows, Vocal to the mids") {
    std::vector<float> in = sine(-12, 12, 60); const auto mid = sine(-34, 12, 1500); for (size_t i = 0; i < in.size(); ++i) in[i] += mid[i];
    auto ride = [&](int src) { auto p = make({{Write, 1}, {Source, static_cast<double>(src)}, {Range, 24}}); run(p, in); return p.rideDb(); };
    CHECK(ride(0) > ride(2) + 6.0);   // the quiet vocal-band content wants lifting, the loud bass wants lowering
}
TEST_CASE("MS05 Write Off: the Ride parameter (host automation) is the gain") {
    auto p = make({{Ride, 6}});
    NEAR(rmsDb(run(p, sine(-30, 2))), -24.0, 0.2);
    int id = 0; double v = 0; CHECK(p.takeParamWrite(id, v) == 0);
    NEAR(p.rideDb(), 6.0, 1e-6);
}
TEST_CASE("MS05 Write On reports the ride to the host as a gesture: begin + values ... end") {
    auto p = make({{Write, 1}, {Range, 12}});
    int id = -1; double v = 0; int flags = 0, begins = 0, ends = 0, values = 0; double last = 0;
    const auto in = sine(-26, 6);
    for (size_t off = 0; off + 256 <= in.size(); off += 256) {
        std::vector<float> a(in.begin() + off, in.begin() + off + 256), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256);
        flags = p.takeParamWrite(id, v);
        if (flags & 1) ++begins; if (flags & 2) { ++values; last = v; CHECK(id == Ride); } if (flags & 4) ++ends;
    }
    CHECK(begins == 1); CHECK(ends == 0); CHECK(values > 100);
    NEAR(last, p.rideDb(), 0.5);
    p.setParam(Write, 0);
    std::vector<float> a(256, 0.0f), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256);
    flags = p.takeParamWrite(id, v); CHECK((flags & 4) != 0);
    CHECK(p.takeParamWrite(id, v) == 0);
}
TEST_CASE("MS05 silence stays silent, extreme input finite, latency 0") {
    { auto z = make({{Write, 1}}); for (float v : run(z, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    auto p = make({{Write, 1}, {Range, 24}, {Gate, -80}});
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : run(p, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}
