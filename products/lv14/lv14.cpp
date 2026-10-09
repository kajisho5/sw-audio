#include "lv14/lv14.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::lv14 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv14.delay",    "Delay",    0, 500, 0,   Curve::Skew, 2, {}, "ms"},
        {"lv14.temp",     "Air temp", -10, 40, 22, Curve::Lin, 1, {}, "degC"},
        {"lv14.polarity", "Polarity", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Normal", "Invert"}},
    };
    return s;
}
double speedOfSound(double t) { return 331.5 + 0.6 * t; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 1; while (sz < static_cast<size_t>(kMaxDelayMs * 0.001 * fs_) + 4096) sz <<= 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    mask_ = sz - 1; pos_ = 0; cur_ = target_[Delay] * 0.001 * fs_;
    need_ = static_cast<size_t>(kCollectSeconds * fs_); capRef_.assign(need_, 0.0f); capMic_.assign(need_, 0.0f);
    state_ = Idle; collected_ = 0; writePending_ = false; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::startMeasure() { if (prepared_) { state_ = Collecting; collected_ = 0; writePending_ = false; } }

void Processor::processWithSidechain(float** ch, int numCh, int n, const float* const* sc, int scCh) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const double goal = target_[Delay] * 0.001 * fs_, a = 1.0 - std::exp(-1.0 / (0.05 * fs_)), sign = target_[Polarity] > 0.5 ? -1.0 : 1.0;
    const bool collect = state_ == Collecting;
    for (int i = 0; i < n; ++i) {
        if (collect && collected_ < need_) {
            double m = 0; for (int c = 0; c < nc; ++c) m += ch[c][i]; capMic_[collected_] = static_cast<float>(m / nc);
            double r = 0; if (sc && scCh > 0) { for (int c = 0; c < std::min(scCh, 2); ++c) r += sc[c][i]; r /= std::min(scCh, 2); } capRef_[collected_] = static_cast<float>(r);
            if (++collected_ >= need_) state_ = Ready;
        }
        cur_ += a * (goal - cur_); if (std::abs(goal - cur_) < 1e-4) cur_ = goal;
        const size_t w = pos_ & mask_;
        const double rp = static_cast<double>(pos_) - cur_; const double fl = std::floor(rp), fr = rp - fl; const size_t i0 = static_cast<size_t>(static_cast<long long>(fl)) & mask_, i1 = (i0 + 1) & mask_;
        for (int c = 0; c < nc; ++c) {
            auto& b = buf_[static_cast<size_t>(c)]; b[w] = ch[c][i];
            double y = cur_ == 0.0 ? static_cast<double>(ch[c][i]) : (1.0 - fr) * b[i0] + fr * b[i1];
            if (cur_ != 0.0 && fr == 0.0) y = b[i0];
            y *= sign; const float o = static_cast<float>(y); ch[c][i] = std::abs(o) < 1e-30f ? 0.0f : o;
        }
        ++pos_;
    }
}

bool Processor::analyse() {
    if (state_ != Ready) return false;
    const size_t N = need_; size_t L = 1; while (L < 2 * N) L <<= 1;
    Fft fft(static_cast<int>(L)); std::vector<std::complex<double>> A(L), B(L);
    double eR = 0, eM = 0;
    for (size_t i = 0; i < N; ++i) { A[i] = capRef_[i]; B[i] = capMic_[i]; eR += double(capRef_[i]) * capRef_[i]; eM += double(capMic_[i]) * capMic_[i]; }
    if (eR < 1e-9 || eM < 1e-9) { state_ = Failed; confidence_ = 0; return false; }
    fft.forward(A); fft.forward(B);
    for (size_t k = 0; k < L; ++k) A[k] = std::conj(A[k]) * B[k];   // r[lag] = sum ref[n] mic[n + lag]
    fft.inverse(A);
    const size_t maxLag = static_cast<size_t>(kMaxDelayMs * 0.001 * fs_);
    size_t best = 0; double bv = -1e30; for (size_t l = 0; l <= maxLag; ++l) if (A[l].real() > bv) { bv = A[l].real(); best = l; }
    double second = 0; for (size_t l = 0; l <= maxLag; ++l) if ((l + 3 < best || l > best + 3)) second = std::max(second, A[l].real());
    confidence_ = bv / std::sqrt(eR * eM);
    if (confidence_ < 0.1 || bv < 2.0 * second) { state_ = Failed; return false; }
    double off = 0; if (best > 0 && best < maxLag) { const double y0 = A[best - 1].real(), y1 = A[best].real(), y2 = A[best + 1].real(), d = y0 - 2 * y1 + y2; if (std::abs(d) > 1e-12) off = std::clamp(0.5 * (y0 - y2) / d, -0.5, 0.5); }
    foundMs_ = (static_cast<double>(best) + off) / fs_ * 1000.0; state_ = Done; writePending_ = true;
    return true;
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (!writePending_) return 0;
    writePending_ = false; id = Delay; plain = std::clamp(std::round(foundMs_ * 100.0) / 100.0, 0.0, kMaxDelayMs); return 7;
}

// the delay it makes on purpose is still coming out after the input has stopped: the tail is that delay
double Processor::tailSeconds() const { return target_[Delay] * 0.001 + 0.1; }

}  // namespace sw::lv14
