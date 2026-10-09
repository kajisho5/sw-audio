#include "doctest.h"
#include "sw/shell.hpp"
#include "sw/loudness.hpp"
#include "sw/unit.hpp"
#include "eq05/eq05.hpp"
#include <cmath>
#include <random>
#include <vector>
using namespace sw;

namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
// simple real cores used to exercise the shell
struct GainCore {
    float g = 1.0f;
    void prepare(double, int) {}
    void process(float** ch, int nch, int n) { for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] *= g; }
    int latencySamples() const { return 0; }
};
struct DelayCore {
    int d = 64; std::vector<std::vector<float>> buf; std::vector<int> pos;
    void prepare(double, int) { buf.assign(2, std::vector<float>(static_cast<size_t>(d), 0.0f)); pos.assign(2, 0); }
    void process(float** ch, int nch, int n) {
        for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) {
            auto& b = buf[static_cast<size_t>(c)]; int& p = pos[static_cast<size_t>(c)];
            const float y = b[static_cast<size_t>(p)]; b[static_cast<size_t>(p)] = ch[c][i]; ch[c][i] = y; p = (p + 1) % d;
        }
    }
    int latencySamples() const { return d; }
};
template <class S> std::vector<float> run(S& sh, const std::vector<float>& in, int block = 256) {
    std::vector<float> l = in, r = in;
    for (size_t off = 0; off < l.size(); off += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), l.size() - off));
        float* c[2] = {l.data() + off, r.data() + off}; sh.process(c, 2, n);
    }
    return l;
}
std::vector<float> noise(size_t n, double level, unsigned seed = 5) {
    std::mt19937 rng(seed); std::normal_distribution<double> d(0, level); std::vector<float> x(n);
    for (auto& v : x) v = static_cast<float>(d(rng));
    return x;
}
double shortTermOfTail(const std::vector<float>& x) {
    LoudnessMeter m; m.setup(kFs, 2);
    const size_t start = x.size() - 3 * 48000;
    const float* c[2] = {x.data() + start, x.data() + start};
    m.process(c, 2, 3 * 48000);
    return m.shortTerm();
}
}

TEST_CASE("In off passes the input through bit-exactly (EQ05 core, heavy settings)") {
    Shell<eq05::Processor> sh; sh.prepare(kFs, 256, 2);
    sh.core().setParam(eq05::HfGain, 12.0); sh.core().setParam(eq05::Drive, 10.0);
    sh.setIn(false); sh.snap();
    const auto in = noise(4096, 0.3);
    CHECK(run(sh, in) == in);
}
TEST_CASE("In off keeps the reported latency: output is the input delayed by the core latency") {
    Shell<DelayCore> sh; sh.prepare(kFs, 256, 2); sh.setIn(false); sh.snap();
    const auto in = noise(4096, 0.3);
    const auto out = run(sh, in);
    for (size_t i = 64; i < in.size(); ++i) REQUIRE(out[i] == in[i - 64]);
}
TEST_CASE("Output +6 dB doubles the level") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.setOutputDb(6.0); sh.snap();
    const auto out = run(sh, std::vector<float>(1024, 0.25f));
    CHECK(out.back() == doctest::Approx(0.25 * std::pow(10.0, 6.0 / 20.0)).epsilon(1e-5));
}
TEST_CASE("Auto gain off leaves the core's level change") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 2.0f; sh.snap();
    CHECK(run(sh, std::vector<float>(1024, 0.1f)).back() == doctest::Approx(0.2f));
}
TEST_CASE("Auto gain matches the processed loudness to the input within 0.3 dB") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 2.0f; sh.setAutoGain(true); sh.snap();
    const auto in = noise(48000 * 14, 0.1);
    const auto out = run(sh, in);
    CHECK(std::abs(shortTermOfTail(out) - shortTermOfTail(in)) < 0.3);
    CHECK(sh.autoGainDb() == doctest::Approx(-6.02).epsilon(0.03));
}
TEST_CASE("Auto gain correction is limited to -18 dB") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 100.0f; sh.setAutoGain(true); sh.snap();
    run(sh, noise(48000 * 14, 0.001));
    CHECK(sh.autoGainDb() == doctest::Approx(-18.0).epsilon(0.01));
}
TEST_CASE("Auto gain holds its correction through silence") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 2.0f; sh.setAutoGain(true); sh.snap();
    run(sh, noise(48000 * 14, 0.1));
    const double before = sh.autoGainDb();
    run(sh, std::vector<float>(48000 * 6, 0.0f));
    CHECK(sh.autoGainDb() == doctest::Approx(before).epsilon(0.001));
}
TEST_CASE("Delta outputs exactly zero when the core changes nothing") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.setDelta(true); sh.snap();
    for (float v : run(sh, noise(8192, 0.3))) REQUIRE(v == 0.0f);
}
TEST_CASE("Delta is latency aligned: a pure delay core gives zero difference") {
    Shell<DelayCore> sh; sh.prepare(kFs, 256, 2); sh.setDelta(true); sh.snap();
    const auto out = run(sh, noise(8192, 0.3));
    for (size_t i = 64; i < out.size(); ++i) REQUIRE(out[i] == 0.0f);
}
TEST_CASE("Delta of a +6 dB core with auto gain off is the input itself") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 2.0f; sh.setDelta(true); sh.snap();
    const auto in = noise(4096, 0.2);
    const auto out = run(sh, in);
    for (size_t i = 0; i < in.size(); ++i) REQUIRE(out[i] == doctest::Approx(in[i]).epsilon(1e-6));
}
TEST_CASE("switching In off and on does not click") {
    Shell<GainCore> sh; sh.prepare(kFs, 64, 2); sh.core().g = 0.5f; sh.snap();
    const int n = 24000; std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.8 * std::sin(2 * kPi * 100.0 * i / kFs));
    for (int off = 0; off < n; off += 64) {
        if (off == 9600) sh.setIn(false);
        float* c[2] = {l.data() + off, r.data() + off}; sh.process(c, 2, 64);
    }
    double tr = 0, st = 0;
    for (int i = 9601; i < 12000; ++i) tr = std::max(tr, (double)std::abs(l[i] - l[i - 1]));
    for (int i = 18001; i < n; ++i) st = std::max(st, (double)std::abs(l[i] - l[i - 1]));
    CHECK(tr <= 1.05 * st);
}

