#include "in07/osc.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>

namespace sw::in07 {

const char* const kWaveTableNames[kWaveTables] = {"Classic", "Pulse", "Sync", "Fold", "Formant", "Bright", "Organ", "Glass"};
const char* const kSampleNames[kSamples] = {"Air", "Breath", "Rain", "Static", "Grit", "Knock", "Tick", "Click"};
const char* const kFmRatioLabels[kFmRatios] = {"0.5", "1", "1.5", "2", "3", "4", "5", "6", "7", "9", "11", "14"};

double fmRatioOf(int index) {
    static const double r[kFmRatios] = {0.5, 1, 1.5, 2, 3, 4, 5, 6, 7, 9, 11, 14};
    return r[std::clamp(index, 0, kFmRatios - 1)];
}

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double fmIndexLimit(double inc, double ratio) {
    // k = the first upper sideband (carrier + k x modulator) past Nyquist; (I/2)^k / k! <= 0.002 (a lower sideband may fold at the same k: two of them stay under -50 dB)
    static const std::array<double, 65> lim = [] {
        std::array<double, 65> a{};
        for (int k = 1; k <= 64; ++k) a[static_cast<size_t>(k)] = 2.0 * std::exp((std::log(0.002) + std::lgamma(k + 1.0)) / k);
        return a;
    }();
    const double fm = ratio * inc;
    if (fm <= 0.0) return kFmIndexMax;
    const double room = (0.5 - inc) / fm;
    if (room >= 64.0) return kFmIndexMax;
    const int k = std::max(1, static_cast<int>(room) + 1);
    return std::min(kFmIndexMax, lim[static_cast<size_t>(k)]);
}

double fmFeedbackLimit(double inc, double ratio, double index) {
    const double fm = ratio * inc;
    if (fm <= 0.0) return kFmFeedbackMax;
    const double room = (0.5 - inc) / fm, y = room / std::pow(index + 1.0, 1.5);
    return std::clamp(0.26 * (y - 0.5), 0.0, kFmFeedbackMax) * std::clamp((room - 3.0) / 5.0, 0.0, 1.0);
}

const float* sineTable() {
    static const std::array<float, 4098> t = [] {
        std::array<float, 4098> a{};
        for (int i = 0; i < 4098; ++i) a[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * kPi * i / 4096.0));
        return a;
    }();
    return t.data();
}

