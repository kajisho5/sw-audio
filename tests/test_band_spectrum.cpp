// sw::BandSpectrum: the long-term average spectrum in 1/6-octave bands (EQ05 Match)
#include "doctest.h"
#include "sw/band_spectrum.hpp"
#include "sw/svf.hpp"
#include "tu.hpp"
using namespace tu;
namespace {
void feed(sw::BandSpectrum& s, const std::vector<float>& x, size_t block = 256) { for (size_t off = 0; off < x.size(); off += block) s.process(x.data() + off, static_cast<int>(std::min(block, x.size() - off))); }
}  // namespace

TEST_CASE("BandSpectrum: white noise is flat per bin; a tone sits in its band; the level follows the input level") {
    sw::BandSpectrum s; s.prepare(kFs); s.start(); feed(s, noise(-20.0, 8.0, 3));
    double db[sw::BandSpectrum::kBands]; REQUIRE(s.levels(db));
    double mean = 0; for (int b = 5; b < 55; ++b) mean += db[b]; mean /= 50;
    for (int b = 5; b < 55; ++b) CHECK(std::abs(db[b] - mean) < 1.0);   // flat power per bin (the long average of white noise)
    sw::BandSpectrum t; t.prepare(kFs); t.start(); feed(t, sine(-20.0, 8.0, 1000.0));
    double dt[sw::BandSpectrum::kBands]; REQUIRE(t.levels(dt));
    int top = 0; for (int b = 1; b < sw::BandSpectrum::kBands; ++b) if (dt[b] > dt[top]) top = b;
    CHECK(sw::BandSpectrum::lowHz(top) <= 1000.0); CHECK(sw::BandSpectrum::lowHz(top + 1) >= 1000.0);
    sw::BandSpectrum q; q.prepare(kFs); q.start(); feed(q, noise(-26.0, 8.0, 3));
    double dq[sw::BandSpectrum::kBands]; REQUIRE(q.levels(dq));
    for (int b = 5; b < 55; ++b) CHECK(db[b] - dq[b] == doctest::Approx(6.0).epsilon(0.1));
}

TEST_CASE("BandSpectrum: a slope is seen (pink-ish: 3 dB per octave in power per bin ... here a 6 dB / octave tilt), silence does not count, the same result whatever the block size") {
    auto x = noise(-20.0, 8.0, 5); double lp = 0; for (auto& v : x) { lp += 0.2 * (v - lp); v = static_cast<float>(lp); }   // a one-pole low-pass: falls 6 dB per octave above ~1.5 kHz
    sw::BandSpectrum s; s.prepare(kFs); s.start(); feed(s, x);
    double db[sw::BandSpectrum::kBands]; REQUIRE(s.levels(db));
    CHECK(db[10] - db[50] == doctest::Approx(12.9).epsilon(0.12));   // a one-pole low-pass (fc ~ 1.5 kHz): about -12.9 dB at 6.4 kHz against the lows
    sw::BandSpectrum z; z.prepare(kFs); z.start(); feed(z, std::vector<float>(static_cast<size_t>(5 * kFs), 0.0f)); double d0[sw::BandSpectrum::kBands]; CHECK(!z.levels(d0)); CHECK(z.playingSeconds() == 0.0);
    sw::BandSpectrum a, b; a.prepare(kFs); b.prepare(kFs); a.start(); b.start(); feed(a, x, 256); feed(b, x, 37);
    double da[sw::BandSpectrum::kBands], dbb[sw::BandSpectrum::kBands]; a.levels(da); b.levels(dbb);
    for (int i = 0; i < sw::BandSpectrum::kBands; ++i) CHECK(da[i] == dbb[i]);
    CHECK(a.playingSeconds() == doctest::Approx(8.0).epsilon(0.02));
}

TEST_CASE("BandSpectrum: with a limit it stops on the same frame whatever the block size") {
    const auto x = noise(-20.0, 20.0, 7);
    sw::BandSpectrum a, b; a.prepare(kFs); b.prepare(kFs); a.setLimitSeconds(10.0); b.setLimitSeconds(10.0); a.start(); b.start();
    CHECK(!a.full());
    feed(a, x, 256); feed(b, x, 37);
    CHECK(a.full()); CHECK(b.full());
    CHECK(a.playingSeconds() >= 10.0); CHECK(a.playingSeconds() < 10.1);
    double da[sw::BandSpectrum::kBands], db[sw::BandSpectrum::kBands]; REQUIRE(a.levels(da)); REQUIRE(b.levels(db));
    for (int i = 0; i < sw::BandSpectrum::kBands; ++i) CHECK(da[i] == db[i]);
    a.start(); CHECK(!a.full()); CHECK(a.playingSeconds() == 0.0);   // start() listens again, with the same limit
}

TEST_CASE("BandSpectrum: samplePoints predicts what the bands read of a steep slope (also where a band is narrower than a bin)") {
    sw::BandSpectrum s; s.prepare(kFs);
    for (int b = 0; b < sw::BandSpectrum::kBands; ++b) { double f[sw::BandSpectrum::kMaxPoints], w[sw::BandSpectrum::kMaxPoints]; const int n = s.samplePoints(b, f, w); REQUIRE(n >= 1); double sum = 0; for (int i = 0; i < n; ++i) sum += w[i]; CHECK(sum == doctest::Approx(1.0)); }
    // white noise through three TPT one-pole high-passes at 500 Hz (18 dB per octave; |H|^2 = (u^2 / (1 + u^2))^3 with u = tan(pi f / fs) / tan(pi 500 / fs))
    auto x = noise(-20.0, 14.0, 12);
    sw::OnePole hp[3]; for (auto& h : hp) h.setup(sw::OnePole::Mode::HighPass, 500.0, kFs);
    for (auto& v : x) { double y = v; for (auto& h : hp) y = h.process(y); v = static_cast<float>(y); }
    sw::BandSpectrum m; m.prepare(kFs); m.start(); feed(m, x);
    double lv[sw::BandSpectrum::kBands], flat[sw::BandSpectrum::kBands]; REQUIRE(m.levels(lv));
    sw::BandSpectrum n0; n0.prepare(kFs); n0.start(); feed(n0, noise(-20.0, 14.0, 12)); REQUIRE(n0.levels(flat));
    double centre = 0, points = 0; int cnt = 0;
    for (int b = 8; b < 40; ++b) {
        double f[sw::BandSpectrum::kMaxPoints], w[sw::BandSpectrum::kMaxPoints]; const int n = s.samplePoints(b, f, w);
        auto H2 = [&](double hz) { const double u = std::tan(3.14159265358979323846 * hz / kFs) / std::tan(3.14159265358979323846 * 500.0 / kFs); const double u2 = u * u; return (u2 / (1 + u2)) * (u2 / (1 + u2)) * (u2 / (1 + u2)); };
        double p = 0; for (int i = 0; i < n; ++i) p += w[i] * H2(f[i]);
        const double read = lv[b] - flat[b], viaPoints = 10.0 * std::log10(p), viaCentre = 10.0 * std::log10(H2(sw::BandSpectrum::centerHz(b)));
        centre += std::abs(read - viaCentre); points += std::abs(read - viaPoints); ++cnt;
        CHECK(std::abs(read - viaPoints) < 0.4);
    }
    INFO("mean error: at the centre " << centre / cnt << " dB, with the points " << points / cnt << " dB");
    CHECK(points / cnt < 0.1); CHECK(points < 0.5 * centre);
}
