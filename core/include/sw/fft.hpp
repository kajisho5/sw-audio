// SW AUDIO core — iterative radix-2 complex FFT (double). Sizes: powers of two. inverse() includes the 1/N scale.
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace sw {

class Fft {
public:
    explicit Fft(int n = 2) { setup(n); }
    void setup(int n) {
        n_ = n;
        tw_.resize(static_cast<size_t>(n / 2));
        for (int k = 0; k < n / 2; ++k) tw_[static_cast<size_t>(k)] = std::polar(1.0, -2.0 * 3.14159265358979323846 * k / n);
        rev_.resize(static_cast<size_t>(n));
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev_[static_cast<size_t>(i)] = r; }
    }
    int size() const { return n_; }
    void forward(std::vector<std::complex<double>>& x) const { run(x, false); }
    void inverse(std::vector<std::complex<double>>& x) const {
        run(x, true);
        const double s = 1.0 / n_;
        for (auto& v : x) v *= s;
    }

private:
    void run(std::vector<std::complex<double>>& x, bool inv) const {
        for (int i = 0; i < n_; ++i) { const int r = rev_[static_cast<size_t>(i)]; if (r > i) std::swap(x[static_cast<size_t>(i)], x[static_cast<size_t>(r)]); }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len / 2, step = n_ / len;
            for (int i = 0; i < n_; i += len)
                for (int k = 0; k < half; ++k) {
                    std::complex<double> w = tw_[static_cast<size_t>(k * step)];
                    if (inv) w = std::conj(w);
                    const std::complex<double> u = x[static_cast<size_t>(i + k)], b = x[static_cast<size_t>(i + k + half)];
                    // by hand: operator* of std::complex<double> goes through __muldc3 (NaN handling) unless -ffast-math, and is several times slower
                    const std::complex<double> v(b.real() * w.real() - b.imag() * w.imag(), b.real() * w.imag() + b.imag() * w.real());
                    x[static_cast<size_t>(i + k)] = u + v;
                    x[static_cast<size_t>(i + k + half)] = u - v;
                }
        }
    }
    int n_ = 2;
    std::vector<std::complex<double>> tw_;
    std::vector<int> rev_;
};

}  // namespace sw
