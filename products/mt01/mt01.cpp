#include "mt01/mt01.hpp"
#include <algorithm>
#include <cmath>

namespace sw::mt01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"mt01.preset", "Preset",    0, 3, 0,    Curve::Step, 1, {0, 1, 2, 3}, "", {"ARIB TR-B32 -24", "EBU R128 -23", "Streaming -14", "Custom"}},
        {"mt01.target", "Target",    -40, -5, -24, Curve::Lin, 1, {}, "LUFS"},
        {"mt01.tol",    "Tolerance", 0.5, 3, 1,  Curve::Lin, 1, {}, "LU"},
        {"mt01.pause",  "Pause",     0, 1, 0,    Curve::Step, 1, {0, 1}, "", {"Off", "On"}, nullptr, nullptr, 1.0, false},
    };
    return s;
}

double presetTarget(int p, double custom) { static const double t[3] = {-24.0, -23.0, -14.0}; return p >= 0 && p < 3 ? t[p] : custom; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    meter_.setup(fs_, 2); integ_.setup(fs_, 2, 0.0);
    for (auto& t : tpd_) t.setup(4);
    lra_.reset(); history_.clear(); history_.reserve(kHistory + 1);
    reset(); prepared_ = true;
}

void Processor::reset() {
    meter_.reset(); integ_.reset(); for (auto& t : tpd_) t.reset();
    lra_.reset(); history_.clear();
    momentary_ = shortTerm_ = integrated_ = -200.0; tp_ = 0.0; sinceHist_ = sinceLra_ = 0;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

double Processor::truePeakDb() const { return tp_ > 1e-9 ? 20.0 * std::log10(tp_) : -200.0; }

double Processor::range() const { return lra_.range(); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || target_[Pause] > 0.5 || n <= 0) return;
    const int nch = std::min(numCh, 2);
    const float* in[2] = {ch[0], ch[nch - 1]};
    meter_.process(in, nch, n); integ_.process(in, nch, n);
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) tp_ = std::max(tp_, tpd_[static_cast<size_t>(c)].process(ch[c][i]));
    momentary_ = meter_.momentary(); shortTerm_ = meter_.shortTerm(); integrated_ = integ_.integrated();
    sinceLra_ += n; sinceHist_ += n;
    while (sinceLra_ >= static_cast<int>(0.1 * fs_)) {   // a short-term value every 100 ms, once 3 s are there
        sinceLra_ -= static_cast<int>(0.1 * fs_);
        if (integ_.blocks() >= 30) lra_.add(shortTerm_);
    }
    while (sinceHist_ >= static_cast<int>(fs_)) { sinceHist_ -= static_cast<int>(fs_); history_.push_back(static_cast<float>(shortTerm_)); if (static_cast<int>(history_.size()) > kHistory) history_.erase(history_.begin()); }
}

}  // namespace sw::mt01
