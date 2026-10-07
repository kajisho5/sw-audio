#include "gt03/gt03.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::gt03 {
namespace {
const char* kTypeNames[kTypes] = {"None", "Comp", "Drive", "Fuzz", "Chorus", "Delay", "Reverb"};
double lerpLog(double a, double b, double k) { return a * std::pow(b / a, std::clamp(k, 0.0, 1.0)); }
}

const std::vector<ParamSpec>& specs() {
    static std::vector<std::string> names, ids;
    static const std::vector<ParamSpec> s = [] {
        names.reserve(kSlots * kPerSlot); ids.reserve(kSlots * kPerSlot);
        std::vector<ParamSpec> v = {
            {"gt03.input",  "Input",      -24, 24, 0,  Curve::Lin, 1, {}, "dB"},
            {"gt03.output", "Output",     -24, 24, 0,  Curve::Lin, 1, {}, "dB"},
            {"gt03.gate",   "Noise gate", -80, -20, -60, Curve::Lin, 1, {}, "dB"},
            {"gt03.bypass", "Bypass all", 0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        };
        v[NoiseGate].minLabel = "Off";
        static const int defType[kSlots] = {Comp, Drive, Fuzz, Chorus, Delay, Reverb, None, None};
        for (int n = 0; n < kSlots; ++n) {
            const std::string p = "Pedal " + std::to_string(n + 1), key = "gt03.p" + std::to_string(n + 1);
            names.push_back(p + " Type"); ids.push_back(key + ".type");
            v.push_back({ids.back().c_str(), names.back().c_str(), 0, kTypes - 1, static_cast<double>(defType[n]), Curve::Step, 1, {0, 1, 2, 3, 4, 5, 6}, "", {kTypeNames[0], kTypeNames[1], kTypeNames[2], kTypeNames[3], kTypeNames[4], kTypeNames[5], kTypeNames[6]}});
            names.push_back(p + " On"); ids.push_back(key + ".on");
            v.push_back({ids.back().c_str(), names.back().c_str(), 0, 1, n == 0 ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            for (const char* k : {"A", "B", "C"}) { names.push_back(p + " " + k); ids.push_back(key + "." + (k[0] == 'A' ? "a" : k[0] == 'B' ? "b" : "c")); v.push_back({ids.back().c_str(), names.back().c_str(), 0, 10, 5, Curve::Lin, 1, {}, ""}); }
        }
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; maxBlock_ = std::max(16, maxBlock);
    for (auto& c : comp_) { c = std::make_unique<dy08::Processor>(); c->prepare(fs_, maxBlock_); }
    for (auto& c : drive_) { c = std::make_unique<sa06::Processor>(); c->prepare(fs_, maxBlock_); }
    for (auto& c : chorus_) { c = std::make_unique<md01::Processor>(); c->prepare(fs_, maxBlock_); }
    for (auto& c : delay_) { c = std::make_unique<dl01::Processor>(); c->prepare(fs_, maxBlock_); }
    for (auto& c : reverb_) { c = std::make_unique<rv01::Processor>(); c->prepare(fs_, maxBlock_); }
    for (auto& d : dry_) d.assign(static_cast<size_t>(maxBlock_) + 8, 0.0f);
    gate_.prepare(fs_); gate_.set(GateEngine::Mode::Gate, target_[NoiseGate], -50.0, 1.0, 60.0, 150.0); tracker_.prepare(fs_, 12000.0, true); hz_ = 0;
    for (auto& a : wasOn_) a.fill(false);
    prepared_ = true; assign(); applyAll();
}

void Processor::assign() {
    int used[kTypes] = {0};
    for (int n = 0; n < kSlots; ++n) {
        const int t = std::clamp(static_cast<int>(target_[static_cast<size_t>(slotParam(n, Type))] + 0.5), 0, kTypes - 1);
        Assigned a; a.type = t;
        if (t != None) { const int k = used[t]++; a.pool = k < 2 ? k : -1; }
        assigned_[static_cast<size_t>(n)] = a;
    }
}

void Processor::applySlot(int n) {
    const Assigned a = assigned_[static_cast<size_t>(n)]; if (a.pool < 0 || !prepared_) return;
    const double A = target_[static_cast<size_t>(slotParam(n, KnobA))], B = target_[static_cast<size_t>(slotParam(n, KnobB))], C = target_[static_cast<size_t>(slotParam(n, KnobC))];
    const size_t k = static_cast<size_t>(a.pool);
    switch (a.type) {
        case Comp: {
            auto& c = *comp_[k]; const double th = -6.0 - 3.4 * A, ratio = 2.0 + 0.6 * A, red = std::max(0.0, -18.0 - th) * (1.0 - 1.0 / ratio);
            c.setParam(dy08::Threshold, th); c.setParam(dy08::Ratio, ratio); c.setParam(dy08::Knee, 6.0); c.setParam(dy08::Attack, 10.0); c.setParam(dy08::Release, 150.0);
            c.setParam(dy08::Makeup, 0.35 * red + (B - 5.0) * 2.4); c.setParam(dy08::Mix, 100.0); break;
        }
        case Drive: case Fuzz: {
            auto& c = *drive_[k + (a.type == Fuzz ? 2 : 0)];
            for (int b = 0; b < 3; ++b) { c.setParam(sa06::band(b, sa06::BType), a.type == Fuzz ? 4.0 : 1.0); c.setParam(sa06::band(b, sa06::BDrive), 2.4 * A); c.setParam(sa06::band(b, sa06::BShape), 1.0); c.setParam(sa06::band(b, sa06::BMix), 100.0); }
            c.setParam(sa06::Tone, (B - 5.0) * 1.2); break;
        }
        case Chorus: { auto& c = *chorus_[k]; c.setParam(md01::Mode, 2.0); c.setParam(md01::Rate, lerpLog(0.1, 5.0, A / 10.0)); c.setParam(md01::Depth, B); c.setParam(md01::Width, 100.0); c.setParam(md01::Tone, 50.0); c.setParam(md01::Mix, 100.0); break; }
        case Delay: { auto& c = *delay_[k]; c.setParam(dl01::Mode, 1.0); c.setParam(dl01::Time, lerpLog(50.0, 1000.0, A / 10.0)); c.setParam(dl01::Feedback, 8.0 * B); c.setParam(dl01::Sync, 0.0); c.setParam(dl01::Mix, 100.0); break; }
        case Reverb: { auto& c = *reverb_[k]; c.setParam(rv01::Algorithm, 3.0); c.setParam(rv01::Decay, lerpLog(0.3, 8.0, A / 10.0)); c.setParam(rv01::Damping, lerpLog(2000.0, 16000.0, B / 10.0)); c.setParam(rv01::Size, 60.0); c.setParam(rv01::Mix, 100.0); break; }
        default: break;
    }
}
void Processor::applyAll() { for (int n = 0; n < kSlots; ++n) applySlot(n); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (!prepared_) return;
    if (id == NoiseGate) { gate_.set(GateEngine::Mode::Gate, target_[NoiseGate], -50.0, 1.0, 60.0, 150.0); return; }
    if (id < Slot1) return;
    const int slot = (id - Slot1) / kPerSlot, field = (id - Slot1) % kPerSlot;
    if (field == Type) { assign(); applyAll(); } else applySlot(slot);
}

int Processor::tunerNote() const { return hz_ > 20.0 ? static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz_ / 440.0))) : 0; }
double Processor::tunerCents() const { if (hz_ <= 20.0) return 0.0; const double m = 69.0 + 12.0 * std::log2(hz_ / 440.0); return (m - std::round(m)) * 100.0; }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    for (int off = 0; off < n; off += maxBlock_) { const int m = std::min(maxBlock_, n - off); float* c[2] = {ch[0] + off, numCh > 1 ? ch[1] + off : ch[0] + off}; run(c, std::min(numCh, 2), m); }
}

