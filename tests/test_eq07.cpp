#include "doctest.h"
#include "eq07/eq07.hpp"
#include <cmath>
#include <complex>
#include <random>
#include <vector>
using namespace sw;
using namespace sw::eq07;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
int bp(int n, int f) { return (n - 1) * kPerBand + f; }
Processor make(std::vector<std::pair<int, double>> set) {
    Processor p; for (auto& s : set) p.setParam(s.first, s.second);
    p.prepare(kFs, 256); p.snapToTargets(); return p;
}
std::vector<float> run(Processor& p, std::vector<float> l, const std::vector<float>* key = nullptr) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - off));
        float* c[2] = {l.data() + off, r.data() + off};
        if (key) { const float* k[2] = {key->data() + off, key->data() + off}; p.processWithSidechain(c, 2, n, k, 2); } else p.process(c, 2, n);
    }
    return l;
}
std::vector<float> sine(double amp, double f, int n) { std::vector<float> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs)); return x; }
double toneDb(const std::vector<float>& y, double f, double amp) {  // DFT amplitude of the second half, re input amplitude
    const size_t n0 = y.size() / 2, n = y.size() - n0; std::complex<double> a; double w = 0;
    for (size_t i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1)); a += h * static_cast<double>(y[n0 + i]) * std::exp(std::complex<double>(0, -2 * kPi * f * (n0 + i) / kFs)); w += h; }
    return 20 * std::log10(2 * std::abs(a) / w / amp);
}
double bandNoiseDb(const std::vector<float>& y, double f0, double f1) {  // mean power in [f0, f1] over the second half (Goertzel, 20 Hz steps)
    const size_t n0 = y.size() / 2; double sum = 0; int bins = 0;
    for (double f = f0; f <= f1; f += 20.0, ++bins) {
        const double cw = 2 * std::cos(2 * kPi * f / kFs); double s1 = 0, s2 = 0;
        for (size_t i = n0; i < y.size(); ++i) { const double s0 = y[i] + cw * s1 - s2; s2 = s1; s1 = s0; }
        sum += s1 * s1 + s2 * s2 - cw * s1 * s2;
    }
    return 10 * std::log10(sum / bins);
}
}

