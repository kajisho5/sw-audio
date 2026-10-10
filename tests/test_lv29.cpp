#include "doctest.h"
#include "lv29/lv29.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::lv29;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::pair<std::vector<float>, std::vector<float>> mix(Processor& p, const std::vector<float>& floor, const std::vector<float>& interp) {
    std::vector<float> l = floor, r = floor;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); std::vector<float> s(interp.begin() + off, interp.begin() + off + n); float* c[2] = {l.data() + off, r.data() + off}; const float* sc[1] = {s.data()}; p.processWithSidechain(c, 2, n, sc, 1); }
    return {l, r};
}
}

TEST_CASE("LV29 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Output].labels == std::vector<std::string>{"Floor", "Interp and floor", "Interp"}); CHECK(s[Output].def == 1);
    CHECK(s[FloorUnder].min == -40); CHECK(s[FloorUnder].max == 0); CHECK(s[FloorUnder].def == -14);
    CHECK(s[Crossfade].min == 50); CHECK(s[Crossfade].max == 2000); CHECK(s[Crossfade].def == 400); CHECK(s[Crossfade].curve == Curve::Log);
    CHECK(s[InterpLevel].min == -20); CHECK(s[InterpLevel].max == 10); CHECK(s[InterpLevel].def == 0); CHECK(s[AutoDetect].def == 1);
    Processor q; CHECK(q.latencySamples() == 0);
    // Interp from (SW Link; appended at the end): the host's sidechain or the output of the instance of one LIVE product (not LV29 itself)
    const auto& f = s[InterpFrom];
    CHECK(std::string(f.id) == "lv29.link.interp"); CHECK(f.def == 0); CHECK_FALSE(f.automatable); CHECK(f.curve == Curve::Step);
    REQUIRE(f.labels.size() > 20); CHECK(f.labels[0] == "Sidechain"); CHECK(f.steps.size() == f.labels.size());
    CHECK(std::find(f.labels.begin(), f.labels.end(), "LV01 Voice") != f.labels.end()); CHECK(std::find(f.labels.begin(), f.labels.end(), "LV29 Interp Mix") == f.labels.end());
    for (size_t k = 1; k < f.labels.size(); ++k) { CHECK(f.labels[k].rfind("LV", 0) == 0); CHECK(f.labels[k].rfind(Processor::interpProduct(static_cast<int>(k)), 0) == 0); }
    CHECK(Processor::interpProduct(0) == nullptr);
}
TEST_CASE("LV29 without an interpreter the floor passes untouched") {
    auto p = make(); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV29 Floor and Interp outputs") {
    const auto floor = sine(-20, 2.0, 300), interp = sine(-20, 2.0, 900);
    auto f = make({{Output, FloorOnly}}); const auto a = mix(f, floor, interp); CHECK(std::abs(binDb(a.first, 300, 72000, 96000) - binDb(floor, 300, 72000, 96000)) < 0.1); CHECK(binDb(a.first, 900, 72000, 96000) < -80.0);
    auto i = make({{Output, InterpOnly}, {InterpLevel, 6}}); const auto b = mix(i, floor, interp); CHECK(std::abs(binDb(b.first, 900, 72000, 96000) - (binDb(interp, 900, 72000, 96000) + 6.0)) < 0.2); CHECK(binDb(b.first, 300, 72000, 96000) < -60.0); CHECK(b.first == b.second);
}
TEST_CASE("LV29 Interp and floor: the floor goes under only while the interpreter speaks (Auto detect)") {
    const auto floor = sine(-20, 6.0, 300); std::vector<float> interp = voice(180.0, 6.0, 0.0, 700.0, 1800.0, 0.1); for (size_t i = 0; i < 3 * 48000; ++i) interp[i] = 0.0f;   // silent for 3 s, then speaks
    auto p = make({{Crossfade, 100}}); const auto o = mix(p, floor, interp);
    CHECK(std::abs(binDb(o.first, 300, 24000, 72000) - binDb(floor, 300, 24000, 72000)) < 0.3);                   // pause: the floor at full level
    CHECK(std::abs((binDb(o.first, 300, 5 * 48000, 6 * 48000) - binDb(floor, 300, 5 * 48000, 6 * 48000)) - (-14.0)) < 1.0);   // speaking: 14 dB under
    CHECK(p.interpreterSpeaking()); CHECK(std::abs(p.floorGainDb() - (-14.0)) < 0.5);
    auto off = make({{AutoDetect, 0}}); const auto q = mix(off, floor, interp); CHECK(std::abs((binDb(q.first, 300, 24000, 72000) - binDb(floor, 300, 24000, 72000)) - (-14.0)) < 0.5);   // always under
}
TEST_CASE("LV29 claps and noise on the interpreter's line do not duck the floor") {
    const auto floor = sine(-20, 4.0, 300); auto interp = noise(-25, 4.0, 5); auto p = make({{Crossfade, 100}}); const auto o = mix(p, floor, interp);
    CHECK_FALSE(p.interpreterSpeaking()); CHECK(std::abs(p.floorGainDb()) < 0.5); (void)o;
}
TEST_CASE("LV29 Crossfade sets the time of a switch") {
    const auto floor = sine(-20, 3.0, 300), interp = sine(-20, 3.0, 900);
    auto slow = make({{Output, FloorOnly}, {Crossfade, 1000}}); slow.snapToTargets(); slow.setParam(Output, InterpOnly);
    auto fast = make({{Output, FloorOnly}, {Crossfade, 100}}); fast.snapToTargets(); fast.setParam(Output, InterpOnly);
    const auto a = mix(slow, floor, interp), b = mix(fast, floor, interp);
    CHECK(binDb(b.first, 300, 14400, 19200) < binDb(floor, 300, 14400, 19200) - 20.0);   // 0.3-0.4 s: the fast one is already over
    CHECK(binDb(a.first, 300, 14400, 19200) > binDb(floor, 300, 14400, 19200) - 12.0);    // the slow one is only part of the way (a one-pole with a third of the time: 0.37 at 0.35 s)
    CHECK(binDb(a.first, 900, 2 * 48000, 3 * 48000) > binDb(interp, 900, 2 * 48000, 3 * 48000) - 1.0);
}
TEST_CASE("LV29 stereo sidechain, mono, odd blocks, before prepare") {
    auto p = make({{Output, InterpOnly}}); std::vector<float> l = noise(-20, 0.5, 3); std::vector<float> s0 = noise(-20, 0.5, 4), s1 = s0;
    for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; const float* sc[2] = {s0.data() + off, s1.data() + off}; p.processWithSidechain(c, 1, n, sc, 2); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}

