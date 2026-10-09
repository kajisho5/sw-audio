#include "doctest.h"
#include "eq02/eq02.hpp"
#include "sw/fir_design.hpp"
#include "sw/svf.hpp"
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>
using namespace sw;
using namespace sw::eq02;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
using cd = std::complex<double>;
int bp(int n, int field) { return (n - 1) * kPerBand + field; }
std::vector<float> impulse(Processor& p, int n = 8192, int side = 0) {
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f); l[0] = 1.0f; r[0] = side ? -1.0f : 1.0f;
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    return l;
}
cd resp(const std::vector<float>& h, double f) { cd H; for (size_t n = 0; n < h.size(); ++n) H += static_cast<double>(h[n]) * std::exp(cd(0, -2 * kPi * f * n / kFs)); return H; }
double db(const std::vector<float>& h, double f) { return 20 * std::log10(std::abs(resp(h, f))); }
// a processor with every default band switched off, then the given settings
Processor make(std::vector<std::pair<int, double>> set) {
    Processor p; for (int b = 1; b <= kBands; ++b) p.setParam(bp(b, On), 0);
    for (auto& s : set) p.setParam(s.first, s.second);
    p.prepare(kFs, 256); p.snapToTargets(); return p;
}
double toneDb(Processor& p, double f, double amp) {  // steady sine level change
    const int n = 48000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / kFs));
    for (int off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, std::min(256, n - off)); }
    double s = 0; for (int i = n / 2; i < n; ++i) s += l[i] * l[i];
    return 10 * std::log10(s / (n / 2)) - 20 * std::log10(amp / std::sqrt(2.0));
}
}

TEST_CASE("EQ02 table follows the spec (24 bands x 9, then phase / mid-side / output)") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(kBands == 24); CHECK(kPerBand == 9);
    CHECK(std::string(s[bp(1, On)].id) == "eq02.b1.on"); CHECK(std::string(s[bp(24, DynThresh)].id) == "eq02.b24.dynthresh");
    for (int b = 1; b <= 5; ++b) CHECK(s[bp(b, On)].def == 1);
    CHECK(s[bp(6, On)].def == 0);
    CHECK(s[bp(1, Freq)].def == 100); CHECK(s[bp(3, Freq)].def == 1600); CHECK(s[bp(5, Freq)].def == 12000);
    CHECK(s[bp(1, Gain)].min == -30); CHECK(s[bp(1, Gain)].max == 30); CHECK(s[bp(1, Q)].min == doctest::Approx(0.1)); CHECK(s[bp(1, Q)].max == 40);
    CHECK(s[bp(1, Slope)].steps == std::vector<double>{6, 12, 18, 24, 36, 48, 72, 96}); CHECK(s[bp(1, Slope)].def == 24);
    CHECK(s[bp(1, Type)].labels == std::vector<std::string>{"Bell", "Lo shelf", "Hi shelf", "Lo cut", "Hi cut", "Notch"});
    CHECK(s[bp(1, Place)].labels == std::vector<std::string>{"Stereo", "Mid", "Side"});
    CHECK(s[bp(1, DynRange)].min == -24); CHECK(s[bp(1, DynRange)].def == 0); CHECK(s[bp(1, DynThresh)].def == -30);
    CHECK(s[PhaseMode].labels == std::vector<std::string>{"Zero latency", "Natural", "Linear"}); CHECK(s[Ms].def == 0); CHECK(s[Output].max == 24);
}
TEST_CASE("defaults are flat (5 bands on at 0 dB)") {
    Processor p; p.prepare(kFs, 256); p.snapToTargets();
    const auto h = impulse(p); for (double f : {100.0, 1600.0, 12000.0}) CHECK(std::abs(db(h, f)) < 0.01);
}
TEST_CASE("Zero latency: bell, no latency") {
    auto p = make({{bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, Gain), 6}});
    CHECK(p.latencySamples() == 0); CHECK(db(impulse(p), 1000) == doctest::Approx(6).epsilon(0.01));
}
TEST_CASE("Zero latency corrects the bandwidth cramping near Nyquist (bell at 15 kHz)") {
    auto p = make({{bp(1, On), 1}, {bp(1, Freq), 15000}, {bp(1, Gain), 12}, {bp(1, Q), 1}});
    Svf plain; plain.setup(Svf::Mode::Bell, 15000, kFs, 1, 12);
    std::vector<float> hp(8192, 0.0f); for (size_t i = 0; i < hp.size(); ++i) hp[i] = static_cast<float>(plain.process(i == 0 ? 1.0 : 0.0));
    const BandShape ana{BandShape::Bell, 15000, 12, 1, 12};
    const double f = 15000 / 1.4, want = 20 * std::log10(ana.magnitude(f));
    const double errCorr = std::abs(db(impulse(p), f) - want), errPlain = std::abs(db(hp, f) - want);
    CHECK(errCorr < errPlain * 0.5);
    CHECK(errCorr < 0.6);
}
TEST_CASE("cut slopes: 6 dB/oct and 48 dB/oct low cuts") {
    auto p6 = make({{bp(1, On), 1}, {bp(1, Type), 3}, {bp(1, Freq), 100}, {bp(1, Slope), 6}});
    CHECK(db(impulse(p6, 16384), 50) == doctest::Approx(-6.99).epsilon(0.02));
    auto p48 = make({{bp(1, On), 1}, {bp(1, Type), 3}, {bp(1, Freq), 100}, {bp(1, Slope), 48}});
    CHECK(db(impulse(p48, 16384), 50) == doctest::Approx(-48.2).epsilon(0.02));
}
TEST_CASE("notch") {
    auto p = make({{bp(1, On), 1}, {bp(1, Type), 5}, {bp(1, Freq), 1000}, {bp(1, Q), 10}});
    const auto h = impulse(p, 16384);
    CHECK(db(h, 1000) < -40); CHECK(db(h, 2000) > -0.5);
}
TEST_CASE("dynamic band: quiet tone untouched, loud tone pulled down by the range") {
    auto quiet = make({{bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, DynRange), -12}, {bp(1, DynThresh), -30}});
    CHECK(std::abs(toneDb(quiet, 1000, 0.003)) < 0.3);
    auto loud = make({{bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, DynRange), -12}, {bp(1, DynThresh), -30}});
    CHECK(toneDb(loud, 1000, 0.5) == doctest::Approx(-12).epsilon(0.03));
}
TEST_CASE("mid/side placement: a Side band leaves a mono signal alone") {
    auto p = make({{Ms, 1}, {bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, Gain), 6}, {bp(1, Place), 2}});
    CHECK(std::abs(db(impulse(p), 1000)) < 0.01);
    auto q = make({{Ms, 1}, {bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, Gain), 6}, {bp(1, Place), 2}});
    CHECK(db(impulse(q, 8192, 1), 1000) == doctest::Approx(6).epsilon(0.01));
}
TEST_CASE("Linear: shared FIR engine, symmetric impulse") {
    auto p = make({{PhaseMode, 2}, {bp(1, On), 1}, {bp(1, Freq), 1000}, {bp(1, Gain), 6}});
    CHECK(p.latencySamples() == 1024 + 128);
    const auto h = impulse(p, 16384);
    CHECK(db(h, 1000) == doctest::Approx(6).epsilon(0.01));
    const int D = p.latencySamples(); double asym = 0;
    for (int k = 1; k < 800; ++k) asym = std::max(asym, (double)std::abs(h[static_cast<size_t>(D - k)] - h[static_cast<size_t>(D + k)]));
    CHECK(asym < 1e-5);
}
TEST_CASE("Natural: same magnitude as Zero latency, phase closer to the analog prototype") {
    auto z = make({{bp(1, On), 1}, {bp(1, Freq), 12000}, {bp(1, Gain), 12}, {bp(1, Q), 2}});
    auto nt = make({{PhaseMode, 1}, {bp(1, On), 1}, {bp(1, Freq), 12000}, {bp(1, Gain), 12}, {bp(1, Q), 2}});
    const int D = nt.latencySamples();
    CHECK(D > 0); CHECK(D <= 64);
    const auto hz = impulse(z), hn = impulse(nt);
    const BandShape ana{BandShape::Bell, 12000, 12, 2, 12};
    double ez = 0, en = 0;
    for (double f : {8000.0, 10000.0, 14000.0, 17000.0}) {
        const cd a = ana.response(f), rz = resp(hz, f), rn = resp(hn, f) * std::exp(cd(0, 2 * kPi * f * D / kFs));
        ez = std::max(ez, std::abs(std::arg(rz / a))); en = std::max(en, std::abs(std::arg(rn / a)));
        CHECK(std::abs(20 * std::log10(std::abs(rn)) - 20 * std::log10(std::abs(rz))) < 0.2);  // spec: magnitude = Zero latency
    }
    CHECK(en < ez * 0.5);
}
TEST_CASE("EQ02 Length follows EQ08 (Linear mode only)") {
    CHECK(std::string(specs()[Length].id) == "eq02.length"); CHECK(specs()[Length].def == 2048);
    Processor p; p.setParam(PhaseMode, 2); p.setParam(Length, 4096); p.prepare(kFs, 256); CHECK(p.latencySamples() == 2048 + 128);
}