// ---- wavetables
namespace {
using Coeffs = std::vector<double>;   // a[h] cos + b[h] sin, h = 0..1024

// one cycle drawn at 8192 points, analysed (what lies past harmonic 4096 folds onto the kept ones at about -80 dB: negligible)
void fromCycle(const std::function<double(double)>& f, Coeffs& a, Coeffs& b) {
    constexpr int N = 8192;
    static const Fft fft(N);
    std::vector<std::complex<double>> x(N);
    for (int n = 0; n < N; ++n) x[static_cast<size_t>(n)] = f(static_cast<double>(n) / N);
    fft.forward(x);
    for (int h = 1; h <= WaveBank::kMaxHarmonics; ++h) {
        a[static_cast<size_t>(h)] = 2.0 * x[static_cast<size_t>(h)].real() / N;
        b[static_cast<size_t>(h)] = -2.0 * x[static_cast<size_t>(h)].imag() / N;
    }
}
// a harmonic of amplitude A and phase phi as sin(h t + phi)
inline void put(Coeffs& a, Coeffs& b, int h, double A, double phi = 0.0) {
    if (h < 1 || h > WaveBank::kMaxHarmonics) return;
    a[static_cast<size_t>(h)] += A * std::sin(phi);
    b[static_cast<size_t>(h)] += A * std::cos(phi);
}

// the frames: x = position 0..1
void frame(int table, double x, Coeffs& a, Coeffs& b) {
    std::fill(a.begin(), a.end(), 0.0);
    std::fill(b.begin(), b.end(), 0.0);
    const int H = WaveBank::kMaxHarmonics;
    switch (table) {
        case 0:   // Classic: a saw (2t - 1) into a square (-1 then +1: the same sign of fundamental, so the mix never cancels)
            for (int h = 1; h <= H; ++h) put(a, b, h, -(1.0 - x) * 2.0 / (kPi * h) - ((h & 1) ? x * 4.0 / (kPi * h) : 0.0));
            break;
        case 1: {  // Pulse: the width from 50 % down to 6 % (the level is normalised, so a thin pulse does not fade)
            const double w = 0.5 - 0.44 * x;
            for (int h = 1; h <= H; ++h) {
                a[static_cast<size_t>(h)] = 2.0 * std::sin(2.0 * kPi * h * w) / (kPi * h);
                b[static_cast<size_t>(h)] = 2.0 * (1.0 - std::cos(2.0 * kPi * h * w)) / (kPi * h);
            }
            break;
        }
        case 2: {  // Sync: a saw hard-synced to the cycle, the slave from 1 to 7 times the pitch
            const double r = 1.0 + 6.0 * x;
            fromCycle([r](double t) { const double u = r * t; return 2.0 * (u - std::floor(u)) - 1.0; }, a, b);
            break;
        }
        case 3: {  // Fold: a sine through a sine folder, the drive from 0.8 to 9.4 (odd harmonics, the Bessel pattern)
            const double g = 0.8 + 8.6 * x;
            fromCycle([g](double t) { return std::sin(g * std::sin(2.0 * kPi * t)); }, a, b);
            break;
        }
        case 4: {  // Formant: a saw through three formants, the vowels a > e > i > o > u (placed for a 130 Hz voice)
            static const double V[5][3] = {{800, 1150, 2800}, {400, 1600, 2700}, {300, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
            const double s = x * 4.0;
            const int i = std::min(3, static_cast<int>(s));
            const double fr = s - i;
            double F[3];
            for (int k = 0; k < 3; ++k) F[k] = std::exp((1.0 - fr) * std::log(V[i][k]) + fr * std::log(V[i + 1][k]));
            static const double B[3] = {80, 90, 120}, G[3] = {1.0, 0.7, 0.35};
            for (int h = 1; h <= H; ++h) {
                const double f = 130.0 * h;
                double m = 0.02;
                for (int k = 0; k < 3; ++k) { const double u = f / F[k], q = F[k] / B[k]; m += G[k] / std::sqrt((1 - u * u) * (1 - u * u) + (u / q) * (u / q)); }
                put(a, b, h, -m / h);
            }
            break;
        }
        case 5: {  // Bright: a saw through a resonant low-pass (Q 5) swept from the 2nd to the 128th harmonic, with the filter's phase
            const double hc = std::exp2(1.0 + 6.0 * x), q = 5.0;
            for (int h = 1; h <= H; ++h) {
                const std::complex<double> s(0.0, h / hc);
                const std::complex<double> resp = 1.0 / (s * s + s / q + 1.0);
                put(a, b, h, -std::abs(resp) / h, std::arg(resp));
            }
            break;
        }
        case 6: {  // Organ: drawbar registrations (harmonics 1 2 3 4 5 6 8, each step -3 dB, 0 = out), flute to full
            static const int hs[7] = {1, 2, 3, 4, 5, 6, 8};
            static const int R[4][7] = {{8, 4, 0, 0, 0, 0, 0}, {8, 8, 6, 4, 0, 0, 0}, {8, 8, 8, 6, 4, 3, 2}, {8, 8, 8, 8, 8, 8, 8}};
            const double s = x * 3.0;
            const int i = std::min(2, static_cast<int>(s));
            const double fr = s - i;
            auto amp = [](int d) { return d <= 0 ? 0.0 : std::pow(10.0, -3.0 * (8 - d) / 20.0); };
            for (int k = 0; k < 7; ++k) put(a, b, hs[k], (1.0 - fr) * amp(R[i][k]) + fr * amp(R[i + 1][k]));
            break;
        }
        default: {  // Glass: sparse partials (1 4 7 11 16 23 31) that come up with the position
            static const int hs[7] = {1, 4, 7, 11, 16, 23, 31};
            for (int k = 0; k < 7; ++k) put(a, b, hs[k], k == 0 ? 1.0 : 0.7 * std::exp(-(hs[k] - 1) / (3.0 + 25.0 * x)));
            break;
        }
    }
    a[0] = b[0] = 0.0;   // no DC
}
}  // namespace

const WaveBank& waveBank() {
    static const WaveBank bank = [] {
        WaveBank w;
        size_t off = 0;
        for (int l = 0; l < WaveBank::kLevels; ++l) {
            const int H = std::max(1, static_cast<int>(std::lround(1024.0 * std::exp2(-0.5 * l))));
            int n = 512;
            while (n < 8 * H && n < 4096) n *= 2;
            w.harmonics[static_cast<size_t>(l)] = H;
            w.size[static_cast<size_t>(l)] = n;
            w.offset[static_cast<size_t>(l)] = off + 1;
            off += static_cast<size_t>(n + WaveBank::kGuard);
        }
        w.stride = off;
        w.data.assign(w.stride * kWaveTables * WaveBank::kFrames, 0.0f);
        Fft ffts[5] = {Fft(256), Fft(512), Fft(1024), Fft(2048), Fft(4096)};
        Coeffs a(WaveBank::kMaxHarmonics + 1), b(WaveBank::kMaxHarmonics + 1);
        std::vector<std::complex<double>> y;
        for (int t = 0; t < kWaveTables; ++t)
            for (int f = 0; f < WaveBank::kFrames; ++f) {
                frame(t, static_cast<double>(f) / (WaveBank::kFrames - 1), a, b);
                double pw = 0.0;
                for (int h = 1; h <= WaveBank::kMaxHarmonics; ++h) pw += 0.5 * (a[static_cast<size_t>(h)] * a[static_cast<size_t>(h)] + b[static_cast<size_t>(h)] * b[static_cast<size_t>(h)]);
                const double g = pw > 0.0 ? 0.5 / std::sqrt(pw) : 0.0;   // RMS 0.5 with every harmonic
                for (int l = 0; l < WaveBank::kLevels; ++l) {
                    const int n = w.size[static_cast<size_t>(l)], H = std::min(w.harmonics[static_cast<size_t>(l)], n / 2 - 1);
                    y.assign(static_cast<size_t>(n), {0.0, 0.0});
                    for (int h = 1; h <= H; ++h) {
                        const std::complex<double> c(0.5 * n * g * a[static_cast<size_t>(h)], -0.5 * n * g * b[static_cast<size_t>(h)]);
                        y[static_cast<size_t>(h)] = c;
                        y[static_cast<size_t>(n - h)] = std::conj(c);
                    }
                    const int fi = n == 256 ? 0 : n == 512 ? 1 : n == 1024 ? 2 : n == 2048 ? 3 : 4;
                    ffts[fi].inverse(y);
                    float* dst = w.data.data() + (static_cast<size_t>(t) * WaveBank::kFrames + static_cast<size_t>(f)) * w.stride + w.offset[static_cast<size_t>(l)];
                    for (int i = 0; i < n; ++i) dst[i] = static_cast<float>(y[static_cast<size_t>(i)].real());
                    dst[-1] = dst[n - 1]; dst[n] = dst[0]; dst[n + 1] = dst[1];
                }
            }
        return w;
    }();
    return bank;
}

// ---- samples
namespace {
struct Rng {
    uint32_t s;
    double uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s * (2.0 / 4294967296.0) - 1.0; }   // -1 .. 1
    double pos() { return 0.5 * (uni() + 1.0); }                                                      // 0 .. 1
};
// RBJ biquads (generation only)
struct Bq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP };
    Bq(Type type, double f, double q, double fs = SampleBank::kRate) {
        const double w = 2.0 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        switch (type) {
            case LP: b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; break;
            case HP: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; break;
            case BP: b0 = al; b1 = 0; b2 = -al; break;   // 0 dB at the centre
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double operator()(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

constexpr int kLoop = 96000, kFade = 4800, kWarm = 4800;   // 2 s loops, a 0.1 s seam, 0.1 s for the filters to settle

// a loop: the generator writes kWarm + kLoop + kFade samples; the seam crossfades the tail into the head (equal power: the material is noise)
std::vector<double> loop(const std::function<double(int)>& gen) {
    std::vector<double> g(static_cast<size_t>(kLoop + kFade));
    for (int i = 0; i < kWarm; ++i) gen(i - kWarm);
    for (int i = 0; i < kLoop + kFade; ++i) g[static_cast<size_t>(i)] = gen(i);
    std::vector<double> s(g.begin(), g.begin() + kLoop);
    for (int i = 0; i < kFade; ++i) {
        const double w = (i + 0.5) / kFade;
        s[static_cast<size_t>(i)] = std::sqrt(w) * g[static_cast<size_t>(i)] + std::sqrt(1.0 - w) * g[static_cast<size_t>(kLoop + i)];
    }
    return s;
}

// a decaying sine (a mode of a struck body)
struct Mode { double f, tau, amp; };
double modes(const Mode* m, int n, double t) {
    double y = 0.0;
    for (int i = 0; i < n; ++i) y += m[i].amp * std::exp(-t / m[i].tau) * std::sin(2.0 * kPi * m[i].f * t);
    return y;
}

void normaliseRms(std::vector<double>& x, double rms) {
    double p = 0.0; for (double v : x) p += v * v;
    const double g = rms / std::sqrt(p / static_cast<double>(x.size()) + 1e-30);
    for (double& v : x) v *= g;
}
void normalisePeak(std::vector<double>& x, double peak) {
    double p = 0.0; for (double v : x) p = std::max(p, std::abs(v));
    const double g = peak / (p + 1e-30);
    for (double& v : x) v *= g;
}

std::vector<double> make(int id) {
    const double fs = SampleBank::kRate, root = SampleBank::kRootHz;
    switch (id) {
        case 0: {  // Air: wind, a band around 3.5 kHz over a softer one at 800 Hz, slow swells (0.5 and 1.5 Hz: whole cycles in the loop)
            Rng r{0xA11u}; Bq hi(Bq::BP, 3500, 0.6), lo(Bq::BP, 800, 0.9), top(Bq::LP, 15000, 0.707);
            auto x = loop([&](int i) {
                const double t = i / fs, n = r.uni();
                const double sw = 1.0 + 0.35 * std::sin(2 * kPi * 0.5 * t) + 0.15 * std::sin(2 * kPi * 1.5 * t + 1.0);
                return top(sw * (hi(n) + 0.35 * lo(n)));
            });
            normaliseRms(x, 0.3); return x;
        }
        case 1: {  // Breath: noise through two vowel bands, breathing in and out once in the loop
            Rng r{0xB4EA7u}; Bq f1(Bq::BP, 1100, 3.0), f2(Bq::BP, 2600, 4.0), air(Bq::HP, 400, 0.707), top(Bq::LP, 14000, 0.707);
            auto x = loop([&](int i) {
                const double t = i / fs, n = r.uni();
                const double e = 0.3 + 0.7 * (0.5 - 0.5 * std::cos(2 * kPi * 0.5 * t));
                return top(e * (f1(n) + 0.7 * f2(n) + 0.12 * air(n)));
            });
            normaliseRms(x, 0.3); return x;
        }
        case 2: {  // Rain: drops (decaying sines, 1.5..6 kHz, about 60 a second) over a soft hiss
            Rng r{0x5A1Bu}; Bq bed(Bq::LP, 6000, 0.707), bedHp(Bq::HP, 300, 0.707);
            struct Drop { double f, tau, amp, t0; };
            std::vector<Drop> drops;
            for (double t = -0.2; t < (kLoop + kFade) / fs; t += -std::log(std::max(1e-9, r.pos())) / 60.0)
                drops.push_back({1500.0 * std::exp2(2.0 * r.pos()), 0.003 + 0.005 * r.pos(), 0.2 + 0.8 * r.pos() * r.pos(), t});
            auto x = loop([&](int i) {
                const double t = i / fs;
                double y = 0.25 * bed(bedHp(r.uni()));
                for (const auto& d : drops) { const double u = t - d.t0; if (u >= 0.0 && u < 8 * d.tau) y += d.amp * std::exp(-u / d.tau) * std::sin(2 * kPi * d.f * u); }
                return y;
            });
            normaliseRms(x, 0.3); return x;
        }
        case 3: {  // Static: crackle (short bursts of random sign, about 25 a second) over a hiss
            Rng r{0x57A7u}; Bq hiss(Bq::HP, 2000, 0.707), top(Bq::LP, 16000, 0.707);
            std::vector<std::pair<double, double>> pops;
            for (double t = -0.2; t < (kLoop + kFade) / fs; t += -std::log(std::max(1e-9, r.pos())) / 25.0)
                pops.push_back({t, (r.uni() < 0 ? -1.0 : 1.0) * (0.2 + 0.8 * r.pos() * r.pos() * r.pos())});
            size_t next = 0;
            double burst = 0.0, sign = 0.0;
            auto x = loop([&](int i) {
                const double t = i / fs;
                while (next < pops.size() && pops[next].first <= t) { burst = 1.0; sign = pops[next].second; ++next; }
                const double c = burst * sign * (0.6 + 0.4 * r.uni());
                burst *= 0.82;   // about 0.2 ms
                return top(c + 0.08 * hiss(r.uni()));
            });
            normaliseRms(x, 0.3); return x;
        }
        case 4: {  // Grit: noise held at a wandering rate (2..6 kHz updates) and crushed to 16 steps, swelling once a second
            Rng r{0x6417u}; Bq top(Bq::LP, 9000, 0.707);
            double held = 0.0, acc = 0.0;
            auto x = loop([&](int i) {
                const double t = i / fs;
                const double rate = 4000.0 + 2000.0 * std::sin(2 * kPi * 1.0 * t);
                acc += rate / fs;
                if (acc >= 1.0) { acc -= std::floor(acc); held = std::round(r.uni() * 8.0) / 8.0; }
                const double sw = 0.6 + 0.4 * (0.5 - 0.5 * std::cos(2 * kPi * 1.0 * t));
                return top(sw * held);
            });
            normaliseRms(x, 0.3); return x;
        }
        case 5: {  // Knock: a wooden knock, 0.4 s (modes at 1, 2.31, 4.12, 6.7 x the root, a 2 ms click)
            static const Mode m[4] = {{root, 0.09, 1.0}, {2.31 * root, 0.055, 0.6}, {4.12 * root, 0.03, 0.35}, {6.7 * root, 0.018, 0.2}};
            Rng r{0x4B0Cu}; Bq click(Bq::BP, 3000, 1.0);
            const int n = static_cast<int>(0.4 * fs);
            std::vector<double> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                const double t = i / fs;
                x[static_cast<size_t>(i)] = modes(m, 4, t) + (t < 0.002 ? 0.8 * click(r.uni()) * (1.0 - t / 0.002) : click(0.0));
            }
            for (int i = 0; i < 240; ++i) x[static_cast<size_t>(n - 240 + i)] *= 0.5 + 0.5 * std::cos(kPi * (i + 1) / 240.0);   // the last 5 ms fade out
            normalisePeak(x, 0.9); return x;
        }
        case 6: {  // Tick: a small metal tick, 0.12 s (modes at 8, 13.3, 19.1 x the root, a 1 ms click)
            static const Mode m[3] = {{8.0 * root, 0.025, 1.0}, {13.3 * root, 0.015, 0.7}, {19.1 * root, 0.009, 0.5}};
            Rng r{0x71C4u}; Bq hp(Bq::HP, 5000, 0.707);
            const int n = static_cast<int>(0.12 * fs);
            std::vector<double> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) { const double t = i / fs; x[static_cast<size_t>(i)] = modes(m, 3, t) + (t < 0.001 ? 0.6 * hp(r.uni()) : hp(0.0)); }
            for (int i = 0; i < 240; ++i) x[static_cast<size_t>(n - 240 + i)] *= 0.5 + 0.5 * std::cos(kPi * (i + 1) / 240.0);
            normalisePeak(x, 0.9); return x;
        }
        default: {  // Click: a short band-passed click, 0.05 s
            Rng r{0xC11Cu}; Bq bp(Bq::BP, 4000, 1.4);
            const int n = static_cast<int>(0.05 * fs);
            std::vector<double> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) { const double t = i / fs; x[static_cast<size_t>(i)] = bp(t < 0.0015 ? r.uni() * (1.0 - t / 0.0015) : 0.0); }
            for (int i = 0; i < 240; ++i) x[static_cast<size_t>(n - 240 + i)] *= 0.5 + 0.5 * std::cos(kPi * (i + 1) / 240.0);
            normalisePeak(x, 0.9); return x;
        }
    }
}

// a Blackman-windowed sinc low-pass of T taps (odd) at fc (of the rate it runs at), DC gain 1
std::vector<double> lowpass(int T, double fc) {
    std::vector<double> h(static_cast<size_t>(T));
    const int C = T / 2;
    double sum = 0.0;
    for (int i = 0; i < T; ++i) {
        const double t = i - C, u = 2.0 * fc * t;
        const double sinc = t == 0 ? 1.0 : std::sin(kPi * u) / (kPi * u);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (T - 1)) + 0.08 * std::cos(4.0 * kPi * i / (T - 1));
        h[static_cast<size_t>(i)] = sinc * w; sum += sinc * w;
    }
    for (double& v : h) v /= sum;
    return h;
}
// the minimum-phase filter with the same magnitude (real cepstrum, as for minBLEP): no ringing before the input, so a one-shot's onset
// stays at its start instead of being cut out of a symmetric filter's pre-ringing (which left a step: broadband energy at -30 dB)
std::vector<double> minimumPhase(const std::vector<double>& h) {
    const int T = static_cast<int>(h.size()), M = 16384;
    std::vector<std::complex<double>> x(static_cast<size_t>(M));
    for (int i = 0; i < T; ++i) x[static_cast<size_t>(i)] = h[static_cast<size_t>(i)];
    Fft f(M);
    f.forward(x);
    for (auto& v : x) v = std::log(std::max(std::abs(v), 1e-12));
    f.inverse(x);
    for (int i = 1; i < M / 2; ++i) { x[static_cast<size_t>(i)] *= 2.0; x[static_cast<size_t>(M - i)] = 0.0; }
    f.forward(x);
    for (auto& v : x) v = std::exp(v);
    f.inverse(x);
    std::vector<double> m(static_cast<size_t>(T));
    double sum = 0.0;
    for (int i = 0; i < T; ++i) { m[static_cast<size_t>(i)] = x[static_cast<size_t>(i)].real(); sum += m[static_cast<size_t>(i)]; }
    for (double& v : m) v /= sum;
    return m;
}
// x padded with `pre` samples before and `post` after: the loop's own samples around the seam, or zeros
std::vector<double> padded(const std::vector<double>& x, int pre, int post, bool circular) {
    const int n = static_cast<int>(x.size());
    std::vector<double> y(static_cast<size_t>(pre + n + post), 0.0);
    for (int i = -pre; i < n + post; ++i) {
        int j = i;
        if (circular) { j %= n; if (j < 0) j += n; } else if (j < 0 || j >= n) continue;
        y[static_cast<size_t>(i + pre)] = x[static_cast<size_t>(j)];
    }
    return y;
}
constexpr int kT = 191, kC = kT / 2;   // the level filters: 191 taps
// x2 up: zero-stuff and low-pass at 0.21 of the new rate (flat to 0.195, gone from 0.225), polyphase (the stuffed zeros are skipped).
// Loops: zero phase, around the seam. One-shots: minimum phase (causal; the output runs on by the filter's length).
std::vector<double> up2(const std::vector<double>& x, bool circular) {
    static const std::vector<double> hz = lowpass(kT, 0.21), hm = minimumPhase(lowpass(kT, 0.21));
    const std::vector<double>& h = circular ? hz : hm;
    const int n = static_cast<int>(x.size()), m = circular ? 2 * n : 2 * n + kT;
    const int pre = circular ? kC / 2 + 2 : kC + 2, post = kC + 2;   // the causal sum reaches 95 inputs back
    const std::vector<double> xp = padded(x, pre, post, circular);
    std::vector<double> y(static_cast<size_t>(m), 0.0);
    for (int o = 0; o < m; ++o) {
        const int i = o >> 1, r = o & 1;
        double acc = 0.0;
        if (circular) {   // y[2i + r] = 2 sum_t h[k0 + 2t] x[i + t - 47], k0 = (95 - r) & 1
            const int k0 = (kC - r) & 1;
            for (int t = 0; k0 + 2 * t < kT; ++t) acc += h[static_cast<size_t>(k0 + 2 * t)] * xp[static_cast<size_t>(i + t - kC / 2 + pre)];
        } else {          // y[2i + r] = 2 sum_t h[r + 2t] x[i - t]
            for (int t = 0; r + 2 * t < kT; ++t) acc += h[static_cast<size_t>(r + 2 * t)] * xp[static_cast<size_t>(i - t + pre)];
        }
        y[static_cast<size_t>(o)] = 2.0 * acc;
    }
    return y;
}
// /2 down: low-pass at 0.105 of the input rate (flat to 0.09, gone from 0.12 = 0.24 of the new rate), every second sample
std::vector<double> down2(const std::vector<double>& x, bool circular) {
    static const std::vector<double> hz = lowpass(kT, 0.105), hm = minimumPhase(lowpass(kT, 0.105));
    const std::vector<double>& h = circular ? hz : hm;
    const int n = static_cast<int>(x.size()), m = circular ? n / 2 : (n + kT) / 2 + 1;
    const std::vector<double> xp = padded(x, kT, kT + 2, circular);
    std::vector<double> y(static_cast<size_t>(m), 0.0);
    for (int i = 0; i < m; ++i) {
        double acc = 0.0;
        if (circular) { const double* q = xp.data() + 2 * i - kC + kT; for (int k = 0; k < kT; ++k) acc += h[static_cast<size_t>(k)] * q[k]; }
        else { const double* q = xp.data() + 2 * i + kT; for (int k = 0; k < kT; ++k) acc += h[static_cast<size_t>(k)] * q[-k]; }
        y[static_cast<size_t>(i)] = acc;
    }
    return y;
}
}  // namespace