TEST_CASE("LV29 when the interpreter's line goes away (the sidechain is unplugged, the SW Link source stops) the floor comes back over the Crossfade, not at once") {
    const auto floor = sine(-20, 4.0, 300), interp = sine(-20, 4.0, 900);
    auto p = make({{AutoDetect, 0}, {Crossfade, 400}});   // Auto detect Off: the floor is under whenever the line is there
    std::vector<float> a(floor.begin(), floor.begin() + 2 * 48000), s(interp.begin(), interp.begin() + 2 * 48000), r = a;
    for (size_t off = 0; off < a.size(); off += 256) { float* c[2] = {a.data() + off, r.data() + off}; const float* sc[2] = {s.data() + off, s.data() + off}; p.processWithSidechain(c, 2, 256, sc, 2); }
    NEAR(p.floorGainDb(), -14.0, 0.5);
    // the line is gone: the same floor goes in with no sidechain at all
    std::vector<float> b(floor.begin() + 2 * 48000, floor.end()), br = b;
    for (size_t off = 0; off < b.size(); off += 256) { float* c[2] = {b.data() + off, br.data() + off}; p.processWithSidechain(c, 2, 256, nullptr, 0); }
    const double ref0 = binDb(floor, 300, 2 * 48000, 2 * 48000 + 2400);
    CHECK(binDb(b, 300, 0, 2400) < ref0 - 6.0);                  // the first 50 ms: still well under (it did not jump to full level)
    CHECK(binDb(b, 300, 24000, 48000) > ref0 - 1.0);             // after 0.5 s .. 1 s: back to full
    NEAR(p.floorGainDb(), 0.0, 0.2);
    // and without any line from the start the floor is the input, bit for bit (as before)
    auto q = make({{AutoDetect, 0}}); const auto x = noise(-20, 1.0, 3), y = run(q, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
