#include "doctest.h"
#include "lv22/lv22.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv22;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> delayed(const std::vector<float>& x, size_t d, float g = 1.0f) { std::vector<float> y(x.size(), 0.0f); for (size_t i = d; i < x.size(); ++i) y[i] = g * x[i - d]; return y; }
void feed(Processor& p, std::vector<float> l, std::vector<float> r, const std::vector<float>* ref = nullptr) {
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off};
        if (ref) { const float* sc[1] = {ref->data() + off}; p.processWithSidechain(c, 2, n, sc, 1); } else p.process(c, 2, n); }
}
}

TEST_CASE("LV22 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mode].labels == std::vector<std::string>{"Mic vs mic", "Speaker", "Line"}); CHECK(s[Mode].def == 0);
    CHECK(s[Window].min == 50); CHECK(s[Window].max == 1000); CHECK(s[Window].def == 200); CHECK(s[Window].curve == Curve::Log); CHECK(s[HoldResult].def == 1);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV22 the sound passes untouched") {
    auto p = make(); const auto x = noise(-20, 1.0, 3); std::vector<float> l = x, r = x; feed(p, l, r); std::vector<float> l2 = x, r2 = x; float* c[2] = {l2.data(), r2.data()}; Processor q = make(); q.process(c, 2, static_cast<int>(l2.size()));
    for (size_t i = 0; i < x.size(); ++i) { REQUIRE(l2[i] == x[i]); REQUIRE(r2[i] == x[i]); }
}
TEST_CASE("LV22 Mic vs mic: in phase, out of phase, delayed, unrelated") {
    const auto x = noise(-20, 2.0, 3);
    { auto p = make(); feed(p, x, x); CHECK(p.result() == InPhase); CHECK(p.correlation() > 0.95); }
    { auto p = make(); auto inv = x; for (auto& v : inv) v = -v; feed(p, x, inv); CHECK(p.result() == OutOfPhase); CHECK(p.correlation() < -0.95); }
    { auto p = make(); auto inv = delayed(x, 96); for (auto& v : inv) v = -v; feed(p, inv, x); CHECK(p.result() == OutOfPhase); CHECK(std::abs(p.lagMs() - 2.0) < 0.2); }   // the test (left) 2 ms behind the reference (right), inverted
    { auto p = make(); feed(p, x, noise(-20, 2.0, 9)); CHECK(p.result() == Unknown); }
}
TEST_CASE("LV22 Speaker mode: the reference on the sidechain, the mic hears it later") {
    const auto ref = noise(-20, 2.0, 5); const auto mic = delayed(ref, 1440, 0.4f);   // 30 ms of air
    { auto p = make({{Mode, Speaker}}); feed(p, mic, mic, &ref); CHECK(p.result() == InPhase); CHECK(std::abs(p.lagMs() - 30.0) < 0.5); }
    { auto inv = mic; for (auto& v : inv) v = -v; auto p = make({{Mode, Speaker}}); feed(p, inv, inv, &ref); CHECK(p.result() == OutOfPhase); }
    { auto p = make({{Mode, Line}}); feed(p, mic, mic, &ref); CHECK(p.result() == Unknown); }   // a 30 ms lag is outside the +-1 ms of Line
}
TEST_CASE("LV22 with the LV21 Polarity pulse: positive pulse heard positive, inverted one negative") {
    std::vector<float> pulse(96000, 0.0f); for (size_t t = 0; t + 10 < pulse.size(); t += 24000) for (size_t k = 0; k < 5; ++k) pulse[t + k] = 0.5f;
    const auto air = delayed(pulse, 960);   // 20 ms
    { auto p = make({{Mode, Speaker}, {Window, 600}}); feed(p, air, air, &pulse); CHECK(p.result() == InPhase); }
    { auto inv = air; for (auto& v : inv) v = -v; auto p = make({{Mode, Speaker}, {Window, 600}}); feed(p, inv, inv, &pulse); CHECK(p.result() == OutOfPhase); }
}
TEST_CASE("LV22 Hold result keeps the last decisive answer") {
    const auto x = noise(-20, 1.0, 3); auto inv = x; for (auto& v : inv) v = -v; const auto z = noise(-20, 1.0, 9);
    auto h = make({{HoldResult, 1}}); feed(h, x, inv); CHECK(h.result() == OutOfPhase); feed(h, x, z); CHECK(h.result() == OutOfPhase);
    auto o = make({{HoldResult, 0}}); feed(o, x, inv); CHECK(o.result() == OutOfPhase); feed(o, x, z); CHECK(o.result() == Unknown);
}
TEST_CASE("LV22 silence is Unknown; mono, odd blocks, before prepare") {
    auto p = make(); feed(p, std::vector<float>(48000, 0.0f), std::vector<float>(48000, 0.0f)); CHECK(p.result() == Unknown);
    auto m = make(); std::vector<float> l = noise(-20, 1.0, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; m.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