const SampleBank& sampleBank() {
    static const SampleBank bank = [] {
        SampleBank b;
        for (int id = 0; id < kSamples; ++id) {
            SampleBank::Sample& s = b.s[static_cast<size_t>(id)];
            s.loop = id < 5;
            std::vector<double> x = make(id);
            s.length = static_cast<int>(x.size());
            if (!s.loop) x.resize(x.size() + SampleBank::kTail, 0.0);   // room for the filters' smear
            x = up2(x, s.loop);                                          // level 0: 96 kHz, content under 20.2 kHz
            for (int j = 0; j < SampleBank::kLevels; ++j) {
                if (j > 0) x = down2(x, s.loop);
                const int n = static_cast<int>(x.size());
                auto& v = s.lv[static_cast<size_t>(j)];
                v.assign(static_cast<size_t>(SampleBank::kPre + n + SampleBank::kPost), 0.0f);
                for (int i = 0; i < n; ++i) v[static_cast<size_t>(SampleBank::kPre + i)] = static_cast<float>(x[static_cast<size_t>(i)]);
                if (s.loop) {   // the guards wrap around the seam
                    for (int i = 0; i < SampleBank::kPre; ++i) v[static_cast<size_t>(i)] = v[static_cast<size_t>(n + i)];
                    for (int i = 0; i < SampleBank::kPost; ++i) v[static_cast<size_t>(SampleBank::kPre + n + i)] = v[static_cast<size_t>(SampleBank::kPre + i)];
                }
            }
        }
        return b;
    }();
    return bank;
}

