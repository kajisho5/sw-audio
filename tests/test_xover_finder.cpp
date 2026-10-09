// sw::CrossoverFinder (DY10 Auto): from 10 s of playing it takes the long-term average spectrum, weights it by the ear's sensitivity (K-weighting), cuts the weighted energy into four about equal parts and moves each
// cut to the nearest valley of the spectrum (within half an octave), keeping the three an octave apart
#include "doctest.h"
#include "sw/crossover_finder.hpp"
#include "tu.hpp"
#include "xover_program.hpp"
using namespace tu;
using xoverprog::clusters;
namespace {
void feed(sw::CrossoverFinder& f, const std::vector<float>& x, size_t block = 256) { for (size_t off = 0; off < x.size(); off += block) f.process(x.data() + off, static_cast<int>(std::min(block, x.size() - off))); }
}  // namespace

TEST_CASE("CrossoverFinder: four clusters of equal weighted energy with gaps between: the three crossovers fall in the gaps") {
    // 90-180 Hz, 500-1000 Hz, 1.7-3.4 kHz, 5-9.5 kHz; the gaps: 180-500, 1000-1700, 3400-5000 Hz
    const auto x = clusters({{90, 180, 0.22}, {500, 1000, 0.28}, {1700, 3400, 0.28}, {5000, 9500, 0.22}}, 12.0);
    sw::CrossoverFinder f; f.prepare(kFs); f.start();
    feed(f, x);
    CHECK(!f.listening()); REQUIRE(f.done());
    const auto r = f.result();
    INFO("crossovers " << r.hz[0] << " " << r.hz[1] << " " << r.hz[2] << " Hz");
    CHECK(r.hz[0] >= 180.0); CHECK(r.hz[0] <= 500.0);
    CHECK(r.hz[1] >= 1000.0); CHECK(r.hz[1] <= 1700.0);
    CHECK(r.hz[2] >= 3400.0); CHECK(r.hz[2] <= 5000.0);
    CHECK(r.hz[1] >= 2.0 * r.hz[0] - 1e-9); CHECK(r.hz[2] >= 2.0 * r.hz[1] - 1e-9);
}

TEST_CASE("CrossoverFinder: it needs 10 s of playing (silence does not count), the same result whatever the block size") {
    const auto x = clusters({{90, 180, 0.22}, {500, 1000, 0.28}, {1700, 3400, 0.28}, {5000, 9500, 0.22}}, 12.0, 7);
    sw::CrossoverFinder a, b; a.prepare(kFs); b.prepare(kFs); a.start(); b.start();
    // 6 s of the program, 30 s of silence, then the rest: 6 s of playing are not enough
    std::vector<float> y(x.begin(), x.begin() + static_cast<long>(6 * kFs)); y.resize(static_cast<size_t>(36 * kFs), 0.0f);
    feed(a, y); CHECK(a.listening()); CHECK(!a.done()); CHECK(a.progress() == doctest::Approx(0.6).epsilon(0.03));
    feed(a, std::vector<float>(x.begin() + static_cast<long>(6 * kFs), x.end()));
    CHECK(a.done()); CHECK(!a.listening());
    std::vector<float> z = y; z.insert(z.end(), x.begin() + static_cast<long>(6 * kFs), x.end());
    feed(b, z, 37);
    REQUIRE(b.done());
    for (int i = 0; i < 3; ++i) CHECK(a.result().hz[i] == b.result().hz[i]);
}

TEST_CASE("CrossoverFinder: the ranges hold for any program (an octave apart, inside 20 Hz .. 20 kHz); cancel and the time limit leave nothing") {
    for (double hz : {60.0, 100.0, 1000.0, 15000.0, 19000.0}) {   // one tone: the three cuts would sit on it
        sw::CrossoverFinder f; f.prepare(kFs); f.start();
        feed(f, sine(-20.0, 11.0, hz));
        REQUIRE(f.done());
        const auto r = f.result(); INFO("tone " << hz << " Hz: " << r.hz[0] << " " << r.hz[1] << " " << r.hz[2]);
        CHECK(r.hz[0] >= 20.0); CHECK(r.hz[2] <= 20000.0 + 1e-6); CHECK(r.hz[1] >= 2.0 * r.hz[0] - 1e-6); CHECK(r.hz[2] >= 2.0 * r.hz[1] - 1e-6);
    }
    {   // a flat spectrum has no valleys: the cuts stay where the weighted energy is in four equal parts (white noise: the ripple of the average does not move them), whatever the noise
        sw::CrossoverFinder a, b; a.prepare(kFs); b.prepare(kFs); a.start(); b.start(); feed(a, noise(-20.0, 12.0, 11)); feed(b, noise(-20.0, 12.0, 12));
        REQUIRE(a.done()); REQUIRE(b.done());
        for (int i = 0; i < 3; ++i) { INFO("cut " << i << ": " << a.result().hz[i] << " vs " << b.result().hz[i]); CHECK(a.result().hz[i] == doctest::Approx(b.result().hz[i]).epsilon(0.03)); }
    }
    { sw::CrossoverFinder f; f.prepare(kFs); f.start(); feed(f, noise(-20.0, 4.0)); f.cancel(); CHECK(!f.listening()); CHECK(!f.done()); }
    { sw::CrossoverFinder f; f.prepare(kFs); f.start(); feed(f, std::vector<float>(static_cast<size_t>(95 * kFs), 0.0f)); CHECK(!f.listening()); CHECK(!f.done()); }   // never heard anything: it gave up after 90 s
}