TEST_CASE("EQ07 table follows the spec (6 bands x 9, then sidechain / spectral)") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(kBands == 6); CHECK(kPerBand == 9);
    CHECK(std::string(s[bp(1, On)].id) == "eq07.b1.on"); CHECK(std::string(s[bp(6, Release)].id) == "eq07.b6.release");
    CHECK(s[bp(1, On)].def == 0);
    const double f[6] = {80, 180, 500, 1500, 4000, 10000};
    for (int b = 1; b <= 6; ++b) CHECK(s[bp(b, Freq)].def == f[b - 1]);
    CHECK(s[bp(1, Type)].labels == std::vector<std::string>{"Bell", "Shelf", "Cut", "Notch"});
    CHECK(s[bp(1, Gain)].min == -24); CHECK(s[bp(1, Q)].max == 20); CHECK(s[bp(1, Thresh)].def == -30); CHECK(s[bp(1, Range)].def == -6);
    CHECK(s[bp(1, Attack)].def == 10); CHECK(s[bp(1, Attack)].curve == Curve::Skew); CHECK(s[bp(1, Release)].def == 120);
    CHECK(std::string(s[Sidechain].id) == "eq07.sc"); CHECK(std::string(s[Spectral].id) == "eq07.spectral"); CHECK(s[Spectral].def == 0);
}
TEST_CASE("latency: 0 with Spectral off, 1024 with it on") {
    Processor p; p.prepare(kFs, 256); CHECK(p.latencySamples() == 0);
    p.setParam(Spectral, 1); CHECK(p.latencySamples() == 1024);
}
TEST_CASE("a quiet tone below the threshold keeps the static gain; a loud one gets the range on top") {
    auto q = make({{bp(4, On), 1}, {bp(4, Gain), 3}, {bp(4, Range), -12}, {bp(4, Thresh), -30}});
    CHECK(toneDb(run(q, sine(0.003, 1500, 48000)), 1500, 0.003) == doctest::Approx(3).epsilon(0.03));
    auto l = make({{bp(4, On), 1}, {bp(4, Gain), 3}, {bp(4, Range), -12}, {bp(4, Thresh), -30}});
    CHECK(toneDb(run(l, sine(0.5, 1500, 48000)), 1500, 0.5) == doctest::Approx(3 - 12).epsilon(0.03));
}
TEST_CASE("positive range lifts the band when it is above the threshold") {
    auto l = make({{bp(4, On), 1}, {bp(4, Range), 6}, {bp(4, Thresh), -40}});
    CHECK(toneDb(run(l, sine(0.3, 1500, 48000)), 1500, 0.3) == doctest::Approx(6).epsilon(0.03));
}
TEST_CASE("the detector only hears its own band") {
    auto p = make({{bp(4, On), 1}, {bp(4, Range), -12}, {bp(4, Thresh), -30}, {bp(4, Q), 4}});
    auto x = sine(0.5, 100, 48000);                     // loud 100 Hz, far from the 1.5 kHz band
    const auto q = sine(0.001, 1500, 48000);
    for (size_t i = 0; i < x.size(); ++i) x[i] += q[i];
    CHECK(std::abs(toneDb(run(p, x), 1500, 0.001)) < 0.5);
}
TEST_CASE("External sidechain drives the band") {
    auto p = make({{Sidechain, 1}, {bp(4, On), 1}, {bp(4, Range), -12}, {bp(4, Thresh), -30}});
    const auto key = sine(0.5, 1500, 48000);
    CHECK(toneDb(run(p, sine(0.003, 1500, 48000), &key), 1500, 0.003) == doctest::Approx(-12).epsilon(0.03));
}
TEST_CASE("Spectral moves only the prominent tone, not the noise around it") {
    std::mt19937 rng(3); std::normal_distribution<double> nd(0, 0.05);
    std::vector<float> x(48000 * 2); const auto tone = sine(0.4, 1500, 48000 * 2);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(nd(rng)) + tone[i];
    auto measure = [&](double spectral) {
        auto p = make({{Spectral, spectral}, {bp(4, On), 1}, {bp(4, Range), -12}, {bp(4, Thresh), -30}, {bp(4, Q), 1}});
        const auto y = run(p, x);
        return std::make_pair(toneDb(y, 1500, 0.4), bandNoiseDb(y, 1700, 2100) - bandNoiseDb(x, 1700, 2100));
    };
    const auto broad = measure(0), spec = measure(1);
    CHECK(spec.first < -8.0);              // the tone is still pulled down
    CHECK(broad.second < -6.0);            // broadband mode pulls the neighbouring noise down too
    CHECK(spec.second > broad.second + 4); // spectral mode leaves it mostly alone
}
TEST_CASE("Spectral with nothing to do is transparent apart from the latency") {
    auto p = make({{Spectral, 1}});
    std::mt19937 rng(4); std::normal_distribution<double> nd(0, 0.2);
    std::vector<float> x(16384); for (auto& v : x) v = static_cast<float>(nd(rng));
    const auto y = run(p, x);
    double err = 0; for (size_t i = 2048; i < x.size(); ++i) err = std::max(err, (double)std::abs(y[i] - x[i - 1024]));
    CHECK(err < 1e-4);
}