TEST_CASE("EQ02 Assist: the resonances of the input are listed while it is on; the sound is not changed by listening") {
    auto mk = [] { Processor p; p.setParam(bp(1, On), 1); p.setParam(bp(1, Gain), 3); p.setParam(bp(1, Freq), 800); p.prepare(kFs, 256); p.snapToTargets(); return p; };
    const size_t n = static_cast<size_t>(10 * kFs); std::vector<float> x(n); uint32_t s = 12345;
    for (size_t i = 0; i < n; ++i) { s = s * 1664525u + 1013904223u; const double noise = (static_cast<double>(s >> 8) / 16777216.0 - 0.5) * 0.06; x[i] = static_cast<float>(noise + 0.05 * std::sin(2 * kPi * 2500.0 * static_cast<double>(i) / kFs)); }
    auto runIt = [&](Processor& p) { std::vector<float> l = x, r = x; for (size_t off = 0; off < n; off += 256) { float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, static_cast<int>(std::min<size_t>(256, n - off))); } return l; };
    ResonanceFinder::Mark m[ResonanceFinder::kMarks];
    Processor off = mk(); const auto yOff = runIt(off); CHECK_FALSE(off.assist()); CHECK(off.resonances(m) == 0);
    Processor on = mk(); on.setAssist(true); CHECK(on.assist()); const auto yOn = runIt(on);
    const int k = on.resonances(m); REQUIRE(k >= 1); CHECK(m[0].hz == doctest::Approx(2500).epsilon(0.03)); CHECK(m[0].db > 6.0);
    for (size_t i = 0; i < n; i += 97) REQUIRE(yOn[i] == yOff[i]);   // listening changes nothing
    on.setAssist(false); CHECK(on.resonances(m) == 0); on.setAssist(true); CHECK(on.resonances(m) == 0);   // switching it on starts afresh
}
