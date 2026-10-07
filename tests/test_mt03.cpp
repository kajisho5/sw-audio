#include "doctest.h"
#include "mt03/mt03.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::mt03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
std::vector<float> feed(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return l; }
}

TEST_CASE("MT03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"mt03.scale", "mt03.scroll", "mt03.floor", "mt03.contrast", "mt03.palette", "mt03.notes", "mt03.freq"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Scale].labels == std::vector<std::string>{"Linear", "Log", "Mel"}); CHECK(s[Scale].def == 1);
    CHECK(s[Scroll].min == 2); CHECK(s[Scroll].max == 60); CHECK(s[Scroll].def == 10); CHECK(s[Scroll].curve == Curve::Log);
    CHECK(s[Floor].min == -120); CHECK(s[Floor].max == -60); CHECK(s[Floor].def == -90);
    CHECK(s[Contrast].def == 1); CHECK(s[Palette].labels == std::vector<std::string>{"Mono", "Heat"}); CHECK(s[Palette].def == 0); CHECK(s[ShowNotes].def == 0); CHECK(s[ShowFreq].def == 1);
}
TEST_CASE("MT03 the signal passes unchanged; no delay") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); const auto x = noise(-18, 2.0, 3); const auto y = feed(p, x); for (size_t i = 0; i < x.size(); ++i) CHECK(y[i] == x[i]);
}
TEST_CASE("MT03 the scales: edges, centres and the band of a frequency") {
    for (int sc : {Linear, Log, Mel}) {
        double prev = 0; for (int b = 0; b < kBands; ++b) { const double f = bandHz(sc, b); CHECK(f > prev); prev = f; CHECK(bandOf(sc, f) == b); }
        CHECK(bandHz(sc, 0) > 20.0); CHECK(bandHz(sc, kBands - 1) < 20000.0);
    }
    CHECK(bandHz(Log, 128) / bandHz(Log, 0) > 20.0); NEAR(bandHz(Log, 128), std::sqrt(20.0 * 20000.0), 150.0);      // the middle of the log scale: the geometric mean
    NEAR(bandHz(Linear, 128), 10010.0, 120.0);
    CHECK(bandHz(Mel, 128) < bandHz(Linear, 128)); CHECK(bandHz(Mel, 128) > bandHz(Log, 128));
}
TEST_CASE("MT03 a tone shows in its band on every scale, with its level") {
    for (int sc : {0, 1, 2}) {
        auto p = make({{Scale, double(sc)}, {Floor, -120}}); feed(p, sine(-9.0, 2.0, 1000.0));
        std::vector<float> c; p.column(p.columnCount() - 1, c); int best = 0; for (int b = 1; b < kBands; ++b) if (c[static_cast<size_t>(b)] > c[static_cast<size_t>(best)]) best = b;
        CHECK(std::abs(best - bandOf(sc, 1000.0)) <= 1);
        NEAR(c[static_cast<size_t>(best)], -9.0 + 3.0, 1.6);   // -9 dBFS rms = -6 dBFS peak (Hann scalloping up to 1.4 dB)
        CHECK(c[static_cast<size_t>(std::min(kBands - 1, best + 30))] < -60.0f);
    }
}
TEST_CASE("MT03 Scroll sets how many columns are kept; Floor is the lowest value") {
    auto p = make({{Scroll, 2}}); feed(p, noise(-30, 6.0, 3)); const double hopS = static_cast<double>(kHop) / kFs;
    NEAR(p.columnCount() * hopS, 2.0, 0.05);
    auto q = make({{Scroll, 20}}); feed(q, noise(-30, 6.0, 3)); CHECK(q.columnCount() > 250); NEAR(q.columnCount() * hopS, 6.0 - kFft / kFs, 0.1);
    q.setParam(Scroll, 2); CHECK(q.columnCount() * hopS <= 2.05);
    auto f = make({{Floor, -60}}); feed(f, sine(-80, 1.0, 1000)); std::vector<float> c; f.column(f.columnCount() - 1, c); for (float v : c) CHECK(v >= -60.0f);
}
TEST_CASE("MT03 the band-pass preview listens to the band, changes the output meanwhile, and fades in and out") {
    auto p = make(); CHECK_FALSE(p.previewActive());
    p.setPreview(true, 800.0, 1250.0); CHECK(p.previewActive());
    std::vector<float> a = sine(-12, 2.0, 1000), b = sine(-12, 2.0, 5000), mix(a.size()); for (size_t i = 0; i < a.size(); ++i) mix[i] = a[i] + b[i];
    const auto y = feed(p, mix);
    NEAR(binDb(y, 1000, 48000, 96000), binDb(a, 1000, 48000, 96000), 1.0);   // the centre keeps its level CHECK(binDb(y, 5000, 48000, 96000) < binDb(b, 5000, 48000, 96000) - 40.0);
    p.setPreview(false, 800, 1250); const auto z = feed(p, noise(-18, 1.0, 5)); CHECK(p.previewActive() == false);
    auto q = make(); const auto n = noise(-18, 1.0, 5); auto w = feed(q, n); for (size_t i = 0; i < n.size(); ++i) CHECK(w[i] == n[i]);   // never on: untouched
}
TEST_CASE("MT03 silence and loud input are finite") {
    auto p = make(); feed(p, std::vector<float>(48000, 0.0f)); std::vector<float> c; p.column(p.columnCount() - 1, c); for (float v : c) CHECK(std::isfinite(v));
    auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; for (float v : feed(p, x)) CHECK(std::isfinite(v));
}
