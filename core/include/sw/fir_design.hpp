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
    static constexpr int kPerOctave = 96;            // the log-frequency grid the magnitude functions are evaluated on (and interpolated from) ...
    static constexpr double kGridHz = 20.0;          // ... from here up; below it every bin is evaluated itself
    static constexpr int kMaxGrid = 1700;            // 96 per octave up to 384 kHz
    static constexpr double kRefineLn = 0.01;        // the interpolation may be off by this much (in the log of the magnitude: about 0.09 dB) in the middle of an interval
    void prepare(int L) {
        L_ = L; N_ = 4 * L; fft_.setup(N_);
        lin_.assign(static_cast<size_t>(N_), {}); mn_.assign(static_cast<size_t>(N_), {}); K_.assign(static_cast<size_t>(N_), {}); h_.assign(static_cast<size_t>(L), 0.0);
        win_.assign(static_cast<size_t>(L), 1.0);   // the Tukey window centred on the delay (25 % cosine tapers at both ends): keeps resolution, removes the edge step
        const int D = L / 2;
        for (int n = 0; n < L; ++n) { const double d = std::abs(n - D) / static_cast<double>(D); win_[static_cast<size_t>(n)] = d < 0.75 ? 1.0 : 0.5 * (1.0 + std::cos(3.14159265358979323846 * (d - 0.75) / 0.25)); }
        gl_.assign(kMaxGrid, 0.0); gm_.assign(kMaxGrid, 0.0); flag_.assign(kMaxGrid, 0);
    }
    int length() const { return L_; }
    // the kernel of length L (= prepare()'s) for the given magnitude functions; the returned vector is the designer's own and is valid until the next design.
    // The functions are evaluated on a log grid (96 points per octave, from 20 Hz) and interpolated (cubic, in the log of the magnitude) to the bins: the responses are smooth in log frequency, and a
    // bin-by-bin evaluation of 24 bands over 16k bins was most of the cost; `exactBins` evaluates every bin (the reference of the tests)
    template <class LinMag, class MinMag>
    const std::vector<double>& design(LinMag linMag, MinMag minMag, bool hasMin, double fs, bool exactBins = false) {
        using cd = std::complex<double>;
        const int N = N_, L = L_;  // dense grid: accurate cepstrum, little time aliasing
        const double binHz = fs / N;
        const int J = std::min(kMaxGrid, static_cast<int>(std::ceil(kPerOctave * std::log2(std::max(0.5 * fs, 2.0 * kGridHz) / kGridHz))) + 3);
        auto cubic = [&](const std::vector<double>& g, int j, double t) {   // Catmull-Rom through g[j - 1 .. j + 2]
            const double p0 = g[static_cast<size_t>(std::max(j - 1, 0))], p1 = g[static_cast<size_t>(j)], p2 = g[static_cast<size_t>(std::min(j + 1, J - 1))], p3 = g[static_cast<size_t>(std::min(j + 2, J - 1))];
            return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
        };
        auto lnLin = [&](double f) { return std::log(std::max(static_cast<double>(linMag(f)), 1e-9)); };
        auto lnMin = [&](double f) { return std::log(std::max(static_cast<double>(minMag(f)), 1e-12)); };
        if (!exactBins) {
            for (int j = 0; j < J; ++j) { const double f = kGridHz * std::pow(2.0, static_cast<double>(j) / kPerOctave); gl_[static_cast<size_t>(j)] = lnLin(f); gm_[static_cast<size_t>(j)] = lnMin(f); }
            // where the interpolation misses (a notch, a very narrow bell: the response is not smooth over a grid step) the middle of the interval is evaluated and compared: such intervals get every bin evaluated itself
            for (int j = 0; j < J - 1; ++j) {
                const double f = kGridHz * std::pow(2.0, (static_cast<double>(j) + 0.5) / kPerOctave);
                flag_[static_cast<size_t>(j)] = std::abs(lnLin(f) - cubic(gl_, j, 0.5)) > kRefineLn || std::abs(lnMin(f) - cubic(gm_, j, 0.5)) > kRefineLn;
            }
            flag_[static_cast<size_t>(J - 1)] = true;
        }
        for (int k = 0; k <= N / 2; ++k) {
            const double f = static_cast<double>(k) * binHz;
            int j = 0; double t = 0;
            bool exact = exactBins || f < kGridHz;
            if (!exact) { const double x = std::log2(f / kGridHz) * kPerOctave; j = std::min(J - 2, static_cast<int>(x)); t = x - j; exact = flag_[static_cast<size_t>(j)]; }
            if (exact) { lin_[static_cast<size_t>(k)] = linMag(f); mn_[static_cast<size_t>(k)] = lnMin(f); }
            else { lin_[static_cast<size_t>(k)] = std::exp(cubic(gl_, j, t)); mn_[static_cast<size_t>(k)] = cubic(gm_, j, t); }
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
        // the delay of L / 2 samples on a grid of 4 L bins is a phase of -pi kk / 4 on bin kk: eight values (no sin / cos per bin)
        cd ph[8]; for (int m = 0; m < 8; ++m) ph[m] = std::polar(1.0, -3.14159265358979323846 * m / 4.0);
        for (int k = 0; k < N; ++k) {
            const int kk = k <= N / 2 ? k : k - N;  // signed bin keeps the delay phase Hermitian
            K_[static_cast<size_t>(k)] = lin_[static_cast<size_t>(k)] * mn_[static_cast<size_t>(k)] * ph[static_cast<size_t>(((kk % 8) + 8) % 8)];
        }
        fft_.inverse(K_);
        for (int n = 0; n < L; ++n) h_[static_cast<size_t>(n)] = K_[static_cast<size_t>(n)].real() * win_[static_cast<size_t>(n)];
        return h_;
    }

private:
    int L_ = 0, N_ = 0;
    Fft fft_;
    std::vector<std::complex<double>> lin_, mn_, K_;
    std::vector<double> h_, win_, gl_, gm_;
    std::vector<char> flag_;
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
