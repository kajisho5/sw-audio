#include "doctest.h"
#include "dl02/dl02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::dl02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// no wear, no feedback, flat tone
Set clean(int heads, double ms = 100.0) { return {{Heads, double(heads)}, {Rate, ms}, {Intensity, 0}, {Wear, 0}, {Bass, 0}, {Treble, 0}}; }
std::vector<float> impulse(double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 0.5f; return x; }
size_t peakAt(const std::vector<float>& y, size_t a, size_t b) { size_t k = a; for (size_t i = a; i < std::min(b, y.size()); ++i) if (std::abs(y[i]) > std::abs(y[k])) k = i; return k; }
double peakAbs(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, y.size()); ++i) m = std::max(m, double(std::abs(y[i]))); return m; }
}

TEST_CASE("DL02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"dl02.heads", "dl02.rate", "dl02.intensity", "dl02.bass", "dl02.treble", "dl02.wear", "dl02.mix", "dl02.unit"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Heads].labels == std::vector<std::string>{"1", "2", "3", "1+2", "2+3", "All"}); CHECK(s[Heads].def == 3);
    CHECK(s[Rate].min == 50); CHECK(s[Rate].max == 200); CHECK(s[Rate].curve == Curve::Log); CHECK(s[Rate].reversed);   // Slow (long) on the left
    NEAR(s[Rate].toValue(0.0), 200.0, 1e-9); NEAR(s[Rate].toValue(1.0), 50.0, 1e-9); NEAR(s[Rate].toValue(0.5), 100.0, 1e-9);
    CHECK(s[Intensity].def == 4); CHECK(s[Intensity].max == 10);
    CHECK(s[Bass].min == -6); CHECK(s[Bass].max == 6); CHECK(s[Treble].min == -6); CHECK(s[Treble].max == 6);
    CHECK(s[Wear].def == 3); CHECK(s[Wear].max == 10); CHECK(s[Mix].def == 25);
    CHECK(headMask(H12) == 3); CHECK(headMask(H23) == 6); CHECK(headMask(All) == 7);
}
TEST_CASE("DL02 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("DL02 the heads sit at 1 : 2 : 3 of Rate") {
    for (double ms : {50.0, 100.0, 200.0}) for (int h = 0; h < 6; ++h) {
        auto p = make(clean(h, ms));
        const auto y = run(p, impulse(1.0));
        const int mask = headMask(h); const double t = ms * 0.001 * kFs;
        const double noise = peakAbs(y, 0, static_cast<size_t>(0.5 * t));   // nothing before the first head
        CHECK(noise < 1e-6);
        double top = 0; for (int k = 0; k < 3; ++k) if ((mask >> k) & 1) top = std::max(top, peakAbs(y, static_cast<size_t>((k + 1) * t - 8), static_cast<size_t>((k + 1) * t + 8)));
        for (int k = 0; k < 3; ++k) {
            const size_t a = static_cast<size_t>((k + 1) * t - 8), b = static_cast<size_t>((k + 1) * t + 8);
            if ((mask >> k) & 1) { CHECK(peakAbs(y, a, b) > 0.05); CHECK(std::abs(double(peakAt(y, a, b)) - (k + 1) * t) < 3.0); }
            else CHECK(peakAbs(y, a, b) < 1e-6);
        }
        (void)top;
    }
}
TEST_CASE("DL02 Intensity is the feedback (0 .. 110 %)") {
    auto tone = [](double db) { auto x = sine(db, 0.2, 300); x.resize(static_cast<size_t>(3 * kFs), 0.0f); return x; };
    for (double in : {3.0, 6.0, 9.0}) {
        Set s = clean(H1, 100.0); s.push_back({Intensity, in});
        auto p = make(s); const auto y = run(p, tone(-24));
        const size_t d = 4800;
        const double r = rmsDb(y, 3 * d + 480, 3 * d + 3840) - rmsDb(y, 2 * d + 480, 2 * d + 3840);   // second repeat / first repeat
        const double fb = in * 0.11;
        NEAR(r, 20 * std::log10(fb), 1.5);
    }
    // 110 %: it rings on and does not run away
    Set s = clean(All, 60.0); s.push_back({Intensity, 10});
    auto p = make(s); auto x = noise(-12, 0.3, 4); x.resize(static_cast<size_t>(20 * kFs), 0.0f);
    const auto y = run(p, x); double m = 0; for (float v : y) { CHECK(std::isfinite(v)); m = std::max(m, double(std::abs(v))); }
    CHECK(m < 4.0); CHECK(peakAbs(y, y.size() - 48000, y.size()) > 0.02);
}
TEST_CASE("DL02 Bass and Treble shape the echo inside the loop") {
    auto first = [&](double f, double bass, double treble) { Set s = clean(H1, 100.0); s.push_back({Bass, bass}); s.push_back({Treble, treble}); auto p = make(s); auto x = sine(-24, 0.3, f); x.resize(static_cast<size_t>(1.0 * kFs), 0.0f); const auto y = run(p, x); return rmsDb(y, 4800 + 4800, 4800 + 12000); };
    CHECK(first(60, 6, 0) > first(60, 0, 0) + 4.0); CHECK(first(60, -6, 0) < first(60, 0, 0) - 4.0);
    CHECK(first(8000, 0, 6) > first(8000, 0, 0) + 4.0); CHECK(first(8000, 0, -6) < first(8000, 0, 0) - 4.0);
    NEAR(first(1000, 6, 6), first(1000, 0, 0), 1.0);   // the middle stays
}
TEST_CASE("DL02 tape speed ties the bandwidth: a short Rate is brighter") {
    auto hi = [&](double ms) { auto p = make(clean(H1, ms)); auto x = sine(-24, 0.3, 9000); x.resize(static_cast<size_t>(1.0 * kFs), 0.0f); const size_t d = static_cast<size_t>(ms * 0.001 * kFs); const auto y = run(p, x); return rmsDb(y, d + 4800, d + 12000); };
    CHECK(hi(50) > hi(100) + 2.0); CHECK(hi(100) > hi(200) + 2.0);
}
TEST_CASE("DL02 Wear: high-frequency loss, wow and dropouts") {
    auto hiLoss = [&](double wear) { Set s = clean(H1, 100.0); s.push_back({Wear, wear}); auto p = make(s); auto x = sine(-24, 0.3, 8000); x.resize(static_cast<size_t>(1.0 * kFs), 0.0f); const auto y = run(p, x); return rmsDb(y, 4800 + 4800, 4800 + 12000); };
    CHECK(hiLoss(10) < hiLoss(0) - 6.0);
    // wow: the 1 kHz carrier loses energy to the sidebands
    auto carrier = [&](double wear) { Set s = clean(H1, 100.0); s.push_back({Wear, wear}); auto p = make(s); const auto y = run(p, sine(-24, 3.0, 1000)); return binDb(y, 1000, 24000, 140000) - rmsDb(y, 24000, 140000); };
    CHECK(carrier(10) < carrier(0) - 1.0); CHECK(carrier(0) > -0.5);
    // dropouts: short windows dip well below the typical level; none at Wear 0
    auto spread = [&](double wear) {
        Set s = clean(H1, 100.0); s.push_back({Wear, wear}); auto p = make(s); const auto y = run(p, sine(-24, 20.0, 300));
        double mn = 1e9, mx = -1e9; for (size_t a = 48000; a + 480 <= y.size(); a += 480) { const double r = rmsDb(y, a, a + 480); mn = std::min(mn, r); mx = std::max(mx, r); }
        return mx - mn; };
    CHECK(spread(0) < 0.3); CHECK(spread(10) > 3.0);
}
TEST_CASE("DL02 a Heads change fades over 10 ms, and waits for the bar line while playing") {
    auto dc = std::vector<float>(48000, 0.2f);
    auto levels = [&](bool bar) {
        Set s = clean(H1, 100.0); auto p = make(s);
        std::vector<float> y(dc.size());
        auto l = dc; auto r = dc;
        auto runTo = [&](size_t a, size_t b) { for (size_t off = a; off < b; off += 256) { const int n = static_cast<int>(std::min<size_t>(256, b - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } };
        p.setTempo(120.0);
        runTo(0, 24000);
        p.setParam(Heads, All);
        if (bar) p.setTransport(true, 0.5);   // half a beat at 120 bpm = 12000 samples to the bar line
        else p.setTransport(false, -1.0);
        runTo(24000, 48000);
        return l;
    };
    auto l = levels(false);
    const double before = l[23000], after = l[47000];
    CHECK(after > before * 1.5);                       // 1 head -> 3 heads: 3/sqrt(3) = 1.73 times
    const double lo = before + 0.1 * (after - before), hi = before + 0.9 * (after - before);
    size_t a = 24000, b = 24000; while (a < l.size() && l[a] < lo) ++a; b = a; while (b < l.size() && l[b] < hi) ++b;
    const double ms = (b - a) / kFs * 1000.0;
    CHECK(ms > 6.5); CHECK(ms < 9.5);                  // 10 % .. 90 % of a 10 ms ramp = 8 ms (the three heads read the same steady DC)
    // with the bar position known the change waits for the bar line
    auto w = levels(true);
    CHECK(w[24000 + 10000] < w[23000] * 1.05);         // still one head 10000 samples after the request
    CHECK(w[47000] > w[23000] * 1.5);                  // and then the change has been made (after 12000 samples)
}
