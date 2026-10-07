#include "doctest.h"
#include "lv27/lv27.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv27;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
}

TEST_CASE("LV27 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[FollowScenes].def == 1); CHECK(s[FadeBetween].min == 0); CHECK(s[FadeBetween].max == 1000); CHECK(s[FadeBetween].def == 300);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV27 the sound passes untouched") {
    auto p = make(); const auto x = noise(-20, 1.0, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV27 Learn current registers the scene; a scene change recalls its preset") {
    auto p = make(); p.onSceneChanged("Interview"); CHECK(p.currentScene() == "Interview"); CHECK(p.learnCurrent(3)); CHECK(p.lookup("Interview") == 3);
    p.onSceneChanged("Music"); CHECK(p.activePreset() == 0); CHECK(p.learnCurrent(5));
    p.onSceneChanged("Interview"); CHECK(p.activePreset() == 3); CHECK(p.previousPreset() == 0); p.onSceneChanged("Music"); CHECK(p.activePreset() == 5); CHECK(p.previousPreset() == 3);
    p.onSceneChanged("Unknown scene"); CHECK(p.activePreset() == 5);   // nothing mapped: stay
    CHECK(p.mappings().size() == 2); CHECK(p.learnCurrent(7)); CHECK(p.lookup("Unknown scene") == 7);
}
TEST_CASE("LV27 Follow scenes Off ignores the scenes; a mapping can be changed and removed") {
    auto p = make({{FollowScenes, 0}}); p.setMapping("A", 2); p.onSceneChanged("A"); CHECK(p.activePreset() == 0); CHECK(p.currentScene() == "A");
    p.setParam(FollowScenes, 1); p.onSceneChanged("A"); CHECK(p.activePreset() == 2); CHECK(p.setMapping("A", 4)); CHECK(p.lookup("A") == 4); CHECK(p.mappings().size() == 1);
    CHECK(p.removeMapping("A")); CHECK(p.lookup("A") == 0); CHECK_FALSE(p.removeMapping("A"));
    CHECK_FALSE(p.setMapping("", 1)); CHECK_FALSE(p.setMapping("x", 0)); CHECK_FALSE(p.setMapping("x", 33)); Processor e = make(); CHECK_FALSE(e.learnCurrent(1));   // no scene reported yet
}
TEST_CASE("LV27 the fade runs over Fade between") {
    auto p = make({{FadeBetween, 300}}); p.setMapping("A", 1); p.onSceneChanged("A"); CHECK(p.fadeProgress() == 0.0);
    std::vector<float> z(256, 0.0f); float* c[2] = {z.data(), z.data()}; for (int i = 0; i < 28; ++i) p.process(c, 2, 256);   // 149 ms
    CHECK(std::abs(p.fadeProgress() - 0.5) < 0.05); for (int i = 0; i < 30; ++i) p.process(c, 2, 256); CHECK(p.fadeProgress() == 1.0);
    auto q = make({{FadeBetween, 0}}); q.setMapping("A", 1); q.onSceneChanged("A"); CHECK(q.fadeProgress() == 1.0);
}
TEST_CASE("LV27 the table is saved and loaded; damaged data is refused piece by piece") {
    auto p = make(); p.setMapping("Intro", 1); p.setMapping("日本語のシーン", 9); std::vector<uint8_t> st; p.saveExtra(st);
    auto q = make(); q.loadExtra(st.data(), st.size()); REQUIRE(q.mappings().size() == 2); CHECK(q.lookup("Intro") == 1); CHECK(q.lookup("日本語のシーン") == 9);
    auto r = make(); r.loadExtra(st.data(), st.size() - 3); CHECK(r.mappings().size() == 1);   // cut short: what is whole is kept
    std::vector<uint8_t> bad = {200, 99, 3}; auto u = make(); u.loadExtra(bad.data(), bad.size()); CHECK(u.mappings().empty()); u.loadExtra(nullptr, 0);
}
TEST_CASE("LV27 mono, odd blocks, before prepare") {
    auto p = make(); std::vector<float> l = noise(-20, 1.0, 3), keep = l; for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); } CHECK(l == keep);
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); z.onSceneChanged("x");
}
