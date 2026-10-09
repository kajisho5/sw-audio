#include "doctest.h"
#include "sw/resonance.hpp"
#include "tu.hpp"
#include <algorithm>
#include <cmath>
using namespace sw;
using namespace tu;
namespace {
// noise with a few steady tones added: amplitude of the noise `noiseDb` (RMS), tones `{hz, db}` (RMS)
struct Tone { double hz, db; };
std::vector<float> make(double seconds, double noiseDb, std::vector<Tone> tones, uint64_t seed = 5) {
    Gauss g(seed); const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> x(n); const double na = std::pow(10.0, noiseDb / 20.0);
    for (size_t i = 0; i < n; ++i) { double v = na * g.gauss(); for (const auto& t : tones) v += std::sqrt(2.0) * std::pow(10.0, t.db / 20.0) * std::sin(2 * kPi * t.hz * static_cast<double>(i) / kFs); x[i] = static_cast<float>(v); }
    return x;
}
void feed(ResonanceFinder& r, const std::vector<float>& x) { std::vector<float> l = x, rr = x; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); const float* c[2] = {l.data() + off, rr.data() + off}; r.process(c, 2, n); } }
}

TEST_CASE("resonance finder: steady tones that stick out of the noise are marked, noise alone is not") {
    { ResonanceFinder r; r.setup(kFs); feed(r, make(8.0, -30, {})); ResonanceFinder::Mark m[ResonanceFinder::kMarks]; CHECK(r.marks(m) == 0); }   // noise only
    ResonanceFinder r; r.setup(kFs); feed(r, make(10.0, -30, {{1500, -22}}));
    ResonanceFinder::Mark m[ResonanceFinder::kMarks]; const int n = r.marks(m); REQUIRE(n >= 1);
    CHECK(m[0].hz == doctest::Approx(1500).epsilon(0.03)); CHECK(m[0].db > 6.0); CHECK(m[0].db < 40.0);
    CHECK(n == 1);   // one tone, one mark (no mark on its skirts)
    // two tones: both found, the one that sticks out more first
    ResonanceFinder q; q.setup(kFs); feed(q, make(10.0, -30, {{600, -26}, {4000, -20}}));
    const int k = q.marks(m); REQUIRE(k == 2); CHECK(m[0].db >= m[1].db);   // strongest first: by how far it sticks out of what is around it (a cell is as wide as its frequency: a low tone has less noise to stand out of)
    const double lo = std::min(m[0].hz, m[1].hz), hi = std::max(m[0].hz, m[1].hz); CHECK(lo == doctest::Approx(600).epsilon(0.04)); CHECK(hi == doctest::Approx(4000).epsilon(0.03));
}
TEST_CASE("resonance finder: it needs seconds of it, and lets go of a tone that has stopped") {
    ResonanceFinder r; r.setup(kFs); ResonanceFinder::Mark m[ResonanceFinder::kMarks];
    feed(r, make(0.5, -30, {{1500, -18}})); CHECK(r.marks(m) == 0);                    // half a second is not "lasting"
    feed(r, make(8.0, -30, {{1500, -18}})); CHECK(r.marks(m) >= 1);
    feed(r, make(12.0, -30, {}, 9)); CHECK(r.marks(m) == 0);                         // gone after the tone stopped
    r.reset(); feed(r, make(10.0, -30, {{1500, -18}})); CHECK(r.marks(m) >= 1); r.reset(); CHECK(r.marks(m) == 0);
}
TEST_CASE("resonance finder: any sample rate, silence, mono, extremes") {
    for (double fs : {44100.0, 96000.0}) { ResonanceFinder r; r.setup(fs); std::vector<float> x(static_cast<size_t>(8 * fs)); Gauss g(3); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.03 * g.gauss() + 0.05 * std::sin(2 * kPi * 2000.0 * static_cast<double>(i) / fs));
        for (size_t off = 0; off < x.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - off)); const float* c[1] = {x.data() + off}; r.process(c, 1, n); }
        ResonanceFinder::Mark m[ResonanceFinder::kMarks]; const int n = r.marks(m); REQUIRE(n >= 1); CHECK(m[0].hz == doctest::Approx(2000).epsilon(0.03)); }
    ResonanceFinder r; r.setup(kFs); std::vector<float> z(48000, 0.0f), big(48000, 5.0f); feed(r, z); feed(r, big);
    ResonanceFinder::Mark m[ResonanceFinder::kMarks]; const int n = r.marks(m); for (int i = 0; i < n; ++i) { CHECK(std::isfinite(m[i].hz)); CHECK(std::isfinite(m[i].db)); }
    ResonanceFinder u; u.setup(kFs); const std::vector<float> a(256, 0.1f); const float* c[2] = {a.data(), a.data()}; u.process(c, 2, 256); u.process(c, 0, 256); u.process(c, 2, 0); CHECK(u.marks(m) == 0);   // before it has seen a second
}