void Processor::run(float** ch, int numCh, int n) {
    // the tuner listens to the input whatever else happens
    for (int i = 0; i < n; ++i) tracker_.push(numCh > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i]);
    hz_ = tracker_.voiced() ? tracker_.f0() : hz_;
    if (target_[BypassAll] > 0.5) return;
    // noise gate (the lowest position is Off)
    if (target_[NoiseGate] > -79.99) for (int i = 0; i < n; ++i) { double key = std::abs(ch[0][i]); if (numCh > 1) key = std::max(key, static_cast<double>(std::abs(ch[1][i]))); const double g = gate_.process(key); if (g != 1.0) for (int c = 0; c < numCh; ++c) ch[c][i] = static_cast<float>(ch[c][i] * g); }
    for (int s = 0; s < kSlots; ++s) {
        const Assigned a = assigned_[static_cast<size_t>(s)]; if (a.type == None || a.pool < 0) continue;
        if (target_[static_cast<size_t>(slotParam(s, On))] < 0.5) continue;
        const size_t k = static_cast<size_t>(a.pool);
        if (a.type == Comp) comp_[k]->process(ch, numCh, n);
        else if (a.type == Drive || a.type == Fuzz) {
            drive_[k + (a.type == Fuzz ? 2 : 0)]->process(ch, numCh, n);   // the Level knob is the board's own gain: SA06's output gain belongs to its shell, not to its core
            const double g = std::pow(10.0, (target_[static_cast<size_t>(slotParam(s, KnobC))] - 5.0) * 2.4 / 20.0);
            if (g != 1.0) for (int c = 0; c < numCh; ++c) for (int i = 0; i < n; ++i) ch[c][i] = static_cast<float>(ch[c][i] * g);
        }
        else {
            for (int c = 0; c < numCh; ++c) std::copy(ch[c], ch[c] + n, dry_[static_cast<size_t>(c)].begin());
            if (a.type == Chorus) chorus_[k]->process(ch, numCh, n); else if (a.type == Delay) delay_[k]->process(ch, numCh, n); else reverb_[k]->process(ch, numCh, n);
            const double m = target_[static_cast<size_t>(slotParam(s, KnobC))] / 10.0;
            for (int c = 0; c < numCh; ++c) for (int i = 0; i < n; ++i) {
                const float d = dry_[static_cast<size_t>(c)][static_cast<size_t>(i)], w = ch[c][i];
                const float y = a.type == Chorus ? static_cast<float>((1.0 - m) * d + m * w) : static_cast<float>(d + m * w);
                ch[c][i] = std::abs(y) < 1e-30f ? 0.0f : y;
            }
        }
    }
}

}  // namespace sw::gt03
