#include "doctest.h"
#include "mt02/mt02.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::mt02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
void feed(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
int bin(const Processor& p, double hz) { return static_cast<int>(std::lround(hz / p.binHz(1))); }
}

TEST_CASE("MT02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"mt02.fft", "mt02.speed", "mt02.range", "mt02.slope", "mt02.smooth", "mt02.display"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[FftSize].labels == std::vector<std::string>{"4k", "8k", "16k", "32k"}); CHECK(s[FftSize].def == 8192);
    CHECK(s[Speed].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Speed].def == 1);
    CHECK(s[Range].min == -120); CHECK(s[Range].max == -60); CHECK(s[Range].def == -90); CHECK(s[Slope].min == 0); CHECK(s[Slope].max == 6); CHECK(s[Slope].def == 4.5);
    CHECK(s[Smoothing].labels == std::vector<std::string>{"Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct"}); CHECK(s[Smoothing].def == 3);
    CHECK(s[Display].labels == std::vector<std::string>{"Peak", "Average", "Hold"}); CHECK(s[Display].def == 1);
}
TEST_CASE("MT02 the signal passes unchanged; no delay") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l = noise(-18, 1.0, 3), r = noise(-18, 1.0, 4); const auto l0 = l, r0 = r;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == l0[i]); CHECK(r[i] == r0[i]); }
}
TEST_CASE("MT02 a sine reads its peak level at its frequency (Smoothing Off, Slope 0)") {
    for (int fft : {4096, 8192, 32768}) {
        auto p = make({{FftSize, double(fft)}, {Smoothing, 0}, {Slope, 0}}); const double f = p.binHz(1) * std::round(1000.0 / p.binHz(1));   // on a bin
        auto x = sine(-9.0, 4.0, f); for (auto& v : x) v *= 1.41421356f;   // peak amplitude = 10^(-9/20) x ... the rms scale of tu::sine -> a peak of -6 dBFS
        feed(p, x); std::vector<float> db; p.spectrumDb(db);
        NEAR(db[static_cast<size_t>(bin(p, f))], 20 * std::log10(std::pow(10.0, -9.0 / 20) * 1.41421356 * 1.41421356), 0.6);
        CHECK(db[static_cast<size_t>(bin(p, f * 2))] < db[static_cast<size_t>(bin(p, f))] - 60.0);
    }
}
TEST_CASE("MT02 Slope tilts the display around 1 kHz; Range is the floor") {
    auto p = make({{Smoothing, 3}, {Slope, 0}}); feed(p, noise(-20, 6.0, 3)); std::vector<float> flat, tilt; p.spectrumDb(flat);
    NEAR(flat[static_cast<size_t>(bin(p, 500))], flat[static_cast<size_t>(bin(p, 4000))], 2.0);                 // white noise: flat (smoothed)
    p.setParam(Slope, 3.0); p.spectrumDb(tilt);
    NEAR(tilt[static_cast<size_t>(bin(p, 4000))] - flat[static_cast<size_t>(bin(p, 4000))], 3.0 * 2.0, 0.1);      // two octaves up
    NEAR(tilt[static_cast<size_t>(bin(p, 250))] - flat[static_cast<size_t>(bin(p, 250))], -3.0 * 2.0, 0.1);
    p.setParam(Range, -60); p.spectrumDb(tilt); for (float v : tilt) CHECK(v >= -60.0f - 1e-3f);
}
TEST_CASE("MT02 Smoothing evens out the noise; Speed sets how fast the average follows") {
    auto rough = [&](double sm) { auto p = make({{Smoothing, sm}, {Slope, 0}, {Speed, 0}}); feed(p, noise(-20, 6.0, 3)); std::vector<float> db; p.spectrumDb(db); double m = 0, s = 0; int c = 0; for (int k = bin(p, 1000); k < bin(p, 6000); ++k) { m += db[static_cast<size_t>(k)]; ++c; } m /= c; for (int k = bin(p, 1000); k < bin(p, 6000); ++k) s += (db[static_cast<size_t>(k)] - m) * (db[static_cast<size_t>(k)] - m); return std::sqrt(s / c); };
    CHECK(rough(4) < rough(0) * 0.5);
    auto level = [&](double speed) { auto p = make({{Speed, speed}, {Smoothing, 0}, {Slope, 0}}); feed(p, sine(-30, 3.0, 1000)); feed(p, sine(-10, 0.5, 1000)); std::vector<float> db; p.spectrumDb(db); return db[static_cast<size_t>(bin(p, 1000))]; };
    CHECK(level(2) > level(0) + 3.0);   // 0.5 s after a 20 dB step the fast one is up, the slow one still low
}
TEST_CASE("MT02 Peak falls, Hold stays, Average follows; reset clears") {
    auto read = [&](double disp) { auto p = make({{Display, disp}, {Smoothing, 0}, {Slope, 0}, {Speed, 2}}); feed(p, sine(-10, 1.0, 1000)); feed(p, sine(-60, 3.0, 1000)); std::vector<float> db; p.spectrumDb(db); return db[static_cast<size_t>(bin(p, 1000))]; };
    const double peak = read(Peak), avg = read(Average), hold = read(Hold);
    CHECK(hold > -8.5);   // -7 dB peak, up to 1.4 dB less off a bin (Hann scalloping)
     CHECK(avg < -50.0); CHECK(peak < hold - 10.0); CHECK(peak > avg - 1.0);   // Peak falls 20 dB/s: after 3 s it is back at the level
    auto p = make({{Display, Hold}, {Smoothing, 0}, {Slope, 0}}); feed(p, sine(-10, 1.0, 1000)); p.reset(); std::vector<float> db; p.spectrumDb(db); CHECK(db[static_cast<size_t>(bin(p, 1000))] <= -89.9f);
}
TEST_CASE("MT02 compare against a reference") {
    auto p = make({{Smoothing, 0}, {Slope, 0}}); feed(p, noise(-30, 6.0, 3)); p.captureReference(); feed(p, noise(-24, 8.0, 3)); std::vector<float> d; p.compareDb(d);
    double m = 0; int c = 0; for (int k = bin(p, 500); k < bin(p, 8000); ++k) { m += d[static_cast<size_t>(k)]; ++c; } NEAR(m / c, 6.0, 1.0);
    auto q = make(); feed(q, noise(-30, 2.0, 3)); q.compareDb(d); for (float v : d) CHECK(v == 0.0f);
}
TEST_CASE("MT02 FFT size changes the resolution; silence and loud input are finite") {
    auto p = make({{FftSize, 32768}}); CHECK(p.fftSize() == 32768); CHECK(p.binCount() == 16385); p.setParam(FftSize, 4096); CHECK(p.fftSize() == 4096);
    auto q = make(); auto x = noise(6, 2.0, 9); for (auto& v : x) v *= 8.0f; feed(q, x); std::vector<float> db; q.spectrumDb(db); for (float v : db) CHECK(std::isfinite(v));
}
