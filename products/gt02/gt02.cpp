#include "gt02/gt02.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <complex>
#include <functional>

namespace sw::gt02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"gt02.cab",         "Cab",          0, 2, 2,     Curve::Step, 1, {0, 1, 2}, "", {"1x12", "2x12", "4x12"}},
            {"gt02.mic",         "Mic",          0, 2, 0,     Curve::Step, 1, {0, 1, 2}, "", {"Dynamic", "Ribbon", "Condenser"}},
            {"gt02.micdistance", "Mic distance", 0, 30, 4,    Curve::Lin, 1, {}, "cm"},
            {"gt02.offaxis",     "Off axis",     0, 90, 15,   Curve::Lin, 1, {}, "deg"},
            {"gt02.room",        "Room",         0, 100, 10,  Curve::Lin, 1, {}, "%"},
            {"gt02.lowcut",      "Low cut",      20, 300, 80, Curve::Log, 1, {}, "Hz"},
        };
        v[LowCut].minLabel = "Off";
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
double hpDb(double f, double f0, int n) { return -10.0 * std::log10(1.0 + std::pow(f0 / std::max(f, 1e-3), 2.0 * n)); }
double lpDb(double f, double fc, int n) { return -10.0 * std::log10(1.0 + std::pow(f / fc, 2.0 * n)); }
double bellDb(double f, double fc, double g, double q) {
    const std::complex<double> s(0.0, f / fc);
    const double A = std::pow(10.0, g / 40.0);
    return 20.0 * std::log10(std::abs((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0)));
}
double lowShelfDb(double f, double fc, double g) { const double r = f / fc; return g / (1.0 + r * r); }
double highShelfDb(double f, double fc, double g) { const double r = f / fc; return g * r * r / (1.0 + r * r); }

struct CabModel { double hpHz, lpHz, presHz, presDb, resHz, resDb; };
constexpr CabModel kCab[3] = {{120.0, 5200.0, 2600.0, 3.0, 150.0, 1.0},    // 1x12 (open back)
                              {90.0, 4900.0, 2800.0, 4.0, 105.0, 3.0},     // 2x12
                              {65.0, 4500.0, 3000.0, 5.0, 80.0, 5.0}};     // 4x12 (closed)
}

double modelMagnitude(double f, int cab, int mic, double distanceCm, double offAxisDeg) {
    const CabModel& c = kCab[std::clamp(cab, 0, 2)];
    double db = hpDb(f, c.hpHz, 3) + lpDb(f, c.lpHz, 3) + bellDb(f, c.presHz, c.presDb, 1.2) + bellDb(f, c.resHz, c.resDb, 1.5);
    double prox = 12.0 * std::exp(-distanceCm / 7.0);
    switch (std::clamp(mic, 0, 2)) {
        case 0: db += bellDb(f, 5000.0, 6.0, 1.2) + lpDb(f, 12000.0, 3); break;                                              // dynamic: presence peak
        case 1: db += lowShelfDb(f, 200.0, 3.0) + bellDb(f, 4000.0, -2.0, 0.8) + lpDb(f, 7000.0, 2) + hpDb(f, 40.0, 1); prox *= 1.6; break;   // ribbon
        default: db += highShelfDb(f, 7500.0, 8.0) + lpDb(f, 19000.0, 2); break;                                               // condenser: open top
    }
    db += lowShelfDb(f, 180.0, prox);
    db += highShelfDb(f, 2500.0, -0.16 * offAxisDeg);
    return std::pow(10.0, db / 20.0);
}

void IrDesigner::prepare(int fullLength, double fs) {
    fs_ = fs; full_ = fullLength; direct_ = std::max(1024, fullLength / 4);
    fft_.setup(4 * direct_);
    work_.assign(static_cast<size_t>(4 * direct_), 0.0);
    h_.assign(static_cast<size_t>(full_), 0.0);
    stage_ = 3;
}

void IrDesigner::begin(int cab, int mic, double distanceCm, double offAxisDeg, double roomPct) {
    cab_ = cab; mic_ = mic; dist_ = distanceCm; off_ = offAxisDeg; room_ = roomPct; stage_ = 0;
}

