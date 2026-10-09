// SW AUDIO core — iterative radix-2 complex FFT (double) and a real-data FFT on top of it. Sizes: powers of two. inverse() includes the 1/N scale.
//   Fft: the first two stages (twiddles 1 and -+i) are done without multiplications, the later stages read their twiddles from a contiguous table of their own
//   (no strided lookups), and the inverse is the same loops with the sign of the imaginary part of the twiddle flipped.
//   RealFft: n real samples <-> n/2 + 1 bins (bins 0 .. n/2 of the full spectrum; the rest is their mirror image), through an n/2-point complex FFT: half the work of
//   putting the real data into a complex FFT. The convolvers use it (sw/convolver.hpp, sw/deferred_convolver.hpp).
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
        rev_.resize(static_cast<size_t>(n));
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev_[static_cast<size_t>(i)] = r; }
        // twiddles of the stages len = 8, 16 ... n, one after the other: stage len has len/2 of them (k = 0 .. len/2 - 1: exp(-2 pi i k / len))
        twr_.clear(); twi_.clear();
        for (int len = 8; len <= n; len <<= 1)
            for (int k = 0; k < len / 2; ++k) { const double a = -2.0 * 3.14159265358979323846 * k / len; twr_.push_back(std::cos(a)); twi_.push_back(std::sin(a)); }
    }
    int size() const { return n_; }
    void forward(std::vector<std::complex<double>>& x) const { run(reinterpret_cast<double*>(x.data()), 1.0); }
    void inverse(std::vector<std::complex<double>>& x) const {
        run(reinterpret_cast<double*>(x.data()), -1.0);
        const double s = 1.0 / n_;
        for (auto& v : x) v *= s;
    }

private:
    // d: n complex numbers as interleaved doubles; sg = +1 forward, -1 inverse (conjugate twiddles)
    void run(double* d, double sg) const {
        const int n = n_;
        for (int i = 0; i < n; ++i) {
            const int r = rev_[static_cast<size_t>(i)];
            if (r > i) { std::swap(d[2 * i], d[2 * r]); std::swap(d[2 * i + 1], d[2 * r + 1]); }
        }
        if (n >= 2) for (int i = 0; i < n; i += 2) {   // len 2: w = 1
            const double ur = d[2 * i], ui = d[2 * i + 1], vr = d[2 * i + 2], vi = d[2 * i + 3];
            d[2 * i] = ur + vr; d[2 * i + 1] = ui + vi; d[2 * i + 2] = ur - vr; d[2 * i + 3] = ui - vi;
        }
        if (n >= 4) for (int i = 0; i < n; i += 4) {   // len 4: w = 1 and w = -i (forward) / +i (inverse)
            double* p = d + 2 * i;
            { const double ur = p[0], ui = p[1], vr = p[4], vi = p[5]; p[0] = ur + vr; p[1] = ui + vi; p[4] = ur - vr; p[5] = ui - vi; }
            { const double ur = p[2], ui = p[3], br = p[6], bi = p[7]; const double vr = sg * bi, vi = -sg * br; p[2] = ur + vr; p[3] = ui + vi; p[6] = ur - vr; p[7] = ui - vi; }
        }
        const double* wr = twr_.data(); const double* wi = twi_.data();
        for (int len = 8; len <= n; len <<= 1) {
            const int half = len / 2;
            for (int i = 0; i < n; i += len) {
                double* a = d + 2 * i; double* b = a + 2 * half;
                for (int k = 0; k < half; ++k) {
                    const double cr = wr[k], ci = sg * wi[k];
                    const double ur = a[2 * k], ui = a[2 * k + 1], br = b[2 * k], bi = b[2 * k + 1];
                    // by hand: operator* of std::complex<double> goes through __muldc3 (NaN handling) unless -ffast-math, and is several times slower
                    const double vr = br * cr - bi * ci, vi = br * ci + bi * cr;
                    a[2 * k] = ur + vr; a[2 * k + 1] = ui + vi; b[2 * k] = ur - vr; b[2 * k + 1] = ui - vi;
                }
            }
            wr += half; wi += half;
        }
    }
    int n_ = 2;
    std::vector<double> twr_, twi_;
    std::vector<int> rev_;
};

