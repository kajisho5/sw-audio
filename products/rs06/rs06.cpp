#include "rs06/rs06.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs06 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rs06.reduction", "Reduction",  -30, 0, -10, Curve::Lin, 1, {}, "dB"},
        {"rs06.tail",      "Tail length", 0.1, 5, 0.8, Curve::Log, 1, {}, "s"},
        {"rs06.early",     "Early",      0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Keep", "Reduce"}},
        {"rs06.smooth",    "Smooth",     0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs06.learn",     "Learn room", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

namespace {
constexpr double kRelease[3] = {0.5, 0.7, 0.85}, kPsAlpha = 0.6;
constexpr int kLearnWin = 32, kMaxCand = 64;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    stft_.prepare(kFft, kHop, 2);
    const size_t nb = kFft / 2 + 1;
    for (auto& c : ch_) { c.ps.assign(nb, 0.0); c.lam.assign(nb, 0.0); c.gs.assign(nb, 1.0); c.hist.assign(kHist, std::vector<double>(nb, 0.0)); c.pos = 0; }
    g_.assign(nb, 1.0); tmp_.assign(nb, 1.0);
    levels_.assign(kLearnWin + 1, 0.0); cand_.clear(); cand_.reserve(kMaxCand + 1);
    lpos_ = 0; lcount_ = 0; pending_ = 0; found_ = 0.0; learning_ = false; frameNo_ = 0; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Learn) {
        const bool on = v > 0.5;
        if (on && !learning_) { cand_.clear(); lcount_ = 0; lpos_ = 0; }
        if (!on && learning_ && !cand_.empty()) {
            std::vector<double> c = cand_; std::nth_element(c.begin(), c.begin() + static_cast<long>(c.size() / 2), c.end());
            found_ = std::clamp(c[c.size() / 2], 0.1, 5.0); pending_ = 1; target_[Tail] = found_;
        }
        learning_ = on;
    }
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (!pending_) return 0;
    pending_ = 0; id = Tail; plain = found_; return 7;
}

void Processor::learnFrame(double dB) {
    // the last kLearnWin + 1 levels (a ring); a candidate when they fall by 12 dB or more without rising by more than 1 dB in a step
    levels_[static_cast<size_t>(lpos_)] = dB; lpos_ = (lpos_ + 1) % (kLearnWin + 1); if (lcount_ < kLearnWin + 1) ++lcount_;
    if (lcount_ < kLearnWin + 1) return;
    double first = levels_[static_cast<size_t>(lpos_)], prev = first, sx = 0, sy = 0, sxy = 0, sxx = 0; bool ok = true;
    const int n = kLearnWin + 1;
    for (int i = 0; i < n; ++i) {
        const double v = levels_[static_cast<size_t>((lpos_ + i) % n)];
        if (i > 0 && v > prev + 1.0) ok = false;
        prev = v; sx += i; sy += v; sxy += i * v; sxx += static_cast<double>(i) * i;
    }
    if (!ok || first - prev < 12.0 || prev < -90.0) return;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);   // dB per frame
    if (slope >= -0.05) return;
    const double t60 = -60.0 / slope * (static_cast<double>(kHop) / fs_);
    if (t60 >= 0.1 && t60 <= 5.0 && static_cast<int>(cand_.size()) < kMaxCand) cand_.push_back(t60);
    lcount_ = 0;   // the next candidate needs a fresh window
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    stft_.process(ch, std::min(numCh, 2), n, *this);
    for (int c = 0; c < std::min(numCh, 2); ++c) for (int i = 0; i < n; ++i) if (std::abs(ch[c][i]) < 1e-30f || !std::isfinite(ch[c][i])) ch[c][i] = 0.0f;
}

void Processor::frame(std::complex<double>* const* spec, int nch, int nbins) {
    const double h = static_cast<double>(kHop) / fs_, tail = std::max(0.05, target_[Tail]);
    const double rho = std::pow(10.0, -6.0 * h / tail);
    const int D = std::min(kHist - 1, target_[Early] > 0.5 ? std::max(1, static_cast<int>(std::lround(0.020 / h))) : std::max(1, static_cast<int>(std::lround(0.050 / h))));
    const double rhoD = std::pow(rho, D), floorG = std::pow(10.0, target_[Reduction] / 20.0), rel = kRelease[std::clamp(static_cast<int>(target_[Smooth] + 0.5), 0, 2)];
    const bool freqSmooth = target_[Smooth] > 0.5;
    double total = 0.0;
    for (int c = 0; c < nch; ++c) {
        auto& ch = ch_[static_cast<size_t>(c)];
        std::complex<double>* X = spec[c];
        auto& old = ch.hist[static_cast<size_t>((ch.pos + kHist - D) % kHist)];   // Ps of D frames ago
        for (int k = 0; k < nbins; ++k) {
            const size_t i = static_cast<size_t>(k);
            const double P = std::norm(X[k]);
            ch.ps[i] = frameNo_ == 0 ? P : kPsAlpha * ch.ps[i] + (1.0 - kPsAlpha) * P;
            ch.lam[i] = std::max(rho * ch.lam[i], rhoD * old[i]);
            const double xi = std::max(P / (ch.lam[i] + 1e-30) - 1.0, 0.0);
            tmp_[i] = xi / (1.0 + xi);
            total += P;
        }
        ch.hist[static_cast<size_t>(ch.pos)] = ch.ps; ch.pos = (ch.pos + 1) % kHist;
        for (int k = 0; k < nbins; ++k) { double& g = ch.gs[static_cast<size_t>(k)]; const double t = tmp_[static_cast<size_t>(k)]; g = t > g ? t : rel * g + (1.0 - rel) * t; tmp_[static_cast<size_t>(k)] = g; }
        for (int k = 0; k < nbins; ++k) {
            double g = tmp_[static_cast<size_t>(k)];
            if (freqSmooth) g = 0.25 * tmp_[static_cast<size_t>(std::max(0, k - 1))] + 0.5 * g + 0.25 * tmp_[static_cast<size_t>(std::min(nbins - 1, k + 1))];
            X[k] *= std::clamp(g, floorG, 1.0);
        }
    }
    if (learning_) learnFrame(total > 1e-20 ? 10.0 * std::log10(total / std::max(1, nch)) : -200.0);
    ++frameNo_;
}

}  // namespace sw::rs06
