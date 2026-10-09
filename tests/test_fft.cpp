#include "doctest.h"
#include "sw/convolver.hpp"
#include "sw/deferred_convolver.hpp"
#include "sw/fft.hpp"
#include "sw/tiered_convolver.hpp"
#include "sw/zl_convolver.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <type_traits>
#include <vector>
using namespace sw;

namespace {
using cd = std::complex<double>;
std::vector<double> randomSignal(size_t n, unsigned seed, double amp = 1.0) { std::mt19937 g(seed); std::uniform_real_distribution<double> d(-amp, amp); std::vector<double> x(n); for (auto& v : x) v = d(g); return x; }
// the kernel of a reverb-like decay (so the sums stay small and a relative error means something)
std::vector<double> decayKernel(size_t n, unsigned seed) { auto h = randomSignal(n, seed, 0.3); for (size_t i = 0; i < n; ++i) h[i] *= std::exp(-6.0 * static_cast<double>(i) / static_cast<double>(n)); return h; }
std::vector<double> directConv(const std::vector<float>& x, const std::vector<double>& h) {   // y[n] = sum_k h[k] x[n-k]
    std::vector<double> y(x.size(), 0.0);
    for (size_t n = 0; n < x.size(); ++n) { double s = 0; const size_t kmax = std::min(h.size(), n + 1); for (size_t k = 0; k < kmax; ++k) s += h[k] * x[n - k]; y[n] = s; }
    return y;
}
}  // namespace

