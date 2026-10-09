#include "doctest.h"
#include "gt02/gt02.hpp"
#include "sw/zl_convolver.hpp"
#include "sw/tiered_convolver.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::gt02;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// impulse response of the processor (left channel), n samples
std::vector<float> ir(Processor& p, size_t n = 16384) { std::vector<float> x(n, 0.0f); x[0] = 1.0f; return run(p, x); }
double respDb(const std::vector<float>& h, double f) {
    std::complex<double> a; for (size_t i = 0; i < h.size(); ++i) a += static_cast<double>(h[i]) * std::exp(std::complex<double>(0, -2 * kPi * f * static_cast<double>(i) / kFs));
    return 20 * std::log10(std::abs(a) + 1e-12);
}
double band(const std::vector<float>& h, double f0, double f1) {   // mean level in dB over a log-spaced band
    double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.08) { s += respDb(h, f); ++n; } return s / n;
}
Set pure(int cab, int mic, double dist = 4, double off = 0, double room = 0) { return {{Cab, double(cab)}, {Mic, double(mic)}, {MicDistance, dist}, {OffAxis, off}, {Room, room}, {LowCut, 20}}; }
double tailDb(const std::vector<float>& h, size_t from) { double e = 0, t = 0; for (size_t i = 0; i < h.size(); ++i) { const double v = double(h[i]) * h[i]; t += v; if (i >= from) e += v; } return 10 * std::log10(e / t + 1e-20); }
}

