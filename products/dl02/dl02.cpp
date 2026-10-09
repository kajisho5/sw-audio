#include "dl02/dl02.hpp"
#include <algorithm>
#include <cmath>

namespace sw::dl02 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"dl02.heads",     "Heads",     0, 5, 3,     Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"1", "2", "3", "1+2", "2+3", "All"}},
        {"dl02.rate",      "Rate",      50, 200, 100, Curve::Log, 1, {}, "ms", {}, nullptr, nullptr, 1.0, true, true},
        {"dl02.intensity", "Intensity", 0, 10, 4,    Curve::Lin, 1, {}, ""},
        {"dl02.bass",      "Bass",      -6, 6, 0,    Curve::Lin, 1, {}, "dB"},
        {"dl02.treble",    "Treble",    -6, 6, 0,    Curve::Lin, 1, {}, "dB"},
        {"dl02.wear",      "Wear",      0, 10, 3,    Curve::Lin, 1, {}, ""},
        {"dl02.mix",       "Mix",       0, 100, 25,  Curve::Lin, 1, {}, "%"},
            unitSpec("dl02.unit"),
    };
    return s;
}
int headMask(int heads) { static const int m[6] = {1, 2, 4, 3, 6, 7}; return m[std::clamp(heads, 0, 5)]; }

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::setTransport(bool playing, double beatsToNextBar) {
    barKnown_ = playing && beatsToNextBar >= 0.0 && bpm_ > 0.0;
    if (barKnown_) countdown_ = beatsToNextBar * 60.0 / bpm_ * fs_;
}

void Processor::setTone() {
    const double ms = target_[Rate], wear = target_[Wear];
    const double cut = std::clamp(9000.0 * std::sqrt(100.0 / ms) * (1.0 - 0.06 * wear), 500.0, 0.45 * fs_);
    for (auto& c : ch_) {
        c.lp.setup(Svf::Mode::LowPass, cut, fs_, 0.7071, 0.0);
        c.bass.setup(Svf::Mode::LowShelf, 200.0, fs_, 0.7071, target_[Bass]);
        c.treble.setup(Svf::Mode::HighShelf, 3000.0, fs_, 0.7071, target_[Treble]);
    }
    toneRate_ = ms; toneWear_ = wear; toneBass_ = target_[Bass]; toneTreble_ = target_[Treble];
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.8 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    for (auto& c : ch_) { c.lp.reset(); c.bass.reset(); c.treble.reset(); }
    pos_ = 0; t_ = target_[Rate] * 0.001 * fs_; dropGain_ = dropTarget_ = 1.0; dropLeft_ = 0;
    applied_ = headMask(static_cast<int>(target_[Heads] + 0.5));
    for (size_t k = 0; k < 3; ++k) g_[k] = (applied_ >> k) & 1;
    setTone();
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    t_ = target_[Rate] * 0.001 * fs_;
    applied_ = headMask(static_cast<int>(target_[Heads] + 0.5));
    for (size_t k = 0; k < 3; ++k) g_[k] = (applied_ >> k) & 1;
    setTone();
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

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    if (target_[Rate] != toneRate_ || target_[Wear] != toneWear_ || target_[Bass] != toneBass_ || target_[Treble] != toneTreble_) setTone();
    const double fb = target_[Intensity] * 0.11, wear = target_[Wear];
    const double wowAmp = 0.12 * wear * 0.001 * fs_, flAmp = 0.006 * wear * 0.001 * fs_;
    const double tgt = target_[Rate] * 0.001 * fs_;
    const double glide = 1.0 - std::exp(-1.0 / (0.15 * fs_)), headStep = 1.0 / (0.01 * fs_), dgSmooth = 1.0 - std::exp(-1.0 / (0.003 * fs_));
    const double dropRate = std::max(0.0, wear - 2.0) * 0.375 / fs_;
    const int wantMask = headMask(static_cast<int>(target_[Heads] + 0.5));
    const double inc[3] = {0.55 / fs_, 1.37 / fs_, 9.1 / fs_};
    for (int i = 0; i < n; ++i) {
        if (wantMask != applied_ && (!barKnown_ || countdown_ <= 0.0)) applied_ = wantMask;
        if (barKnown_) countdown_ -= 1.0;
        for (size_t k = 0; k < 3; ++k) { const double want = (applied_ >> k) & 1; g_[k] += std::clamp(want - g_[k], -headStep, headStep); }
        t_ += glide * (tgt - t_);
        for (int k = 0; k < 3; ++k) { ph_[k] += inc[k]; if (ph_[k] >= 1.0) ph_[k] -= 1.0; }
        if (dropLeft_ > 0) { if (--dropLeft_ == 0) dropTarget_ = 1.0; }
        else if (dropRate > 0.0 && (rnd() >> 8) * (1.0 / 16777216.0) < dropRate) {
            dropLeft_ = static_cast<long>((0.02 + 0.04 * ((rnd() >> 8) * (1.0 / 16777216.0))) * fs_);
            dropTarget_ = std::pow(10.0, -(4.0 + 6.0 * ((rnd() >> 8) * (1.0 / 16777216.0))) / 20.0);
        }
        dropGain_ += dgSmooth * (dropTarget_ - dropGain_);
        const double gs = g_[0] + g_[1] + g_[2];
        const double outNorm = 1.0 / std::sqrt(std::max(1.0, gs)), fbNorm = 1.0 / std::max(1.0, gs);
        double echo[2] = {0.0, 0.0}, mean[2] = {0.0, 0.0};
        for (int c = 0; c < 2; ++c) {
            const double off = c == 0 ? 0.0 : 0.2;
            auto lfo = [&](int k) { double p = ph_[k] + off * (k + 1); p -= std::floor(p); return std::sin(2.0 * kPi * p); };
            const double mod = wowAmp * (0.7 * lfo(0) + 0.3 * lfo(1)) + flAmp * lfo(2);
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) if (g_[static_cast<size_t>(k)] > 0.0) sum += g_[static_cast<size_t>(k)] * read(c, (k + 1) * (t_ + mod));
            echo[c] = sum * outNorm * dropGain_; mean[c] = sum * fbNorm * dropGain_;
        }
        if (nch == 1) { echo[1] = echo[0]; mean[1] = mean[0]; }
        for (int c = 0; c < 2; ++c) {
            const double in = c < nch ? ch[c][i] : ch[0][i];
            double x = in + fb * mean[c];
            x = std::tanh(x);
            auto& cc = ch_[static_cast<size_t>(c)];
            x = cc.lp.process(x);
            x = cc.bass.process(x);
            x = cc.treble.process(x);
            buf_[static_cast<size_t>(c)][pos_ & mask_] = static_cast<float>(x);
        }
        ++pos_;
        for (int c = 0; c < nch; ++c) { double y = echo[c]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::dl02
