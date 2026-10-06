#include "st03/st03.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::st03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st03.delay",    "Delay",    0, 20, 0,      Curve::Skew, 2, {}, "ms"},
        {"st03.phase",    "Phase",    -180, 180, 0,  Curve::Lin, 1, {}, "deg"},
        {"st03.polarity", "Polarity", 0, 1, 0,       Curve::Step, 1, {0, 1}, "", {"Normal", "Invert"}},
        {"st03.mix",      "Mix",      0, 100, 100,   Curve::Lin, 1, {}, "%"},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
double roundDelayMs(double ms) { return std::round(ms * 100.0) / 100.0; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.03 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& h : hil_) h.reset();
    capMain_.assign(static_cast<size_t>(kCollectSeconds * fs_), 0.0f); capRef_ = capMain_;
    pos_ = 0; state_ = Idle; collected_ = 0; pending_ = 0; rotMix_ = 0.0;
    snapToTargets();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {
    delay_ = roundDelayMs(target_[Delay]) * 0.001 * fs_;
    rotMix_ = std::abs(target_[Phase]) >= 0.05 ? 1.0 : 0.0;
    phaseT_ = target_[Phase]; cosT_ = std::cos(phaseT_ * kPi / 180.0); sinT_ = std::sin(phaseT_ * kPi / 180.0);
}

double Processor::read(int c, double d) const {
    const auto& b = buf_[static_cast<size_t>(c)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double delayT = roundDelayMs(target_[Delay]) * 0.001 * fs_, phaseTarget = target_[Phase];
    const double glide = 1.0 - std::exp(-1.0 / (0.02 * fs_));
    const bool inv = target_[Polarity] > 0.5;
    cosT_ = std::cos(phaseTarget * kPi / 180.0); sinT_ = std::sin(phaseTarget * kPi / 180.0); phaseT_ = phaseTarget;
    const bool rotWanted = std::abs(phaseTarget) >= 0.05;
    const double blendK = 1.0 - std::exp(-1.0 / (0.02 * fs_));
    for (int i = 0; i < n; ++i) {
        delay_ += glide * (delayT - delay_);
        rotMix_ += blendK * ((rotWanted ? 1.0 : 0.0) - rotMix_);
        if (state_ == Collecting && collected_ < capMain_.size()) {
            double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i], r = 0.0;
            if (sc && scCh > 0) { r = sc[0][i]; if (scCh > 1) r = 0.5 * (sc[0][i] + sc[1][i]); }
            capMain_[collected_] = static_cast<float>(m); capRef_[collected_] = static_cast<float>(r);
            if (++collected_ >= capMain_.size()) state_ = Ready;
        }
        for (int c = 0; c < nch; ++c) buf_[static_cast<size_t>(c)][pos_ & mask_] = ch[c][i];
        ++pos_;
        for (int c = 0; c < nch; ++c) {
            double y = delay_ > 0.5 ? read(c, std::max(3.0, delay_ + 1.0)) : static_cast<double>(ch[c][i]);
            if (rotWanted || rotMix_ > 1e-6) { double I, Q; hil_[static_cast<size_t>(c)].process(y, I, Q); const double yr = cosT_ * I + sinT_ * Q; y += rotMix_ * (yr - y); }
            if (inv) y = -y;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

void Processor::startAutoAlign() { if (!prepared_) return; collected_ = 0; confidence_ = 0.0; state_ = Collecting; pending_ = 0; }

bool Processor::analyse() {
    if (state_ != Ready) return false;
    const size_t N = capMain_.size(), maxLag = static_cast<size_t>(kMaxDelayMs * 0.001 * fs_) + 1;
    // lag with the largest |cross-correlation|: r[tau] = sum x[n - tau] y[n], by FFT (zero padded)
    size_t M = 1; while (M < N + maxLag + 2) M <<= 1;
    std::vector<std::complex<double>> X(M), Y(M);
    double ex = 0, ey = 0;
    for (size_t i = 0; i < N; ++i) { X[i] = capMain_[i]; Y[i] = capRef_[i]; ex += double(capMain_[i]) * capMain_[i]; ey += double(capRef_[i]) * capRef_[i]; }
    if (ex < 1e-12 || ey < 1e-12) { state_ = Failed; return false; }
    Fft fft(static_cast<int>(M)); fft.forward(X); fft.forward(Y);
    for (size_t k = 0; k < M; ++k) X[k] = std::conj(X[k]) * Y[k];   // r[tau] = sum x[n] y[n + tau]: the reference LATE by tau
    fft.inverse(X);
    size_t best = 0; double bv = 0;
    for (size_t t = 0; t <= maxLag && t < M; ++t) { const double v = std::abs(X[t].real()); if (v > bv) { bv = v; best = t; } }
    confidence_ = bv / std::sqrt(ex * ey);
    if (confidence_ < kMinConfidence) { state_ = Failed; return false; }
    // the track as the processor would delay it, through the Hilbert pair: A = sum(i y), B = sum(q y) (low band only); the rotator's best output is sqrt(A^2 + B^2) at phi = atan2(B, A)
    const double a = std::exp(-2.0 * kPi * kLowBandHz / fs_);
    HilbertIir h; double A = 0, B = 0, C0 = 0;   // C0: the track as it is (Phase 0 bypasses the pair) against the reference
    double s1 = 0, s2 = 0, u1 = 0, u2 = 0, t1 = 0, t2 = 0;
    for (size_t i = 0; i < N; ++i) {
        const double xd = i >= best ? capMain_[i - best] : 0.0;
        s1 = (1 - a) * xd + a * s1; s2 = (1 - a) * s1 + a * s2;                       // the delayed track, low band
        t1 = (1 - a) * capRef_[i] + a * t1; t2 = (1 - a) * t1 + a * t2;                // the reference, low band
        double I, Q; h.process(s2, I, Q);
        if (i >= 4000) { A += I * t2; B += Q * t2; C0 += s2 * t2; }
        (void)u1; (void)u2;
    }
    const double rot = std::sqrt(A * A + B * B);
    foundMs_ = std::round(static_cast<double>(best) / fs_ * 1000.0 * 100.0) / 100.0;
    foundDeg_ = 0.0; foundInvert_ = false;
    if (rot > std::abs(C0)) foundDeg_ = std::atan2(B, A) * 180.0 / kPi;   // the rotator beats both the bypass and its inversion
    else foundInvert_ = C0 < 0.0;
    setParam(Delay, foundMs_); setParam(Phase, foundDeg_); setParam(Polarity, foundInvert_ ? 1.0 : 0.0);
    snapToTargets();
    pending_ = 3; state_ = Done;
    return true;
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (pending_ == 3) { id = Delay; plain = target_[Delay]; pending_ = 2; return 7; }
    if (pending_ == 2) { id = Phase; plain = target_[Phase]; pending_ = 1; return 7; }
    if (pending_ == 1) { id = Polarity; plain = target_[Polarity]; pending_ = 0; return 7; }
    return 0;
}

}  // namespace sw::st03
