#include "ut01/ut01.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace sw::ut01 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"ut01.gain",    "Gain",    -24, 24, 0,   Curve::Lin, 1, {}, "dB"},
            {"ut01.balance", "Balance", -100, 100, 0, Curve::Lin, 1, {}, "%"},
            {"ut01.width",   "Width",   0, 200, 100,  Curve::Lin, 1, {}, "%"},
            {"ut01.phase.l", "Ø L",     0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ut01.phase.r", "Ø R",     0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ut01.swap",    "Swap",    0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ut01.mono",    "Mono",    0, 1, 0,      Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
            {"ut01.channel", "Channel", 0, 2, 0,      Curve::Step, 1, {0, 1, 2}, "", {"Both", "L only", "R only"}},
        };
        v[Balance].minLabel = "L"; v[Balance].maxLabel = "R";
        return v;
    }();
    return s;
}

int classifyTrack(const std::string& name) {
    std::string n; for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](std::initializer_list<const char*> keys) { for (const char* k : keys) if (n.find(k) != std::string::npos) return true; return false; };
    if (has({"vox", "vocal", "voice", "lead v", "bv", "ボーカル", "歌", "コーラス"})) return Vocal;
    if (has({"drum", "kick", "snare", "hat", "tom", "cymbal", "perc", "ドラム", "キック", "スネア"})) return Drums;
    if (has({"bass", "ベース"})) return Bass;
    if (has({"guitar", "gtr", "ギター"})) return Guitar;
    if (has({"key", "piano", "synth", "organ", "pad", "keys", "ピアノ", "シンセ", "キー"})) return Keys;
    if (has({"bus", "master", "mix", "group", "stem", "バス", "マスター"})) return Bus;
    return Other;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; kept_.fill(0.0); has_.fill(false); g_ = target_[Gain]; b_ = target_[Balance]; w_ = target_[Width]; }

void Processor::prepare(double sampleRate, int) { fs_ = sampleRate; g_ = target_[Gain]; b_ = target_[Balance]; w_ = target_[Width]; prepared_ = true; }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::saveExtra(std::vector<uint8_t>& out) const {
    for (int k = 0; k < kKinds; ++k) { out.push_back(has_[static_cast<size_t>(k)] ? 1 : 0); float f = static_cast<float>(kept_[static_cast<size_t>(k)]); uint8_t b[4]; std::memcpy(b, &f, 4); out.insert(out.end(), b, b + 4); }
}
void Processor::loadExtra(const uint8_t* d, size_t size) {
    if (size < static_cast<size_t>(kKinds) * 5) return;
    for (int k = 0; k < kKinds; ++k) { has_[static_cast<size_t>(k)] = d[k * 5] != 0; float f; std::memcpy(&f, d + k * 5 + 1, 4); kept_[static_cast<size_t>(k)] = std::clamp(static_cast<double>(f), -24.0, 24.0); }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1) return;
    const bool stereo = numCh > 1;
    const double a = 1.0 - std::exp(-1.0 / (0.01 * fs_));
    const bool pl = target_[PhaseL] > 0.5, pr = target_[PhaseR] > 0.5, sw = target_[Swap] > 0.5, mono = target_[Mono] > 0.5;
    const int chan = static_cast<int>(target_[Channel] + 0.5);
    for (int i = 0; i < n; ++i) {
        g_ += a * (target_[Gain] - g_); b_ += a * (target_[Balance] - b_); w_ += a * (target_[Width] - w_);
        const bool unity = std::abs(g_ - target_[Gain]) < 1e-9 && std::abs(b_) < 1e-9 && std::abs(w_ - 100.0) < 1e-9;
        const double gl = (chan != RightOnly ? std::pow(10.0, g_ / 20.0) : 1.0), gr = (chan != LeftOnly ? std::pow(10.0, g_ / 20.0) : 1.0);
        double l = ch[0][i], r = stereo ? ch[1][i] : ch[0][i];
        if (pl) l = -l; if (pr) r = -r; if (sw) std::swap(l, r);
        if (!unity || std::abs(w_ - 100.0) > 1e-9) { const double m = 0.5 * (l + r), s = 0.5 * (l - r) * (w_ * 0.01); l = m + s; r = m - s; }
        if (mono) { l = r = 0.5 * (l + r); }
        const double bb = b_ * 0.01; l *= std::min(1.0, 1.0 - bb); r *= std::min(1.0, 1.0 + bb);
        l *= gl; r *= gr;
        if (!std::isfinite(l)) l = 0.0; if (!std::isfinite(r)) r = 0.0;
        ch[0][i] = std::abs(l) < 1e-30 ? 0.0f : static_cast<float>(l); if (stereo) ch[1][i] = std::abs(r) < 1e-30 ? 0.0f : static_cast<float>(r);
    }
}

}  // namespace sw::ut01
