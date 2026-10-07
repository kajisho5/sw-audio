#include "doctest.h"
#include "lv25/lv25.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv25;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> impulse(size_t n, float a = 0.5f) { std::vector<float> x(n, 0.0f); x[0] = a; return x; }
size_t peakAt(const std::vector<float>& y, size_t from, size_t to) { size_t b = from; for (size_t i = from; i < to; ++i) if (std::abs(y[i]) > std::abs(y[b])) b = i; return b; }
void blocks(Processor& p, size_t n) { std::vector<float> z(256, 0.0f); for (size_t i = 0; i < n; i += 256) { float* c[2] = {z.data(), z.data()}; p.process(c, 2, 256); } }
}

TEST_CASE("LV25 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Clock].labels == std::vector<std::string>{"Tap", "MIDI", "BPM"}); CHECK(s[Clock].def == 0);
    CHECK(s[Time].min == 1); CHECK(s[Time].max == 2000); CHECK(s[Time].def == 500); CHECK(s[Time].curve == Curve::Log);
    CHECK(s[Feedback].min == 0); CHECK(s[Feedback].max == 95); CHECK(s[Feedback].def == 35);
    CHECK(s[Tone].labels == std::vector<std::string>{"Dark", "Neutral", "Bright"}); CHECK(s[Tone].def == 0); CHECK(s[Mix].def == 15);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV25 the echo comes after Time, repeats with Feedback") {
    auto p = make({{Time, 250}, {Feedback, 50}, {Tone, 2}}); const auto y = run(p, impulse(48000 * 3));
    const size_t a = peakAt(y, 100, 24000), b = peakAt(y, a + 100, a + 24000); CHECK(std::abs(static_cast<double>(a) - 12000.0) < 8.0); CHECK(std::abs(static_cast<double>(b) - 24000.0) < 8.0);
    CHECK(std::abs(y[b]) < 0.75 * std::abs(y[a])); CHECK(std::abs(y[b]) > 0.3 * std::abs(y[a]));
    for (size_t i = 0; i < 11990; ++i) REQUIRE(y[i] == 0.0f);   // nothing dry, nothing before the first echo
}
TEST_CASE("LV25 Feedback 95 % settles; Feedback 0 gives one echo") {
    auto p = make({{Time, 100}, {Feedback, 95}}); const auto y = run(p, noise(-6, 4.0, 3)); for (float v : y) REQUIRE(std::isfinite(v)); CHECK(peakDb(y, 0, y.size()) < 12.0);
    auto q = make({{Time, 100}, {Feedback, 0}, {Tone, 2}}); const auto z = run(q, impulse(48000)); CHECK(std::abs(z[peakAt(z, 6000, 9000)]) < 1e-3f);
}
TEST_CASE("LV25 Tone darkens the repeats") {
    auto dark = make({{Time, 100}, {Feedback, 60}, {Tone, 0}}), bright = make({{Time, 100}, {Feedback, 60}, {Tone, 2}}); const auto a = run(dark, impulse(48000 * 2)), b = run(bright, impulse(48000 * 2));
    auto hf = [&](const std::vector<float>& y) { return binDb(y, 9000, 20000, 70000) - binDb(y, 500, 20000, 70000); }; CHECK(hf(b) > hf(a) + 6.0);
}
TEST_CASE("LV25 Tap: the distance of the taps becomes the Time and goes to the host") {
    auto p = make({{Clock, Tap}, {Time, 500}});
    p.tap(); blocks(p, 24000); p.tap();   // 0.5 s
    int id; double v; { const bool w = p.takeParamWrite(id, v); CHECK((!w || std::abs(v - 500.0) < 10.0)); }
    Processor q = make({{Clock, Tap}}); q.tap(); blocks(q, static_cast<size_t>(0.3 * 48000)); q.tap(); REQUIRE(q.takeParamWrite(id, v)); CHECK(id == Time); CHECK(std::abs(v - 300.0) < 6.0); CHECK(std::abs(q.timeMs() - 300.0) < 6.0);
    blocks(q, static_cast<size_t>(0.3 * 48000)); q.tap(); blocks(q, static_cast<size_t>(0.3 * 48000)); q.tap(); REQUIRE(q.takeParamWrite(id, v)); CHECK(std::abs(v - 300.0) < 6.0);   // the average of the intervals
    blocks(q, 48000 * 4); q.tap(); CHECK_FALSE(q.takeParamWrite(id, v));   // a long pause starts over: one tap is no interval
}
TEST_CASE("LV25 BPM follows the host tempo; MIDI follows the clock; the knob is the fallback") {
    auto b = make({{Clock, Bpm}, {Time, 400}}); CHECK(std::abs(b.timeMs() - 400.0) < 0.5); b.setTempo(120.0); CHECK(std::abs(b.timeMs() - 500.0) < 0.5); b.setTempo(75.0); CHECK(std::abs(b.timeMs() - 800.0) < 0.5);
    auto m = make({{Clock, Midi}, {Time, 400}}); CHECK(std::abs(m.timeMs() - 400.0) < 0.5);
    for (int t = 0; t < 24 * 3; ++t) { m.midiClockTick(); blocks(m, static_cast<size_t>(48000.0 / 2.0 / 24.0 / 256.0 * 256.0 + 0.5)); }   // 120 bpm: a tick every 20.8 ms (a few blocks of 256)
    CHECK(std::abs(m.timeMs() - 500.0) < 30.0);
}
TEST_CASE("LV25 Input bypass lets the echoes ring out but takes no new input") {
    auto p = make({{Time, 200}, {Feedback, 70}, {Tone, 2}}); run(p, impulse(9600)); p.setParam(InputBypass, 1);
    const auto y = run(p, impulse(48000 * 2, 0.5f)); const size_t a = peakAt(y, 100, 24000); CHECK(std::abs(y[a]) > 0.01f);   // the old echoes continue
    double s = 0; for (size_t i = 0; i < 9600; ++i) s += std::abs(y[i]); CHECK(s > 0.0); CHECK(peakDb(y, 48000, 96000) < peakDb(y, 0, 24000) - 3.0);
    auto q = make({{Time, 200}, {Feedback, 70}, {InputBypass, 1}}); const auto z = run(q, impulse(48000)); for (float v : z) REQUIRE(v == 0.0f);   // nothing came in, nothing comes out
}
TEST_CASE("LV25 time changes glide; mono; odd blocks; before prepare") {
    auto p = make({{Time, 100}}); run(p, noise(-20, 0.5, 3)); p.setParam(Time, 700); const auto y = run(p, noise(-20, 0.5, 3)); for (float v : y) REQUIRE(std::isfinite(v));
    auto m = make(); std::vector<float> l = noise(-20, 1.0, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; m.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); z.tap(); z.midiClockTick();
}
