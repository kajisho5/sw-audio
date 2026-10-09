// The spectrum the plug-in screens draw: the audio thread copies the output (L+R)/2 into a ring, the GUI thread takes the last 4096 samples every poll,
// windows them (Hann), runs one FFT and folds the bins into 64 log-spaced bands from 20 Hz to 20 kHz (dB relative to a full-scale sine).
// No allocation on the audio thread; the ring holds relaxed atomics (a torn read only blurs one frame of a display).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sw::gui {

constexpr int kSpecBands = 64;
constexpr int kMaxReadouts = 160;   // the most values a product's `readouts` trait may hand to the screen (RS05 sends a 2 x 64 point waveform)
constexpr int kSpecFft = 4096;
constexpr int kGonioPts = 160;   // stereo scope: correlation, then L, R of 160 recent samples (every 8th)

// in-place radix-2 FFT (n a power of two)
inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) { const auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; }
        }
    }
}

// centre frequency of band b (0 .. kSpecBands-1)
inline double specBandFreq(int b) { return 20.0 * std::pow(1000.0, (b + 0.5) / kSpecBands); }

// x: kSpecFft samples (oldest first) -> out[kSpecBands] in dB (floor -120)
inline void spectrumOf(const float* x, double sampleRate, double* out) {
    std::vector<std::complex<double>> a(kSpecFft);
    double wsum = 0.0;
    for (int i = 0; i < kSpecFft; ++i) { const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / kSpecFft); wsum += w; a[static_cast<size_t>(i)] = x[i] * w; }
    fft(a);
    const double binHz = sampleRate / kSpecFft, nyq = sampleRate / 2.0;
    auto amp = [&](int k) { return 2.0 * std::abs(a[static_cast<size_t>(std::clamp(k, 1, kSpecFft / 2))]) / wsum; };   // amplitude of a sine at that bin
    for (int b = 0; b < kSpecBands; ++b) {
        const double f0 = 20.0 * std::pow(1000.0, static_cast<double>(b) / kSpecBands), f1 = 20.0 * std::pow(1000.0, static_cast<double>(b + 1) / kSpecBands);
        double v;
        if (f0 >= nyq) v = 0.0;
        else if ((f1 - f0) / binHz < 1.5) {   // narrower than a bin: interpolate the amplitude between the two bins around the centre
            const double pos = std::sqrt(f0 * f1) / binHz; const int k = static_cast<int>(pos); const double fr = pos - k;
            v = amp(k) * (1.0 - fr) + amp(k + 1) * fr;
        } else {                              // several bins: the sum of their power, scaled so that a sine in the band reads its own level
            double p = 0.0; const int k0 = std::max(1, static_cast<int>(std::ceil(f0 / binHz))), k1 = std::min(kSpecFft / 2, static_cast<int>(std::floor(f1 / binHz)));
            for (int k = k0; k <= std::max(k0, k1); ++k) { const double m = amp(k); p += m * m; }
            v = std::sqrt(p / 1.5);          // a Hann window spreads a sine over about 1.5 bins of power
        }
        out[b] = v > 1e-6 ? 20.0 * std::log10(v) : -120.0;
    }
}

// what another plug-in of the suite needs to read the ring (SW Link, swlink.hpp): the samples, the write position and the size - 1 (a power of two). Nothing else of SpectrumTap is shared,
// so builds of different versions can read each other's rings.
struct RingView { const std::atomic<float>* data = nullptr; const std::atomic<size_t>* head = nullptr; size_t mask = 0; };

class SpectrumTap {
public:
    RingView view() const { return {ring_, &head_, kRing - 1}; }
    // audio thread
    void push(float* const* d, uint32_t nch, uint32_t frames) {
        if (!nch) return;
        size_t h = head_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frames; ++i) { const float v = nch > 1 ? 0.5f * (d[0][i] + d[1][i]) : d[0][i]; ring_[h & (kRing - 1)].store(v, std::memory_order_relaxed); ++h; }
        head_.store(h, std::memory_order_release);
    }
    // audio thread: the stereo pair for the scope (called with the output, like push)
    void pushStereo(float* const* d, uint32_t nch, uint32_t frames) {
        if (!nch) return;
        size_t h = sh_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frames; ++i) { const float l = d[0][i], r = nch > 1 ? d[1][i] : l; sl_[h & (kRing - 1)].store(l, std::memory_order_relaxed); sr_[h & (kRing - 1)].store(r, std::memory_order_relaxed); ++h; }
        sh_.store(h, std::memory_order_release);
    }
    // GUI thread: out[0] = correlation of the last 2048 samples (-1 .. +1, 0 for silence), then L, R of kGonioPts samples every 8th, oldest first
    void stereo(double* out) const {
        const size_t h = sh_.load(std::memory_order_acquire);
        double ll = 0, rr = 0, lr = 0;
        for (size_t i = 0; i < 2048; ++i) { const double l = sl_[(h - 1 - i) & (kRing - 1)].load(std::memory_order_relaxed), r = sr_[(h - 1 - i) & (kRing - 1)].load(std::memory_order_relaxed); ll += l * l; rr += r * r; lr += l * r; }
        out[0] = (ll > 1e-12 && rr > 1e-12) ? lr / std::sqrt(ll * rr) : 0.0;
        for (int k = 0; k < kGonioPts; ++k) { const size_t idx = (h - 8 * static_cast<size_t>(kGonioPts - k)) & (kRing - 1); out[1 + 2 * k] = sl_[idx].load(std::memory_order_relaxed); out[2 + 2 * k] = sr_[idx].load(std::memory_order_relaxed); }
    }
    // GUI thread
    void compute(double sampleRate, double* out) const {
        std::vector<float> x(kSpecFft);
        const size_t h = head_.load(std::memory_order_acquire);
        for (int i = 0; i < kSpecFft; ++i) x[static_cast<size_t>(i)] = ring_[(h - kSpecFft + static_cast<size_t>(i)) & (kRing - 1)].load(std::memory_order_relaxed);
        spectrumOf(x.data(), sampleRate > 0 ? sampleRate : 48000.0, out);
    }
private:
    static constexpr size_t kRing = 8192;
    std::atomic<float> ring_[kRing] = {};
    std::atomic<size_t> head_{0};
    std::atomic<float> sl_[kRing] = {}, sr_[kRing] = {};
    std::atomic<size_t> sh_{0};
};

}  // namespace sw::gui