TEST_CASE("Mix blends the processed signal with the aligned dry signal (after Auto gain, before Output)") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 3.0f; sh.setMix(0.25); sh.setOutputDb(6.0); sh.snap();
    const auto out = run(sh, std::vector<float>(1024, 0.1f));
    const double expect = (0.1 + 0.25 * (0.3 - 0.1)) * std::pow(10.0, 6.0 / 20.0);
    CHECK(out.back() == doctest::Approx(expect).epsilon(1e-5));
}
TEST_CASE("Mix 0 with Delta gives silence (nothing changed)") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.core().g = 3.0f; sh.setMix(0.0); sh.setDelta(true); sh.snap();
    for (float v : run(sh, noise(2048, 0.3))) REQUIRE(v == 0.0f);
}

namespace {
struct ScCore {  // copies the sidechain into the output so the test can see what arrived
    bool gotSc = false;
    void prepare(double, int) {}
    void process(float** ch, int nch, int n) { for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] = 0.0f; gotSc = false; }
    void processWithSidechain(float** ch, int nch, int n, const float* const* sc, int scCh) {
        gotSc = sc != nullptr && scCh > 0;
        for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] = gotSc ? sc[std::min(c, scCh - 1)][i] : 0.0f;
    }
    int latencySamples() const { return 0; }
};
}
TEST_CASE("the shell hands the sidechain to cores that accept one") {
    Shell<ScCore> sh; sh.prepare(kFs, 64, 2); sh.snap();
    std::vector<float> l(64, 0.1f), r(64, 0.1f), sl(64, 0.7f), sr(64, -0.7f);
    float* c[2] = {l.data(), r.data()}; const float* s[2] = {sl.data(), sr.data()};
    sh.process(c, 2, 64, s, 2);
    CHECK(sh.core().gotSc); CHECK(l[10] == doctest::Approx(0.7f)); CHECK(r[10] == doctest::Approx(-0.7f));
}
TEST_CASE("without a sidechain the core runs its normal path") {
    Shell<ScCore> sh; sh.prepare(kFs, 64, 2); sh.snap();
    std::vector<float> l(64, 0.1f), r(64, 0.1f);
    float* c[2] = {l.data(), r.data()};
    sh.process(c, 2, 64);
    CHECK_FALSE(sh.core().gotSc); CHECK(l[10] == 0.0f);
}

