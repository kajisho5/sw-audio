#include "doctest.h"
#include "lv19/lv19.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv19;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV19 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[FrameRate].labels == std::vector<std::string>{"24", "25", "29.97", "30", "59.94"}); CHECK(s[FrameRate].def == 2);
    CHECK(s[Delay].min == 0); CHECK(s[Delay].max == 1000); CHECK(s[Delay].def == 0); CHECK(s[LockToVideo].def == 1);
    CHECK(framesPerSecond(2) == doctest::Approx(29.97003)); CHECK(framesPerSecond(4) == doctest::Approx(59.94006)); CHECK(framesPerSecond(0) == 24);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV19 Lock to video rounds the delay to whole frames; Frames follows") {
    auto p = make({{FrameRate, 3}, {Delay, 100.0}}); CHECK(p.effectiveDelayMs() == doctest::Approx(100.0)); CHECK(p.frames() == doctest::Approx(3.0));   // 30 fps: 33.33 ms a frame
    auto q = make({{FrameRate, 3}, {Delay, 110.0}}); CHECK(q.effectiveDelayMs() == doctest::Approx(100.0)); auto r = make({{FrameRate, 3}, {Delay, 120.0}}); CHECK(r.effectiveDelayMs() == doctest::Approx(133.333).epsilon(1e-4));
    auto u = make({{FrameRate, 3}, {Delay, 110.0}, {LockToVideo, 0}}); CHECK(u.effectiveDelayMs() == doctest::Approx(110.0)); CHECK(u.frames() == doctest::Approx(3.3).epsilon(1e-3));
    CHECK(make({{FrameRate, 0}}).frameMs() == doctest::Approx(41.6667).epsilon(1e-4));
}
TEST_CASE("LV19 delays the audio exactly; Delay 0 is bit for bit") {
    const auto x = noise(-20, 1.0, 3); { auto p = make(); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]); }
    auto p = make({{LockToVideo, 0}, {Delay, 50.0}}); const auto y = run(p, x); for (size_t i = 2500; i < x.size(); i += 19) REQUIRE(y[i] == x[i - 2400]);
    auto m = make({{LockToVideo, 0}, {Delay, 1000.0}}); const auto w = run(m, std::vector<float>(48000 * 2, 0.5f)); CHECK(w[47000] == 0.0f); CHECK(w[48200] == 0.5f);
}
TEST_CASE("LV19 Clap sync: the sound comes first -> the delay to apply") {
    // a clap at 1.0 s in the audio; the video clap is at 1.0 s + 120 ms (the sound is 120 ms early)
    std::vector<float> x = noise(-50, 3.0, 4); for (size_t i = 0; i < 300; ++i) x[48000 + i] += 0.6f * static_cast<float>(std::exp(-static_cast<double>(i) / 60.0) * (i % 2 ? 1 : -1));
    auto p = make({{LockToVideo, 0}}); size_t pos = 0; const size_t markAt = 48000 + 5760;
    for (; pos < x.size(); pos += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - pos)); if (pos <= markAt && markAt < pos + 256) { std::vector<float> l(x.begin() + pos, x.begin() + pos + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); p.markVideoClap(0.0); CHECK(p.syncState() == Waiting); continue; }
        std::vector<float> l(x.begin() + pos, x.begin() + pos + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); }
    CHECK(p.syncState() == Done); CHECK_FALSE(p.audioLate()); CHECK(std::abs(p.foundMs() - 120.0) < 10.0);   // the block holding the mark is up to 5.3 ms off
    int id; double v; REQUIRE(p.takeParamWrite(id, v)); CHECK(id == Delay); CHECK(std::abs(v - p.foundMs()) < 0.06); CHECK_FALSE(p.takeParamWrite(id, v));
}
TEST_CASE("LV19 Clap sync: a late sound cannot be fixed by delaying it; no clap fails") {
    std::vector<float> x = noise(-50, 3.0, 4); for (size_t i = 0; i < 300; ++i) x[48000 + 4800 + i] += 0.6f * static_cast<float>(std::exp(-static_cast<double>(i) / 60.0) * (i % 2 ? 1 : -1));   // audio clap at 1.1 s
    auto p = make(); size_t pos = 0; const size_t markAt = 48000;   // the video clap at 1.0 s: audio 100 ms late
    for (; pos < x.size(); pos += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - pos)); std::vector<float> l(x.begin() + pos, x.begin() + pos + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); if (pos <= markAt && markAt < pos + 256) p.markVideoClap(0.0); }
    CHECK(p.syncState() == Done); CHECK(p.audioLate()); CHECK(std::abs(p.lateMs() - 100.0) < 10.0); int id; double v; CHECK_FALSE(p.takeParamWrite(id, v));
    auto q = make(); const auto nz = noise(-40, 3.0, 5); for (size_t pos2 = 0; pos2 < nz.size(); pos2 += 256) { const int n = static_cast<int>(std::min<size_t>(256, nz.size() - pos2)); std::vector<float> l(nz.begin() + pos2, nz.begin() + pos2 + n), r = l; float* c[2] = {l.data(), r.data()}; q.process(c, 2, n); if (pos2 == 48000 / 256 * 256) q.markVideoClap(0.0); }
    CHECK(q.syncState() == Failed);
    auto z = make(); z.markVideoClap(0.0); CHECK(z.syncState() == Waiting);
}
TEST_CASE("LV19 reaction time moves the video moment earlier") {
    std::vector<float> x = noise(-50, 3.0, 4); for (size_t i = 0; i < 300; ++i) x[48000 + i] += 0.6f * static_cast<float>(std::exp(-static_cast<double>(i) / 60.0) * (i % 2 ? 1 : -1));
    auto run1 = [&](double reaction) { auto p = make({{LockToVideo, 0}}); const size_t markAt = 48000 + 14400; for (size_t pos = 0; pos < x.size(); pos += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - pos)); std::vector<float> l(x.begin() + pos, x.begin() + pos + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); if (pos <= markAt && markAt < pos + 256) p.markVideoClap(reaction); } return p.foundMs(); };
    CHECK(std::abs((run1(0.0) - run1(150.0)) - 150.0) < 1.0);
}
TEST_CASE("LV19 mono, odd blocks, before prepare") {
    auto p = make({{Delay, 20.0}}); std::vector<float> l = noise(-20, 1.0, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); z.markVideoClap(0.0); CHECK(z.syncState() == Idle);
}