TEST_CASE("Fft: forward matches the plain DFT for every size, inverse undoes it") {
    for (int n = 2; n <= 1024; n <<= 1) {
        Fft f(n); const auto re = randomSignal(static_cast<size_t>(n), 10u + static_cast<unsigned>(n)), im = randomSignal(static_cast<size_t>(n), 90u + static_cast<unsigned>(n));
        std::vector<cd> x(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = cd(re[static_cast<size_t>(i)], im[static_cast<size_t>(i)]);
        auto y = x; f.forward(y);
        double worst = 0;
        for (int k = 0; k < n; ++k) { cd s(0, 0); for (int i = 0; i < n; ++i) s += x[static_cast<size_t>(i)] * std::polar(1.0, -2.0 * 3.14159265358979323846 * static_cast<double>(k) * i / n); worst = std::max(worst, std::abs(s - y[static_cast<size_t>(k)])); }
        CHECK(worst < 1e-9 * n);
        auto z = y; f.inverse(z); double back = 0; for (int i = 0; i < n; ++i) back = std::max(back, std::abs(z[static_cast<size_t>(i)] - x[static_cast<size_t>(i)]));
        CHECK(back < 1e-12 * n);
    }
}

TEST_CASE("RealFft: the half spectrum of real data equals the complex FFT's, and the inverse returns the data") {
    for (int n = 4; n <= 4096; n <<= 1) {
        RealFft r; r.setup(n); Fft f(n); const auto x = randomSignal(static_cast<size_t>(n), 300u + static_cast<unsigned>(n));
        std::vector<cd> X(static_cast<size_t>(n / 2 + 1)); r.forward(x.data(), X.data());
        std::vector<cd> c(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) c[static_cast<size_t>(i)] = cd(x[static_cast<size_t>(i)], 0.0); f.forward(c);
        double worst = 0; for (int k = 0; k <= n / 2; ++k) worst = std::max(worst, std::abs(X[static_cast<size_t>(k)] - c[static_cast<size_t>(k)]));
        CHECK(worst < 1e-11 * n); CHECK(X[0].imag() == 0.0); CHECK(X[static_cast<size_t>(n / 2)].imag() == 0.0);
        std::vector<double> y(static_cast<size_t>(n)); r.inverse(X.data(), y.data()); double back = 0; for (int i = 0; i < n; ++i) back = std::max(back, std::abs(y[static_cast<size_t>(i)] - x[static_cast<size_t>(i)]));
        CHECK(back < 1e-12 * n);
    }
    // a convolution through the half spectra: circular product of two real sequences
    { const int n = 64; RealFft r; r.setup(n); const auto a = randomSignal(n, 1), b = randomSignal(n, 2); std::vector<cd> A(n / 2 + 1), B(n / 2 + 1); r.forward(a.data(), A.data()); r.forward(b.data(), B.data());
      for (int k = 0; k <= n / 2; ++k) A[static_cast<size_t>(k)] *= B[static_cast<size_t>(k)];
      std::vector<double> y(n); r.inverse(A.data(), y.data()); double worst = 0;
      for (int i = 0; i < n; ++i) { double s = 0; for (int j = 0; j < n; ++j) s += a[static_cast<size_t>(j)] * b[static_cast<size_t>((i - j + n) % n)]; worst = std::max(worst, std::abs(s - y[static_cast<size_t>(i)])); }
      CHECK(worst < 1e-12); }
}

TEST_CASE("Convolver / DeferredConvolver / ZeroLatencyConvolver / TieredConvolver equal the direct sum") {
    const size_t N = 40000;
    std::vector<float> x(N); { const auto r = randomSignal(N, 7, 0.5); for (size_t i = 0; i < N; ++i) x[i] = static_cast<float>(r[i]); }
    auto run1 = [&](auto& c) { std::vector<float> y = x; for (size_t off = 0; off < N; off += 256) { const int m = static_cast<int>(std::min<size_t>(256, N - off)); c.process(y.data() + off, m); } return y; };
    auto check = [&](const std::vector<float>& y, const std::vector<double>& ref, size_t latency, double tol, const char* what) {
        double worst = 0; for (size_t i = latency; i < N; ++i) worst = std::max(worst, std::abs(static_cast<double>(y[i]) - ref[i - latency])); INFO(what); CHECK(worst < tol);
    };
    { const auto h = decayKernel(3000, 1); Convolver c; c.prepare(3000, 256, 1); c.setKernel(h, true);
      std::vector<float> y = x; for (size_t off = 0; off < N; off += 256) { float* p[1] = {y.data() + off}; c.process(p, 1, static_cast<int>(std::min<size_t>(256, N - off))); }
      check(y, directConv(x, h), 256, 2e-6, "Convolver (latency = one block)"); }
    { const auto h = decayKernel(20000, 2); DeferredConvolver c; c.prepare(20000, 1024); c.beginKernel(h); while (!c.stepKernel(1 << 20)) {} c.commitKernel(true);
      const auto y = run1(c); check(y, directConv(x, h), 2048, 2e-6, "DeferredConvolver (latency = two blocks)"); }
    { const auto h = decayKernel(3000, 3); ZeroLatencyConvolver c; c.prepare(3000, 128, 1, 960); c.setKernel(h, true);
      std::vector<float> y = x; for (size_t off = 0; off < N; off += 256) { float* p[1] = {y.data() + off}; c.process(p, 1, static_cast<int>(std::min<size_t>(256, N - off))); }
      check(y, directConv(x, h), 0, 2e-6, "ZeroLatencyConvolver (no latency)"); }
    for (int K : {1152, 5000, 30000}) {   // tier 1 only, tiers 1 + 2, all three tiers (the longer ones start the third tier after 16384 taps)
        const auto h = decayKernel(static_cast<size_t>(K), 4u + static_cast<unsigned>(K)); TieredConvolver c; c.prepare(K, 960, 0); c.setKernel(h, true);
        const auto y = run1(c); INFO("K = " << K); check(y, directConv(x, h), 0, 3e-6, "TieredConvolver (no latency)");
    }
}

TEST_CASE("Convolver / DeferredConvolver / ZeroLatencyConvolver / TieredConvolver: reset() forgets the audio and keeps the kernel (a reverb tail must not come out after the host jumped)") {
    const size_t N = 40000;
    std::vector<float> x(N); { const auto r = randomSignal(N, 11, 0.5); for (size_t i = 0; i < N; ++i) x[i] = static_cast<float>(r[i]); }
    auto proc = [](auto& c, float* p, int n) {
        using C = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<C, Convolver> || std::is_same_v<C, ZeroLatencyConvolver>) { float* pp[1] = {p}; c.process(pp, 1, n); } else c.process(p, n);
    };
    // x for 12000 samples, reset(), then silence with one impulse: the output after the reset is the kernel itself (no trace of x), after the kernel's latency
    auto check = [&](auto& c, int latency, const std::vector<double>& h, const char* name) {
        std::vector<float> y(N, 0.0f); for (size_t i = 0; i < 12000; ++i) y[i] = x[i];
        size_t off = 0; for (; off < 12000; off += 256) proc(c, y.data() + off, 256);                 // (12000 is not a multiple of 256: the last block runs on into the silence)
        c.reset();
        for (size_t i = off; i < N; ++i) y[i] = 0.0f;
        const size_t at = off + 100; y[at] = 1.0f;
        for (; off + 256 <= N; off += 256) proc(c, y.data() + off, 256);
        double leak = 0; for (size_t i = at + 1 + 0; i < at + static_cast<size_t>(latency); ++i) leak = std::max(leak, static_cast<double>(std::fabs(y[i])));   // before the impulse's own result: only old audio could be here
        double before = 0; for (size_t i = (12000 / 256 + 1) * 256; i < at; ++i) before = std::max(before, static_cast<double>(std::fabs(y[i])));
        double err = 0; for (size_t k = 0; k < std::min<size_t>(h.size(), 4000); ++k) err = std::max(err, std::abs(static_cast<double>(y[at + static_cast<size_t>(latency) + k]) - h[k]));
        INFO(name); CHECK(before < 1e-9); CHECK(err < 3e-6); (void)leak;
    };
    { const auto h = decayKernel(3000, 1); Convolver c; c.prepare(3000, 256, 1); c.setKernel(h, true); check(c, 256, h, "Convolver"); }
    { const auto h = decayKernel(20000, 2); DeferredConvolver c; c.prepare(20000, 1024); c.beginKernel(h); while (!c.stepKernel(1 << 20)) {} c.commitKernel(true); check(c, 2048, h, "DeferredConvolver"); }
    { const auto h = decayKernel(3000, 3); ZeroLatencyConvolver c; c.prepare(3000, 128, 1, 960); c.setKernel(h, true); check(c, 0, h, "ZeroLatencyConvolver"); }
    { const auto h = decayKernel(30000, 4); TieredConvolver c; c.prepare(30000, 960, 1); c.setKernel(h, true); check(c, 0, h, "TieredConvolver (phase 1: the heavy blocks keep their place)"); }
}