// ---- Unit A / B / C: the gain tolerance of the output stage, a different one on the left and on the right channel, on the wet signal only
namespace {
std::pair<std::vector<float>, std::vector<float>> runLR(Shell<GainCore>& sh, const std::vector<float>& in, int block = 256) {
    std::vector<float> l = in, r = in;
    for (size_t off = 0; off < l.size(); off += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), l.size() - off));
        float* c[2] = {l.data() + off, r.data() + off}; sh.process(c, 2, n);
    }
    return {l, r};
}
}
TEST_CASE("Unit A is the reference: the Shell is bit-exact as before") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.setUnit(0); sh.snap();
    const auto in = noise(4096, 0.3);
    const auto [l, r] = runLR(sh, in);
    CHECK(l == in); CHECK(r == in);
}
TEST_CASE("Unit B and C: each channel's wet signal has its own gain, within +-0.3 dB") {
    for (int unit : {1, 2}) {
        Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.setUnit(unit); sh.snap();
        const auto [l, r] = runLR(sh, std::vector<float>(2048, 0.25f));
        const double gl = Unit::gainLin(unit, 0, Unit::kOutputSlot), gr = Unit::gainLin(unit, 1, Unit::kOutputSlot);
        CHECK(l.back() == doctest::Approx(0.25 * gl).epsilon(1e-5)); CHECK(r.back() == doctest::Approx(0.25 * gr).epsilon(1e-5));
        CHECK(l.back() != r.back());
        CHECK(std::abs(20 * std::log10(l.back() / 0.25)) <= 0.3 + 1e-4); CHECK(std::abs(20 * std::log10(r.back() / 0.25)) <= 0.3 + 1e-4);
    }
}
TEST_CASE("Unit changes the wet only: Mix 0 % and In off stay bit-exact") {
    Shell<GainCore> a; a.prepare(kFs, 256, 2); a.setUnit(2); a.setMix(0.0); a.snap();
    const auto in = noise(4096, 0.3);
    const auto [l, r] = runLR(a, in); CHECK(l == in); CHECK(r == in);
    Shell<GainCore> b; b.prepare(kFs, 256, 2); b.setUnit(2); b.setIn(false); b.snap();
    const auto [l2, r2] = runLR(b, in); CHECK(l2 == in); CHECK(r2 == in);
}
TEST_CASE("Unit's tolerance is not taken back by Auto gain (it is applied after the loudness is compared)") {
    Shell<GainCore> sh; sh.prepare(kFs, 256, 2); sh.setUnit(1); sh.setAutoGain(true); sh.snap();
    const auto in = noise(48000 * 14, 0.1);
    const auto [l, r] = runLR(sh, in);
    CHECK(std::abs(sh.autoGainDb()) < 0.05);
    double el = 0, er = 0, ei = 0; for (size_t i = in.size() - 48000; i < in.size(); ++i) { el += double(l[i]) * l[i]; er += double(r[i]) * r[i]; ei += double(in[i]) * in[i]; }
    CHECK(10 * std::log10(el / ei) == doctest::Approx(Unit::gainDb(1, 0, Unit::kOutputSlot)).epsilon(0.03));
    CHECK(10 * std::log10(er / ei) == doctest::Approx(Unit::gainDb(1, 1, Unit::kOutputSlot)).epsilon(0.03));
}
TEST_CASE("switching Unit does not click (the gain moves over 20 ms)") {
    Shell<GainCore> sh; sh.prepare(kFs, 64, 2); sh.snap();
    const int n = 24000; std::vector<float> in(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * kPi * 100.0 * i / kFs));
    std::vector<float> l = in, r = in;
    for (int off = 0; off < n; off += 64) { if (off == 12000) sh.setUnit(2); float* c[2] = {l.data() + off, r.data() + off}; sh.process(c, 2, 64); }
    double maxStep = 0, refStep = 0;
    for (int i = 1; i < n; ++i) maxStep = std::max(maxStep, double(std::abs(l[static_cast<size_t>(i)] - l[static_cast<size_t>(i) - 1])));
    for (int i = 1; i < n; ++i) refStep = std::max(refStep, double(std::abs(in[static_cast<size_t>(i)] - in[static_cast<size_t>(i) - 1])));
    CHECK(maxStep < refStep * 1.05);
}