const SincTable& sincTable() {
    static const SincTable table = [] {
        SincTable k;
        constexpr int T = SincTable::kTaps, P = SincTable::kPhases;
        constexpr double beta = 7.0, fc = 0.45;
        auto i0 = [](double x) { double s = 1.0, term = 1.0; for (int m = 1; m < 40; ++m) { term *= (x / (2.0 * m)) * (x / (2.0 * m)); s += term; } return s; };
        std::vector<double> all(static_cast<size_t>((P + 1) * T));
        for (int p = 0; p <= P; ++p) {
            const double frac = static_cast<double>(p) / P;
            double sum = 0.0;
            for (int t = 0; t < T; ++t) {
                const double tau = (t - 3) - frac, u = 2.0 * fc * tau, r = tau / 4.0;
                const double sinc = std::abs(u) < 1e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u);
                const double w = std::abs(r) >= 1.0 ? 0.0 : i0(beta * std::sqrt(1.0 - r * r)) / i0(beta);
                all[static_cast<size_t>(p * T + t)] = sinc * w; sum += sinc * w;
            }
            for (int t = 0; t < T; ++t) all[static_cast<size_t>(p * T + t)] /= sum;   // unity gain at DC at every phase
        }
        k.h.resize(static_cast<size_t>(P * T)); k.d.resize(static_cast<size_t>(P * T));
        for (int i = 0; i < P * T; ++i) { k.h[static_cast<size_t>(i)] = static_cast<float>(all[static_cast<size_t>(i)]); k.d[static_cast<size_t>(i)] = static_cast<float>(all[static_cast<size_t>(i + T)] - all[static_cast<size_t>(i)]); }
        return k;
    }();
    return table;
}

}  // namespace sw::in07