bool IrDesigner::step() {
    const int N = 4 * direct_;
    switch (stage_) {
        case 0: {   // level (mean of 200 Hz..4 kHz = 0 dB) and the log magnitude on the FFT grid
            double mean = 0; int cnt = 0;
            for (double f = 200.0; f <= 4000.0; f *= 1.05) { mean += 20.0 * std::log10(modelMagnitude(f, cab_, mic_, dist_, off_)); ++cnt; }
            norm_ = std::pow(10.0, -mean / cnt / 20.0);
            for (int k = 0; k <= N / 2; ++k) work_[static_cast<size_t>(k)] = std::log(std::max(norm_ * modelMagnitude(static_cast<double>(k) * fs_ / N, cab_, mic_, dist_, off_), 1e-6));
            for (int k = 1; k < N / 2; ++k) work_[static_cast<size_t>(N - k)] = work_[static_cast<size_t>(k)];
            stage_ = 1; return false;
        }
        case 1: {   // minimum phase from the real cepstrum (fold the anti-causal half onto the causal half)
            fft_.inverse(work_);
            for (int n = 1; n < N / 2; ++n) { work_[static_cast<size_t>(n)] *= 2.0; work_[static_cast<size_t>(N - n)] = 0.0; }
            fft_.forward(work_);
            for (auto& v : work_) v = std::exp(v);
            fft_.inverse(work_);
            stage_ = 2; return false;
        }
        case 2: {
            std::fill(h_.begin(), h_.end(), 0.0);
            double direct = 0;
            for (int n = 0; n < direct_; ++n) {
                const double d = static_cast<double>(n) / direct_;
                const double w = d < 0.75 ? 1.0 : 0.5 * (1.0 + std::cos(kPi * (d - 0.75) / 0.25));   // the last quarter fades out
                h_[static_cast<size_t>(n)] = work_[static_cast<size_t>(n)].real() * w;
                direct += h_[static_cast<size_t>(n)] * h_[static_cast<size_t>(n)];
            }
            if (room_ > 0.0) {   // decaying dark noise after 4 ms: energy = 0.35 x Room x the direct part's
                uint32_t rng = 0x2545f491u; double lp = 0, e = 0;
                const int pre = static_cast<int>(0.004 * fs_);
                for (int pass = 0; pass < 2; ++pass) {   // pass 0 measures the tail's energy, pass 1 adds it (same generator, nothing stored)
                    rng = 0x2545f491u; lp = 0;
                    const double gain = pass == 0 ? 0.0 : std::sqrt(0.35 * room_ * 0.01 * direct / std::max(e, 1e-30));
                    for (int n = pre; n < full_; ++n) {
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        lp += 0.45 * ((rng / 2147483648.0 - 1.0) - lp);
                        const double ramp = std::min(1.0, (n - pre) / (0.004 * fs_));
                        const double v = lp * ramp * std::exp(-static_cast<double>(n - pre) / (0.06 * fs_));
                        if (pass == 0) e += v * v; else h_[static_cast<size_t>(n)] += gain * v;
                    }
                }
            }
            stage_ = 3; return true;
        }
        default: return true;
    }
}

std::vector<double> designIr(int length, double fs, int cab, int mic, double distanceCm, double offAxisDeg, double roomPct) {
    IrDesigner d; d.prepare(length, fs); d.begin(cab, mic, distanceCm, offAxisDeg, roomPct);
    while (!d.step()) {}
    return d.ir();
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::rebuild(bool immediate) {
    designer_.begin(static_cast<int>(target_[Cab] + 0.5), static_cast<int>(target_[Mic] + 0.5), target_[MicDistance], target_[OffAxis], target_[Room]);
    while (!designer_.step()) {}
    for (auto& c : conv_) c.setKernel(designer_.ir(), immediate);
    designing_ = false;
}

void Processor::prepare(double sampleRate, int) {
    clock_.reset(); ticks_ = 0;
    fs_ = sampleRate;
    len_ = 8192; while (len_ < 8192.0 * fs_ / 48000.0) len_ *= 2;
    for (size_t c = 0; c < conv_.size(); ++c) conv_[c].prepare(len_, static_cast<int>(0.02 * fs_), static_cast<int>(c));
    designer_.prepare(len_, fs_);
    rebuild(true);
    lowCutOn_ = target_[LowCut] > specs()[LowCut].min * 1.0001;
    for (auto& f : hp_) f.setup(Svf::Mode::HighPass, target_[LowCut], fs_, 0.70710678, 0);
    dirty_ = false; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    if (id == LowCut) {
        lowCutOn_ = v > sp.min * 1.0001;
        for (auto& f : hp_) f.setupRamp(Svf::Mode::HighPass, v, fs_, 0.70710678, 0, static_cast<int>(0.01 * fs_));
    } else dirty_ = true;
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    rebuild(true); dirty_ = false;
    for (auto& f : hp_) f.setup(Svf::Mode::HighPass, target_[LowCut], fs_, 0.70710678, 0);
}

// at every grid point of the stream: a new design starts (when the last IR has faded in), or the one being designed takes a step (every 4 points = 256 samples; about a millisecond each)
void Processor::gridTick() {
    if (designing_) {
        if (++ticks_ >= GridClock::kStepTicks) { ticks_ = 0; if (designer_.step()) { for (auto& c : conv_) c.setKernel(designer_.ir(), false); designing_ = false; } }
    } else if (dirty_ && !conv_[0].fading() && !conv_[1].fading()) {
        designer_.begin(static_cast<int>(target_[Cab] + 0.5), static_cast<int>(target_[Mic] + 0.5), target_[MicDistance], target_[OffAxis], target_[Room]);
        designing_ = true; dirty_ = false; ticks_ = 0;
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    for (int off = 0; off < n;) {
        const int len = std::min(n - off, clock_.toNext());
        float* seg[2] = {ch[0] + off, nch > 1 ? ch[1] + off : nullptr};
        processSegment(seg, nch, len);
        off += len;
        if (clock_.advance(len)) gridTick();
    }
}

void Processor::processSegment(float** ch, int nch, int n) {
    for (int c = 0; c < nch; ++c) conv_[static_cast<size_t>(c)].process(ch[c], n);
    if (lowCutOn_) for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(hp_[static_cast<size_t>(c)].process(ch[c][i]));
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) if (std::abs(ch[c][i]) < 1e-30f) ch[c][i] = 0.0f;
}

// the tail: the length of the impulse response (cabinet and room), plus a little
double Processor::tailSeconds() const { return fs_ > 0.0 ? static_cast<double>(len_) / fs_ + 0.1 : 0.0; }

void Processor::reset() {
    for (auto& c : conv_) c.reset();
    for (auto& f : hp_) f.reset();
}

}  // namespace sw::gt02
