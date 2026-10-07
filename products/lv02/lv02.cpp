#include "lv02/lv02.hpp"
#include <cstring>

namespace sw::lv02 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv02.sens",    "Sensitivity", 0, 2, 2,        Curve::Step, 1, {0, 1, 2}, "", {"Low", "Mid", "High"}},
        {"lv02.depth",   "Max depth",   -24, -3, -12,   Curve::Lin,  1, {}, "dB"},
        {"lv02.width",   "Width",       0.05, 1.0 / 3.0, 0.1, Curve::Log, 1, {}, "oct"},   // 1/20 .. 1/3 octave
        {"lv02.release", "Release",     1, 60, 8,       Curve::Log,  1, {}, "s"},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    guard_.prepare(sampleRate, 12); apply(); prepared_ = true;
    for (auto& f : pending_) guard_.addFixed(f.first, f.second);
    pending_.clear();
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); apply(); }

void Processor::saveExtra(std::vector<uint8_t>& out) const {
    const auto fx = guard_.fixedList(); out.push_back(static_cast<uint8_t>(fx.size()));
    for (const auto& f : fx) { const float a = static_cast<float>(f.first), b = static_cast<float>(f.second); uint8_t t[8]; std::memcpy(t, &a, 4); std::memcpy(t + 4, &b, 4); out.insert(out.end(), t, t + 8); }
}
void Processor::loadExtra(const uint8_t* d, size_t size) {
    if (size < 1) return; const size_t n = std::min<size_t>(d[0], 12);
    if (size < 1 + n * 8) return;
    std::vector<std::pair<double, double>> v;
    for (size_t i = 0; i < n; ++i) { float a, b; std::memcpy(&a, d + 1 + i * 8, 4); std::memcpy(&b, d + 5 + i * 8, 4); if (std::isfinite(a) && std::isfinite(b)) v.push_back({a, b}); }
    if (prepared_) { guard_.clearAll(); for (auto& f : v) guard_.addFixed(f.first, f.second); } else pending_ = v;
}

}  // namespace sw::lv02