TEST_CASE("GT02 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"gt02.cab", "gt02.mic", "gt02.micdistance", "gt02.offaxis", "gt02.room", "gt02.lowcut"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Cab].labels == std::vector<std::string>{"1x12", "2x12", "4x12"}); CHECK(s[Cab].def == 2);
    CHECK(s[Mic].labels == std::vector<std::string>{"Dynamic", "Ribbon", "Condenser"}); CHECK(s[Mic].def == 0);
    CHECK(s[MicDistance].max == 30); CHECK(s[MicDistance].def == 4); CHECK(s[OffAxis].max == 90); CHECK(s[OffAxis].def == 15);
    CHECK(s[Room].def == 10); CHECK(s[LowCut].min == 20); CHECK(s[LowCut].max == 300); CHECK(s[LowCut].def == 80); CHECK(std::string(s[LowCut].minLabel) == "Off");
}
TEST_CASE("GT02 zero-latency convolution equals direct convolution, with and without a crossfade") {
    Gauss g(5); std::vector<double> h1(3000), h2(3000); for (auto& v : h1) v = 0.05 * g.gauss(); for (auto& v : h2) v = 0.05 * g.gauss();
    ZeroLatencyConvolver c; c.prepare(4096, 256, 2, 960); c.setKernel(h1, true);
    std::vector<float> x(20000); for (auto& v : x) v = static_cast<float>(0.3 * g.gauss());
    std::vector<float> y = x, r = x;
    for (size_t off = 0; off < y.size(); off += 200) { const int n = static_cast<int>(std::min<size_t>(200, y.size() - off)); float* p[2] = {y.data() + off, r.data() + off}; c.process(p, 2, n); }
    double worst = 0;
    for (size_t n = 0; n < x.size(); ++n) { double d = 0; for (size_t k = 0; k < h1.size() && k <= n; ++k) d += h1[k] * x[n - k]; worst = std::max(worst, std::abs(d - y[n])); }
    CHECK(worst < 1e-5);
    // switch kernel: after the fade the output equals the new kernel's
    c.setKernel(h2, false);
    std::vector<float> z(20000); for (auto& v : z) v = static_cast<float>(0.3 * g.gauss()); std::vector<float> zz = z, zr = z;
    for (size_t off = 0; off < zz.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, zz.size() - off)); float* p[2] = {zz.data() + off, zr.data() + off}; c.process(p, 2, n); }
    CHECK(!c.fading());
    worst = 0; for (size_t n = 10000; n < z.size(); ++n) { double d = 0; for (size_t k = 0; k < h2.size(); ++k) d += h2[k] * z[n - k]; worst = std::max(worst, std::abs(d - zz[n])); }
    CHECK(worst < 1e-5);
}
TEST_CASE("GT02 (core) tiered convolution of a long kernel equals direct convolution, loaded in steps, with a crossfade") {
    Gauss g(9); std::vector<double> h1(30000), h2(30000); for (auto& v : h1) v = 0.02 * g.gauss(); for (auto& v : h2) v = 0.02 * g.gauss();
    TieredConvolver c; c.prepare(40000, 960); c.setKernel(h1, true);
    std::vector<float> x(40000); for (auto& v : x) v = static_cast<float>(0.3 * g.gauss()); std::vector<float> y = x;
    for (size_t off = 0; off < y.size(); off += 200) c.process(y.data() + off, static_cast<int>(std::min<size_t>(200, y.size() - off)));
    double worst = 0; for (size_t n = 0; n < x.size(); n += 7) { double d = 0; for (size_t k = 0; k < h1.size() && k <= n; ++k) d += h1[k] * x[n - k]; worst = std::max(worst, std::abs(d - y[n])); }
    CHECK(worst < 1e-4);
    c.beginKernel(h2); int steps = 0; while (!c.stepKernel(4)) ++steps; CHECK(steps >= 1); c.commitKernel(false);
    std::vector<float> z(60000); for (auto& v : z) v = static_cast<float>(0.3 * g.gauss()); std::vector<float> zz = z;
    for (size_t off = 0; off < zz.size(); off += 256) c.process(zz.data() + off, static_cast<int>(std::min<size_t>(256, zz.size() - off)));
    CHECK(!c.fading());
    worst = 0; for (size_t n = 35000; n < z.size(); n += 11) { double d = 0; for (size_t k = 0; k < h2.size() && k <= n; ++k) d += h2[k] * z[n - k]; worst = std::max(worst, std::abs(d - zz[n])); }
    CHECK(worst < 1e-4);
}
TEST_CASE("GT02 no delay: the impulse response starts at once") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(pure(2, 0)); const auto h = ir(p);
    double head = 0, all = 0; for (size_t i = 0; i < h.size(); ++i) { const double v = double(h[i]) * h[i]; if (i < 64) head += v; if (i < 1024) all += v; }
    CHECK(head > 0.5 * all);
}
TEST_CASE("GT02 the cabinets differ in size: lows up, highs down") {
    auto a = make(pure(0, 0)); auto b = make(pure(2, 0)); const auto h1 = ir(a), h4 = ir(b);
    const double lows1 = band(h1, 90, 180) - band(h1, 500, 1500), lows4 = band(h4, 90, 180) - band(h4, 500, 1500);
    CHECK(lows4 > lows1 + 3.0);
    const double hi1 = band(h1, 5000, 8000) - band(h1, 500, 1500), hi4 = band(h4, 5000, 8000) - band(h4, 500, 1500);
    CHECK(hi4 < hi1 - 2.0);
    CHECK(band(h4, 20, 40) < band(h4, 500, 1500) - 15.0);   // a speaker does not play 20-40 Hz
}
TEST_CASE("GT02 the microphones differ: Dynamic presence, Ribbon dark and close, Condenser open") {
    auto d = make(pure(2, 0)); auto r = make(pure(2, 1)); auto c = make(pure(2, 2));
    const auto hd = ir(d), hr = ir(r), hc = ir(c);
    const auto rel = [](const std::vector<float>& h, double f0, double f1) { return band(h, f0, f1) - band(h, 500, 1500); };
    CHECK(rel(hc, 10000, 14000) > rel(hd, 10000, 14000) + 6.0);
    CHECK(rel(hr, 8000, 12000) < rel(hd, 8000, 12000) - 4.0);
    CHECK(rel(hr, 90, 180) > rel(hd, 90, 180) + 2.0);
    CHECK(rel(hd, 3500, 5500) > rel(hc, 3500, 5500) + 2.0);
}
TEST_CASE("GT02 closer microphone, more bass (proximity); off axis, less top") {
    auto n = make(pure(2, 0, 0)); auto f = make(pure(2, 0, 30)); const auto hn = ir(n), hf = ir(f);
    CHECK(band(hn, 120, 240) - band(hn, 500, 1500) > band(hf, 120, 240) - band(hf, 500, 1500) + 5.0);
    auto on = make(pure(2, 0, 4, 0)); auto off = make(pure(2, 0, 4, 90)); const auto ho = ir(on), hx = ir(off);
    CHECK(band(hx, 5000, 8000) - band(hx, 500, 1500) < band(ho, 5000, 8000) - band(ho, 500, 1500) - 8.0);
    NEAR(band(hx, 500, 1500), band(ho, 500, 1500), 1.5);
}
TEST_CASE("GT02 Room adds a tail; without it there is none") {
    auto a = make(pure(2, 0, 4, 15, 0)); auto b = make(pure(2, 0, 4, 15, 100)); const auto h0 = ir(a), h1 = ir(b);
    CHECK(tailDb(h0, 1200) < -40.0);
    CHECK(tailDb(h1, 1200) > tailDb(h0, 1200) + 20.0);
    CHECK(tailDb(h1, 1200) < -3.0);
}
TEST_CASE("GT02 Low cut: Off is the cabinet alone; the cut is a 2nd-order high-pass") {
    Set o = pure(2, 0); Set c = pure(2, 0); c[5].second = 150;
    auto po = make(o); auto pc = make(c); const auto ho = ir(po), hc = ir(pc);
    CHECK(respDb(ho, 100) - respDb(hc, 100) > 3.0);
    NEAR(respDb(ho, 2000) - respDb(hc, 2000), 0.0, 0.3);
    CHECK(respDb(ho, 50) - respDb(hc, 50) > 12.0);
}
TEST_CASE("GT02 all cabinets and microphones have about the same level in the middle") {
    for (int cab = 0; cab < 3; ++cab) for (int mic = 0; mic < 3; ++mic) { auto p = make(pure(cab, mic, 4, 15, 10)); NEAR(band(ir(p), 200, 4000), 0.0, 1.5); }
}
TEST_CASE("GT02 changing a setting crossfades to the new IR; silence stays silence; channels are independent") {
    auto p = make(pure(0, 0)); run(p, noise(-20, 0.5)); p.setParam(Cab, 2); run(p, noise(-20, 0.5)); run(p, std::vector<float>(20000, 0.0f));   // let the old input leave the convolver
    auto fresh = make(pure(2, 0)); const auto a = ir(p), b = ir(fresh);
    double worst = 0, mx = 0; for (size_t i = 0; i < a.size(); ++i) { worst = std::max(worst, double(std::abs(a[i] - b[i]))); mx = std::max(mx, double(std::abs(b[i]))); }
    CHECK(worst < 1e-3 * mx + 1e-6);
    auto q = make(pure(2, 0)); std::vector<float> z(24000, 0.0f); for (float v : run(q, z)) CHECK(v == 0.0f);
    auto r = make(pure(2, 0)); const auto l = noise(-20, 0.5); std::vector<float> sil(l.size(), 0.0f); const auto o = run2(r, l, sil); for (float v : o.second) CHECK(v == 0.0f);
}

TEST_CASE("GT02 the impulse response of the processor is the designed IR, tap for tap, at 48 and 192 kHz (the room tail included)") {
    for (double fs : {48000.0, 192000.0}) {
        Processor p; for (auto& s : pure(2, 0, 4, 15, 30)) p.setParam(s.first, s.second);
        p.prepare(fs, 256); p.snapToTargets();
        int len = 8192; while (len < 8192.0 * fs / 48000.0) len *= 2;
        const auto h = designIr(len, fs, 2, 0, 4, 15, 30);
        std::vector<float> x(static_cast<size_t>(len) + 300, 0.0f); x[100] = 1.0f;
        const auto y = run(p, x);
        double peak = 0, worst = 0; for (double v : h) peak = std::max(peak, std::abs(v));
        for (size_t k = 0; k < h.size(); ++k) worst = std::max(worst, std::abs(static_cast<double>(y[k + 100]) - h[k]));
        INFO("fs " << fs); CHECK(peak > 0.0); CHECK(worst < 2e-5 * std::max(1.0, peak));
        for (size_t k = 0; k < 100; ++k) CHECK(y[k] == 0.0f);   // nothing before the impulse
    }
}
