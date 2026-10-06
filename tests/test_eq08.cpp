#include "doctest.h"
#include "eq08/eq08.hpp"
#include <cmath>
#include <complex>
#include <vector>
using namespace sw;
using namespace sw::eq08;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
std::vector<float> impulse(Processor& p, int n = 16384) {
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r; l[0] = 1.0f; r = l;
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return l;
}
double gainAt(const std::vector<float>& h, double f) { std::complex<double> H; for (size_t n = 0; n < h.size(); ++n) H += static_cast<double>(h[n]) * std::exp(std::complex<double>(0, -2 * kPi * f * n / kFs)); return 20 * std::log10(std::abs(H)); }
int band(int n, int field) { return (n - 1) * kPerBand + field; }
}

TEST_CASE("EQ08 table follows the spec (24 bands, then the globals)") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(kBands == 24);
    CHECK(std::string(s[band(1, On)].id) == "eq08.b1.on"); CHECK(std::string(s[band(24, Q)].id) == "eq08.b24.q");
    CHECK(s[band(1, On)].def == 0); CHECK(s[band(2, Freq)].def == 250); CHECK(s[band(5, Freq)].def == 10000);
    CHECK(s[band(1, Gain)].min == -18); CHECK(s[band(1, Q)].def == doctest::Approx(0.7)); CHECK(s[band(1, Q)].max == 10);
    CHECK(s[band(1, Type)].labels == std::vector<std::string>{"Bell", "Lo shelf", "Hi shelf", "Lo cut", "Hi cut"});
    CHECK(std::string(s[Phase].id) == "eq08.phase"); CHECK(s[Phase].labels == std::vector<std::string>{"Linear", "Minimum", "Mixed"});
    CHECK(s[Guard].def == 1); CHECK(s[Ms].def == 0); CHECK(s[Output].min == -18);
}
TEST_CASE("latency: Linear = half the kernel + one block, Minimum = 0") {
    Processor p; p.prepare(kFs, 256); CHECK(p.latencySamples() == 1024 + 128);
    p.setParam(Phase, 1); CHECK(p.latencySamples() == 0);
}
TEST_CASE("Linear: +6 dB bell at 1 kHz, symmetric impulse around the latency") {
    Processor p; p.setParam(Guard, 0); p.setParam(band(3, On), 1); p.setParam(band(3, Gain), 6); p.prepare(kFs, 256); p.snapToTargets();
    const auto h = impulse(p);
    CHECK(gainAt(h, 1000) == doctest::Approx(6.0).epsilon(0.01));
    const int D = p.latencySamples(); double asym = 0;
    for (int k = 1; k < 1000; ++k) asym = std::max(asym, (double)std::abs(h[static_cast<size_t>(D - k)] - h[static_cast<size_t>(D + k)]));
    CHECK(asym < 1e-5);
}
TEST_CASE("Minimum: same bell, no latency") {
    Processor p; p.setParam(Phase, 1); p.setParam(band(3, On), 1); p.setParam(band(3, Gain), 6); p.prepare(kFs, 256); p.snapToTargets();
    CHECK(gainAt(impulse(p), 1000) == doctest::Approx(6.0).epsilon(0.01));
}
TEST_CASE("Mixed: lows are minimum phase (little energy before the main peak)") {
    auto preRatio = [](double phase) {
        Processor p; p.setParam(Guard, 0); p.setParam(Phase, phase); p.setParam(band(1, On), 1); p.setParam(band(1, Type), 1); p.setParam(band(1, Freq), 80); p.setParam(band(1, Gain), 9);
        p.prepare(kFs, 256); p.snapToTargets();
        const auto h = impulse(p); const int D = p.latencySamples();
        double pre = 0, all = 0; for (int n = 0; n < static_cast<int>(h.size()); ++n) { all += h[static_cast<size_t>(n)] * h[static_cast<size_t>(n)]; if (n < D - 48) pre += h[static_cast<size_t>(n)] * h[static_cast<size_t>(n)]; }
        return pre / all;
    };
    CHECK(preRatio(2) < preRatio(0) * 0.05);
}
TEST_CASE("Pre-ring guard moves a steep low cut to minimum phase") {
    auto pre = [](double guard) {
        Processor p; p.setParam(Guard, guard); p.setParam(band(1, On), 1); p.setParam(band(1, Type), 3); p.setParam(band(1, Freq), 40);
        p.setParam(band(2, On), 1); p.setParam(band(2, Gain), 3);  // a gentle 250 Hz bell stays linear
        p.prepare(kFs, 256); p.snapToTargets();
        const auto h = impulse(p); const int D = p.latencySamples();
        double e = 0; for (int n = 0; n < D - 144; ++n) e += h[static_cast<size_t>(n)] * h[static_cast<size_t>(n)];
        return e;
    };
    CHECK(pre(1) < pre(0) * 0.1);
}
TEST_CASE("automating a band gain crossfades kernels without clicks") {
    Processor p; p.setParam(Guard, 0); p.setParam(band(3, On), 1); p.prepare(kFs, 64); p.snapToTargets();
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.3 * std::sin(2 * kPi * 1000.0 * i / kFs));
    for (int off = 0; off < n; off += 64) { if (off % 4800 == 0) p.setParam(band(3, Gain), (off / 4800) % 2 ? 12 : -12); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, 64); }
    double mx = 0; for (int i = 4000; i < n; ++i) mx = std::max(mx, (double)std::abs(l[i] - l[i - 1]));
    CHECK(mx < 2 * kPi * 1000.0 / kFs * 0.3 * 4.0 * 1.15);  // never faster than the loudest steady sine (+12 dB)
}
TEST_CASE("EQ08 Length: 2048 (spec default), 4096, 8192; longer kernels fix the low end") {
    CHECK(std::string(specs()[Length].id) == "eq08.length"); CHECK(specs()[Length].def == 2048);
    Processor p; p.setParam(Length, 8192); p.prepare(kFs, 256); CHECK(p.latencySamples() == 4096 + 128);
    auto err100 = [](double len) {
        Processor q; q.setParam(Guard, 0); q.setParam(Length, len); q.setParam(band(1, On), 1); q.setParam(band(1, Freq), 100); q.setParam(band(1, Gain), 6); q.setParam(band(1, Q), 4);
        q.prepare(kFs, 256); q.snapToTargets();
        return std::abs(gainAt(impulse(q, 32768), 100) - 6.0);
    };
    CHECK(err100(2048) > 1.0);   // the spec length is known to be short here (measured -1.41 dB)
    CHECK(err100(8192) < 0.1);
}