class RealFft {
public:
    // n real samples (a power of two, at least 4)
    void setup(int n) {
        n_ = n; h_ = n / 2; f_.setup(h_);
        wr_.resize(static_cast<size_t>(h_ + 1)); wi_.resize(static_cast<size_t>(h_ + 1));
        for (int k = 0; k <= h_; ++k) { const double a = -2.0 * 3.14159265358979323846 * k / n; wr_[static_cast<size_t>(k)] = std::cos(a); wi_[static_cast<size_t>(k)] = std::sin(a); }
        z_.assign(static_cast<size_t>(h_), std::complex<double>(0, 0));
    }
    int size() const { return n_; }
    // x[n] -> X[n/2 + 1]  (not thread-safe: the object owns its scratch)
    void forward(const double* x, std::complex<double>* X) {
        for (int m = 0; m < h_; ++m) z_[static_cast<size_t>(m)] = std::complex<double>(x[2 * m], x[2 * m + 1]);
        f_.forward(z_);
        const double z0r = z_[0].real(), z0i = z_[0].imag();
        X[0] = std::complex<double>(z0r + z0i, 0.0); X[static_cast<size_t>(h_)] = std::complex<double>(z0r - z0i, 0.0);
        for (int k = 1; k < h_; ++k) {
            const std::complex<double> a = z_[static_cast<size_t>(k)], b = std::conj(z_[static_cast<size_t>(h_ - k)]);
            const double er = 0.5 * (a.real() + b.real()), ei = 0.5 * (a.imag() + b.imag());      // E = (a + b) / 2: the spectrum of the even samples
            const double dr = 0.5 * (a.real() - b.real()), di = 0.5 * (a.imag() - b.imag());      // (a - b) / 2 = i * O: O the spectrum of the odd samples
            const double orr = di, oi = -dr;                                                        // O = -i * (a - b) / 2
            const double cr = wr_[static_cast<size_t>(k)], ci = wi_[static_cast<size_t>(k)];
            X[static_cast<size_t>(k)] = std::complex<double>(er + orr * cr - oi * ci, ei + orr * ci + oi * cr);
        }
    }
    // X[n/2 + 1] -> x[n]  (includes the 1/n scale; the imaginary parts of X[0] and X[n/2] are ignored)
    void inverse(const std::complex<double>* X, double* x) {
        const double x0 = X[0].real(), xh = X[static_cast<size_t>(h_)].real();
        z_[0] = std::complex<double>(0.5 * (x0 + xh), 0.5 * (x0 - xh));
        for (int k = 1; k < h_; ++k) {
            const std::complex<double> a = X[static_cast<size_t>(k)], b = std::conj(X[static_cast<size_t>(h_ - k)]);
            const double er = 0.5 * (a.real() + b.real()), ei = 0.5 * (a.imag() + b.imag());      // E[k]
            const double dr = 0.5 * (a.real() - b.real()), di = 0.5 * (a.imag() - b.imag());      // W^k * O[k]
            const double cr = wr_[static_cast<size_t>(k)], ci = -wi_[static_cast<size_t>(k)];     // conj(W^k)
            const double orr = dr * cr - di * ci, oi = dr * ci + di * cr;                          // O[k]
            z_[static_cast<size_t>(k)] = std::complex<double>(er - oi, ei + orr);                  // Z = E + i * O
        }
        f_.inverse(z_);
        for (int m = 0; m < h_; ++m) { x[2 * m] = z_[static_cast<size_t>(m)].real(); x[2 * m + 1] = z_[static_cast<size_t>(m)].imag(); }
    }

private:
    int n_ = 4, h_ = 2;
    Fft f_;
    std::vector<double> wr_, wi_;
    std::vector<std::complex<double>> z_;
};

}  // namespace sw
