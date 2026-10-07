#include "lv09/lv09.hpp"
#include <algorithm>
#include <cmath>

namespace sw::lv09 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<double> st; std::vector<std::string> lb; for (int i = 1; i <= 16; ++i) { st.push_back(i); lb.push_back(std::to_string(i)); }
        std::vector<ParamSpec> v = {
            {"lv09.base",      "Base",        0, 2, 2,      Curve::Step, 1, {0, 1, 2}, "", {"50 Hz", "60 Hz", "Auto"}},
            {"lv09.harmonics", "Harmonics",   1, 16, 8,     Curve::Step, 1, st, "", lb},
            {"lv09.depth",     "Depth",       -40, 0, -30,  Curve::Lin, 1, {}, "dB"},
            {"lv09.width",     "Width",       0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"Narrow", "Medium", "Wide"}},
            {"lv09.track",     "Track drift", 0, 1, 1,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"lv09.listen",    "Listen",      0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[Listen].automatable = false;
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; for (int i = 0; i < Listen; ++i) apply(i); }

void Processor::apply(int id) {
    const double v = target_[static_cast<size_t>(id)];
    switch (id) {
        case Base: core_.setParam(rs03::Base, v); break;
        case Harmonics: core_.setHarmonicCount(static_cast<int>(v + 0.5)); break;
        case Depth: core_.setParam(rs03::Depth, -v / 4.0); break;
        case Width: core_.setParam(rs03::Width, v * 50.0); break;
        case TrackDrift: core_.setParam(rs03::Track, v); break;
        default: break;
    }
}

void Processor::prepare(double sampleRate, int maxBlock) {
    core_.setParam(rs03::Buzz, 0);
    for (int i = 0; i < Listen; ++i) apply(i);
    core_.prepare(sampleRate, maxBlock);
    for (auto& d : dry_) d.assign(static_cast<size_t>(std::max(1, maxBlock) + 8), 0.0f);
    prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); apply(id); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const int nc = std::min(numCh, 2);
    const bool listen = target_[Listen] > 0.5;
    if (listen) { for (int c = 0; c < nc; ++c) { if (dry_[static_cast<size_t>(c)].size() < static_cast<size_t>(n)) dry_[static_cast<size_t>(c)].assign(static_cast<size_t>(n), 0.0f); std::copy(ch[c], ch[c] + n, dry_[static_cast<size_t>(c)].begin()); } }
    core_.process(ch, numCh, n);
    if (listen) for (int c = 0; c < nc; ++c) for (int i = 0; i < n; ++i) { const float y = dry_[static_cast<size_t>(c)][static_cast<size_t>(i)] - ch[c][i]; ch[c][i] = std::abs(y) < 1e-30f ? 0.0f : y; }
}

}  // namespace sw::lv09