namespace {
// a tone whose level steps between two values every 100 ms: `hiShare` of the blocks at `hi` dB peak, the rest at `lo`
std::vector<float> steppedTone(double f, double lo, double hi, double hiShare, double seconds) {
    const int n = static_cast<int>(seconds * kFs), blk = static_cast<int>(0.1 * kFs); std::vector<float> x(static_cast<size_t>(n));
    int k = 0; for (int i = 0; i < n; ++i) { if (i % blk == 0) ++k; const bool high = (k % 10) < static_cast<int>(std::lround(hiShare * 10)); x[static_cast<size_t>(i)] = static_cast<float>(std::pow(10.0, (high ? hi : lo) / 20.0) * std::sin(2 * kPi * f * i / kFs)); }
    return x;
}
void feed(Processor& p, const std::vector<float>& x, size_t a, size_t b) { std::vector<float> l(x.begin() + static_cast<long>(a), x.begin() + static_cast<long>(b)), r = l; for (size_t off = 0; off < l.size(); off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, static_cast<int>(std::min<size_t>(256, l.size() - off))); } }
}
TEST_CASE("EQ07 Auto thresh: thresholds from the level distribution of each dynamic band (80th percentile for a negative Range, 20th for a positive one)") {
    // band 1: 200 Hz, Range -6; band 2: 3 kHz, Range +6; both bells; the signal is the sum of a 200 Hz and a 3 kHz tone, each stepping between -30 and -10 dB
    auto p = make({{bp(1, On), 1}, {bp(1, Freq), 200}, {bp(1, Range), -6}, {bp(1, Thresh), -50}, {bp(2, On), 1}, {bp(2, Freq), 3000}, {bp(2, Range), 6}, {bp(2, Thresh), -5},
                   {bp(3, On), 1}, {bp(3, Freq), 1000}, {bp(3, Range), 0}, {bp(3, Thresh), -33}, {bp(4, On), 0}, {bp(4, Range), -6}, {bp(4, Thresh), -33},
                   {bp(5, On), 1}, {bp(5, Type), 3}, {bp(5, Range), -6}, {bp(5, Thresh), -33}});   // 3 = a cut: no gain, so no dynamics
    auto a = steppedTone(200, -30, -10, 0.3, 8.0), b = steppedTone(3000, -30, -10, 0.7, 8.0); std::vector<float> x(a.size()); for (size_t i = 0; i < x.size(); ++i) x[i] = a[i] + b[i];
    CHECK_FALSE(p.learning()); int id = 0; double v = 0; CHECK(p.takeParamWrite(id, v) == 0);
    feed(p, x, 0, 4800); p.learnThresholds(); CHECK(p.learning()); CHECK(p.learnProgress() < 0.05);
    feed(p, x, 4800, 4800 + 120000); CHECK(p.learning()); CHECK(p.learnProgress() > 0.4); CHECK(p.learnProgress() < 0.6); CHECK(p.takeParamWrite(id, v) == 0);   // nothing is written while listening
    feed(p, x, 4800 + 120000, 4800 + 240000 + 512);   // the rest (5 s in all)
    CHECK_FALSE(p.learning());
    // the writes: begin + value + end for each band that was learnt (1 and 2), in band order
    std::vector<std::pair<int, double>> w; int f; while ((f = p.takeParamWrite(id, v)) != 0) { CHECK(f == 7); w.emplace_back(id, v); }
    REQUIRE(w.size() == 2); CHECK(w[0].first == bp(1, Thresh)); CHECK(w[1].first == bp(2, Thresh));
    CHECK(w[0].second > -18.0); CHECK(w[0].second < -4.0);    // 80th percentile of mostly-quiet band 1: the loud level (-10 dB peak = about -13 RMS)
    CHECK(w[1].second < -22.0); CHECK(w[1].second > -40.0);   // 20th percentile of mostly-loud band 2: the quiet level
    CHECK(p.takeParamWrite(id, v) == 0);
}
TEST_CASE("EQ07 Auto thresh: silence puts the thresholds at the bottom; nothing to learn without a dynamic band; extremes stay finite") {
    auto p = make({{bp(1, On), 1}, {bp(1, Range), -6}, {bp(1, Thresh), -10}});
    p.learnThresholds(); feed(p, std::vector<float>(static_cast<size_t>(5.2 * kFs), 0.0f), 0, static_cast<size_t>(5.2 * kFs)); CHECK_FALSE(p.learning());
    int id = 0; double v = 0; REQUIRE(p.takeParamWrite(id, v) == 7); CHECK(id == bp(1, Thresh)); CHECK(v == -60.0); CHECK(p.takeParamWrite(id, v) == 0);
    auto q = make({}); q.learnThresholds(); feed(q, steppedTone(500, -30, -10, 0.5, 5.5), 0, static_cast<size_t>(5.2 * kFs)); CHECK_FALSE(q.learning()); CHECK(q.takeParamWrite(id, v) == 0);   // no band is on in the default settings? (only if none is dynamic)
}
