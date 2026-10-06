#include "rs01/rs01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rs01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rs01.profile",   "Profile",    0, 2, 0,     Curve::Step, 1, {0, 1, 2}, "", {"Voice", "Music", "Field"}},
        {"rs01.evo.on",    "Adaptive",   0, 1, 1,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rs01.reduction", "Reduction",  -40, 0, -12, Curve::Lin, 1, {}, "dB"},
        {"rs01.thresh",    "Threshold",  -10, 20, 3,  Curve::Lin, 1, {}, "dB"},
        {"rs01.smooth",    "Smoothing",  0, 2, 1,     Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"rs01.low",       "Low band",   -20, 20, 0,  Curve::Lin, 1, {}, "dB"},
        {"rs01.high",      "High band",  -20, 20, 0,  Curve::Lin, 1, {}, "dB"},
        {"rs01.guard",     "Artifact guard", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rs01.learn",     "Learn",      0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

namespace {
constexpr double kSubSec[3] = {0.2, 0.4, 0.25}, kAlpha[3] = {0.93, 0.96, 0.98}, kOverSub[3] = {1.0, 1.0, 1.15}, kBias = 1.5;
constexpr int kSubs = 8;
double ramp(double f, double full, double gone) { return f <= full ? 1.0 : (f >= gone ? 0.0 : 1.0 - (f - full) / (gone - full)); }   // 1 at/below `full`, 0 at/above `gone`
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    stft_.prepare(kFft, kHop, 2);
    const size_t nb = kFft / 2 + 1;
    for (auto& c : ch_) {
        c.smooth.assign(nb, 0.0); c.sub.assign(nb, 1e30); c.noise.assign(nb, 0.0); c.learnSum.assign(nb, 0.0); c.gainPrev.assign(nb, 1.0); c.prevSig.assign(nb, 0.0); c.gainSmooth.assign(nb, 1.0);
        c.mins.assign(kSubs, std::vector<double>(nb, 1e30)); c.subPos = 0; c.subCount = 0; c.learnN = 0;
    }
    bandAdj_.assign(nb, 0.0); tmpG_.assign(nb, 1.0);
    frameNo_ = 0; prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

double Processor::noiseDb(int bin) const { const double n = ch_[0].noise[static_cast<size_t>(bin)]; return n > 1e-30 ? 10.0 * std::log10(n) : -300.0; }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    stft_.process(ch, std::min(numCh, 2), n, *this);
    for (int c = 0; c < std::min(numCh, 2); ++c) for (int i = 0; i < n; ++i) if (std::abs(ch[c][i]) < 1e-30f || !std::isfinite(ch[c][i])) ch[c][i] = 0.0f;
}

void Processor::frame(std::complex<double>* const* spec, int nch, int nbins) {
    const int prof = std::clamp(static_cast<int>(target_[Profile] + 0.5), 0, 2), sm = std::clamp(static_cast<int>(target_[Smoothing] + 0.5), 0, 2);
    const bool adaptive = target_[Adaptive] > 0.5, learn = target_[Learn] > 0.5, guard = target_[Guard] > 0.5;
    const double alpha = kAlpha[sm], over = std::pow(10.0, target_[Threshold] / 10.0) * kOverSub[prof];
    const int subLen = std::max(1, static_cast<int>(std::lround(kSubSec[prof] * fs_ / kHop)));
    const double depth = target_[Reduction], lowAdj = target_[LowBand], highAdj = target_[HighBand];
    for (int k = 0; k < nbins; ++k) { const double f = k * fs_ / kFft; bandAdj_[static_cast<size_t>(k)] = lowAdj * ramp(f, 400.0, 1200.0) + highAdj * (1.0 - ramp(f, 1500.0, 4000.0)); }
    for (int c = 0; c < nch; ++c) {
        auto& ch = ch_[static_cast<size_t>(c)];
        std::complex<double>* X = spec[c];
        // noise estimate
        for (int k = 0; k < nbins; ++k) {
            const double P = std::norm(X[k]);
            double& s = ch.smooth[static_cast<size_t>(k)];
            s = frameNo_ == 0 ? P : 0.8 * s + 0.2 * P;
            if (learn) ch.learnSum[static_cast<size_t>(k)] += P;
            if (adaptive && !learn) ch.sub[static_cast<size_t>(k)] = std::min(ch.sub[static_cast<size_t>(k)], s);
        }
        if (learn) {
            ++ch.learnN;
            for (int k = 0; k < nbins; ++k) ch.noise[static_cast<size_t>(k)] = ch.learnSum[static_cast<size_t>(k)] / ch.learnN;
        } else {
            if (ch.learnN > 0 && !learn) { std::fill(ch.learnSum.begin(), ch.learnSum.end(), 0.0); ch.learnN = 0; }
            if (adaptive) {
                if (++ch.subCount >= subLen) {
                    ch.subCount = 0;
                    ch.mins[static_cast<size_t>(ch.subPos)] = ch.sub; ch.subPos = (ch.subPos + 1) % kSubs;
                    std::fill(ch.sub.begin(), ch.sub.end(), 1e30);
                }
                for (int k = 0; k < nbins; ++k) {
                    double m = ch.sub[static_cast<size_t>(k)];
                    for (int j = 0; j < kSubs; ++j) m = std::min(m, ch.mins[static_cast<size_t>(j)][static_cast<size_t>(k)]);
                    if (m < 1e29) ch.noise[static_cast<size_t>(k)] = kBias * m;
                }
            }
        }
        // gain
        for (int k = 0; k < nbins; ++k) {
            const size_t i = static_cast<size_t>(k);
            const double P = std::norm(X[k]), lambda = ch.noise[i] * over;
            double G = 1.0;
            if (lambda > 1e-30) {
                const double gamma = P / lambda, xi = alpha * ch.prevSig[i] / lambda + (1.0 - alpha) * std::max(gamma - 1.0, 0.0);
                G = xi / (1.0 + xi);
            }
            tmpG_[i] = G;
        }
        if (guard) {
            for (int k = 0; k < nbins; ++k) { double& g = ch.gainSmooth[static_cast<size_t>(k)]; const double t = tmpG_[static_cast<size_t>(k)]; g = t > g ? t : g + 0.3 * (t - g); }
            for (int k = 0; k < nbins; ++k) {
                const double a = ch.gainSmooth[static_cast<size_t>(std::max(0, k - 1))], b = ch.gainSmooth[static_cast<size_t>(k)], d = ch.gainSmooth[static_cast<size_t>(std::min(nbins - 1, k + 1))];
                tmpG_[static_cast<size_t>(k)] = 0.25 * a + 0.5 * b + 0.25 * d;
            }
        } else {
            for (int k = 0; k < nbins; ++k) ch.gainSmooth[static_cast<size_t>(k)] = tmpG_[static_cast<size_t>(k)];
        }
        for (int k = 0; k < nbins; ++k) {
            const size_t i = static_cast<size_t>(k);
            const double floorDb = std::clamp(std::min(0.0, depth - bandAdj_[i]), -60.0, 0.0), fl = std::pow(10.0, floorDb / 20.0);
            const double G = std::clamp(tmpG_[i], fl, 1.0);
            ch.prevSig[i] = G * G * std::norm(X[k]);
            X[k] *= G;
        }
    }
    ++frameNo_;
}

}  // namespace sw::rs01
