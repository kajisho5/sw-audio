// SW AUDIO core — FIR kernels from analog band prototypes (EQ02 Linear/Natural, EQ08 Linear/Mixed/Pre-ring guard)
// kernel = (linear-phase part, delayed by L/2) x (minimum-phase part, via the real cepstrum), one FIR of length L.
#pragma once
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

namespace sw {

struct BandShape {
    enum Type { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch };
    Type type = Bell;
    double freq = 1000, gainDb = 0, q = 0.7071, slope = 12;  // slope (dB/oct) for cuts
    // complex response of the analog prototype at normalized frequency w = f / freq (q given explicitly)
    std::complex<double> responseNorm(double w, double qq) const {
        using cd = std::complex<double>;
        const cd s(0, std::max(w, 1e-12));
        const double A = std::pow(10.0, gainDb / 40.0);
        switch (type) {
            case Bell: return (s * s + s * (A / qq) + 1.0) / (s * s + s / (A * qq) + 1.0);
            case LowShelf: return A * (s * s + (std::sqrt(A) / qq) * s + A) / (A * s * s + (std::sqrt(A) / qq) * s + 1.0);
            case HighShelf: return A * (A * s * s + (std::sqrt(A) / qq) * s + 1.0) / (s * s + (std::sqrt(A) / qq) * s + A);
            case Notch: return (s * s + 1.0) / (s * s + s / qq + 1.0);
            case LowCut: case HighCut: {
                const int order = std::max(1, static_cast<int>(std::lround(slope / 6.0)));
                cd h = 1.0;
                for (int k = 1; k <= order / 2; ++k) {
                    const double qk = order == 2 ? qq : 1.0 / (2.0 * std::cos((2.0 * k - 1.0) * 3.14159265358979323846 / (2.0 * order)));
                    const cd d = s * s + s / qk + 1.0;
                    h *= type == LowCut ? s * s / d : 1.0 / d;
                }
                if (order % 2) h *= type == LowCut ? s / (s + 1.0) : 1.0 / (s + 1.0);
                return h;
            }
        }
        return 1.0;
    }
    std::complex<double> response(double f) const { return responseNorm(f / freq, q); }
    double magnitude(double f) const { return std::abs(response(f)); }
};

inline double totalMagnitude(const std::vector<BandShape>& bands, double f) {
    double m = 1.0;
    for (const auto& b : bands) m *= b.magnitude(f);
    return m;
}

// the designer keeps its FFT tables and work arrays between designs (prepare(L) makes them: a kernel is redesigned on the audio thread when a knob moves, which must not allocate, and the twiddle
// table of a 4 L point FFT is most of the cost of a design when it is rebuilt each time). The magnitude functions are the linear-phase part's and the minimum-phase part's (templates: no std::function).
class FirDesigner {
public:
    void prepare(int L) { L_ = L; N_ = 4 * L; fft_.setup(N_); lin_.assign(static_cast<size_t>(N_), {}); mn_.assign(static_cast<size_t>(N_), {}); K_.assign(static_cast<size_t>(N_), {}); h_.assign(static_cast<size_t>(L), 0.0); }
    int length() const { return L_; }
    // the kernel of length L (= prepare()'s) for the given magnitude functions; the returned vector is the designer's own and is valid until the next design
    template <class LinMag, class MinMag>
    const std::vector<double>& design(LinMag linMag, MinMag minMag, bool hasMin, double fs) {
        using cd = std::complex<double>;
        const int N = N_, L = L_;  // dense grid: accurate cepstrum, little time aliasing
        for (int k = 0; k <= N / 2; ++k) {
            const double f = static_cast<double>(k) * fs / N;
            lin_[static_cast<size_t>(k)] = linMag(f);
            mn_[static_cast<size_t>(k)] = std::log(std::max(static_cast<double>(minMag(f)), 1e-12));
        }
        for (int k = 1; k < N / 2; ++k) { lin_[static_cast<size_t>(N - k)] = lin_[static_cast<size_t>(k)]; mn_[static_cast<size_t>(N - k)] = mn_[static_cast<size_t>(k)]; }
        // minimum phase from the real cepstrum (fold the anti-causal half onto the causal half)
        if (hasMin) {
            fft_.inverse(mn_);
            for (int n = 1; n < N / 2; ++n) { mn_[static_cast<size_t>(n)] *= 2.0; mn_[static_cast<size_t>(N - n)] = 0.0; }
            fft_.forward(mn_);
            for (auto& v : mn_) v = std::exp(v);
        } else {
            for (auto& v : mn_) v = 1.0;
        }
        const int D = L / 2;
        for (int k = 0; k < N; ++k) {
            const int kk = k <= N / 2 ? k : k - N;  // signed bin keeps the delay phase Hermitian
            K_[static_cast<size_t>(k)] = lin_[static_cast<size_t>(k)] * mn_[static_cast<size_t>(k)] * std::polar(1.0, -2.0 * 3.14159265358979323846 * kk * D / N);
        }
        fft_.inverse(K_);
        for (int n = 0; n < L; ++n) {
            // Tukey window centred on the delay (25 % cosine tapers at both ends): keeps resolution, removes the edge step
            const double d = std::abs(n - D) / static_cast<double>(D);
            const double w = d < 0.75 ? 1.0 : 0.5 * (1.0 + std::cos(3.14159265358979323846 * (d - 0.75) / 0.25));
            h_[static_cast<size_t>(n)] = K_[static_cast<size_t>(n)].real() * w;
        }
        return h_;
    }

private:
    int L_ = 0, N_ = 0;
    Fft fft_;
    std::vector<std::complex<double>> lin_, mn_, K_;
    std::vector<double> h_;
};

// generic form: magnitude functions for the linear-phase part and the minimum-phase part (allocates: for tests and offline use; the products keep a FirDesigner)
inline std::vector<double> designKernelFn(const std::function<double(double)>& linMag, const std::function<double(double)>& minMag,
                                          bool hasMin, int L, double fs) {
    FirDesigner d; d.prepare(L);
    return d.design(linMag, minMag, hasMin, fs);
}

// linear: realized with linear phase (delay L/2). minimum: realized with minimum phase (no added delay, no pre-ring)
inline std::vector<double> designKernel(const std::vector<BandShape>& linear, const std::vector<BandShape>& minimum, int L, double fs) {
    return designKernelFn([&](double f) { return totalMagnitude(linear, f); }, [&](double f) { return totalMagnitude(minimum, f); },
                          !minimum.empty(), L, fs);
}

}  // namespace sw
