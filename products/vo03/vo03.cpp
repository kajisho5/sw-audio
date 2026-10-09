#include "vo03/vo03.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::vo03 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (int n = 1; n <= kVoices; ++n) for (const char* p : {"on", "interval", "level", "pan", "formant", "humanize", "delay"}) v.push_back("vo03.v" + std::to_string(n) + "." + p);
        for (int n = 1; n <= kVoices; ++n) for (const char* p : {"On", "Interval", "Level", "Pan", "Formant", "Humanize", "Delay"}) v.push_back("Voice " + std::to_string(n) + " " + p);
        return v;
    }();
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        v.push_back({"vo03.source", "Source", 0, 2, 1, Curve::Step, 1, {0, 1, 2}, "", {"MIDI", "Scale", "Fixed"}});
        std::vector<double> degs; std::vector<std::string> names = {"-1 oct", "-7th", "-6th", "-5th", "-4th", "-3rd", "-2nd", "Unison", "+2nd", "+3rd", "+4th", "+5th", "+6th", "+7th", "+1 oct"};
        for (int d = -7; d <= 7; ++d) degs.push_back(d);
        static const double defInt[kVoices] = {2, 4, -2, 7}, defPan[kVoices] = {-40, 40, -70, 70};
        for (int n = 0; n < kVoices; ++n) {
            const size_t id = static_cast<size_t>(n * kPerVoice), nm = static_cast<size_t>(kVoices * kPerVoice + n * kPerVoice);
            v.push_back({ids[id + On].c_str(),       ids[nm + On].c_str(),       0, 1, n < 2 ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            v.push_back({ids[id + Interval].c_str(), ids[nm + Interval].c_str(), -7, 7, defInt[n], Curve::Step, 1, degs, "", names});
            v.push_back({ids[id + Level].c_str(),    ids[nm + Level].c_str(),    -60, 0, -3, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[id + Pan].c_str(),      ids[nm + Pan].c_str(),      -100, 100, defPan[n], Curve::Lin, 1, {}, "%"});
            v.push_back({ids[id + Formant].c_str(),  ids[nm + Formant].c_str(),  -3, 3, 0, Curve::Lin, 1, {}, "st"});
            v.push_back({ids[id + Humanize].c_str(), ids[nm + Humanize].c_str(), 0, 100, 25, Curve::Lin, 1, {}, "%"});
            v.push_back({ids[id + Delay].c_str(),    ids[nm + Delay].c_str(),    0, 100, 15, Curve::Lin, 1, {}, "ms"});
        }
        v.push_back({"vo03.key",   "Key",   0, 11, 0, Curve::Step, 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}});
        v.push_back({"vo03.scale", "Scale", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Major", "Minor"}});
        return v;
    }();
    return s;
}
PitchConfig engineConfig(double fs) { PitchConfig c; c.fs = fs; c.minF0 = 85.0; c.maxF0 = 1000.0; c.windowPeriods = 2.0; return c; }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return PitchAnalyzer::latencyFor(engineConfig(fs_)); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    an_.prepare(engineConfig(fs_));
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = v_[static_cast<size_t>(i)];
        v.synth.prepare(an_); v.corr.reset();
        v.delay.assign(static_cast<size_t>(0.1 * fs_) + 8, 0.0f); v.dpos = 0;
        v.drift = v.noise = 0.0; v.rng = 0x9E3779B9u * static_cast<unsigned>(i + 1); v.ph1 = 0.13 * (i + 1); v.ph2 = 0.37 * (i + 1);
    }
    apply();
    prepared_ = true;
}

void Processor::apply() {
    const int src = static_cast<int>(target_[Source] + 0.5);
    const uint16_t mask = scaleBits(static_cast<int>(target_[Scale] + 0.5) == Minor ? 1 : 0, static_cast<int>(target_[Key] + 0.5));
    for (int i = 0; i < kVoices; ++i) {
        PitchCorrector::Settings s;
        s.mask = mask; s.speedMs = 15.0; s.humanize = 0.0; s.vibrato = 1.0;
        const int deg = static_cast<int>(std::lround(target_[static_cast<size_t>(voiceParam(i, Interval))]));
        if (src == Fixed) { s.harmonyMode = 2; s.harmonyFixed = majorDegreeSemitones(deg); } else { s.harmonyMode = 1; s.harmonyDegrees = deg; }
        s.chordMask = src == Midi ? chord_ : 0;
        s.formantSemis = target_[static_cast<size_t>(voiceParam(i, Formant))]; s.formantFollow = false; s.enabled = true;
        v_[static_cast<size_t>(i)].corr.setSettings(s);
    }
}

void Processor::chordChanged() {
    uint16_t m = 0; for (int k = 0; k < 128; ++k) if (held_[static_cast<size_t>(k)]) m = static_cast<uint16_t>(m | (1u << (k % 12)));
    chord_ = m; if (prepared_) apply();
}
void Processor::noteOn(int key) { if (key < 0 || key > 127) return; auto& h = held_[static_cast<size_t>(key)]; if (h < 255) ++h; chordChanged(); }
void Processor::noteOff(int key) { if (key < 0 || key > 127) return; auto& h = held_[static_cast<size_t>(key)]; if (h > 0) --h; chordChanged(); }
void Processor::allNotesOff() { held_.fill(0); chordChanged(); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_) apply();
}

void Processor::snapToTargets() { if (prepared_) apply(); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const double driftK = 1.0 - std::exp(-1.0 / (0.25 * fs_));
    double gl[kVoices], gr[kVoices]; bool on[kVoices]; size_t dly[kVoices]; double hum[kVoices];
    for (int i = 0; i < kVoices; ++i) {
        on[i] = target_[static_cast<size_t>(voiceParam(i, On))] > 0.5;
        const double g = std::pow(10.0, target_[static_cast<size_t>(voiceParam(i, Level))] / 20.0), p = (target_[static_cast<size_t>(voiceParam(i, Pan))] * 0.01 + 1.0) * 3.14159265358979323846 / 4.0;
        gl[i] = g * std::cos(p); gr[i] = g * std::sin(p);
        dly[i] = static_cast<size_t>(std::lround(target_[static_cast<size_t>(voiceParam(i, Delay))] * 0.001 * fs_));
        hum[i] = target_[static_cast<size_t>(voiceParam(i, Humanize))] * 0.01;
    }
    const int lat = an_.latency();
    for (int k = 0; k < n; ++k) {
        const double x = nch > 1 ? 0.5 * (ch[0][k] + ch[1][k]) : ch[0][k];
        an_.push(x);
        double l = an_.sample(an_.now() - 1 - lat), r = l;   // the lead, delayed like the harmonies
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = v_[static_cast<size_t>(i)];
            // a slow random drift of the pitch (two slow sines and a smoothed noise), +-30 cents x Humanize
            v.ph1 += 0.7 / fs_; v.ph2 += 1.3 / fs_; if (v.ph1 >= 1.0) v.ph1 -= 1.0; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
            v.rng = v.rng * 1664525u + 1013904223u; v.noise += 0.0005 * (((v.rng >> 8) * (1.0 / 8388608.0) - 1.0) - v.noise);
            v.drift += driftK * ((0.5 * std::sin(6.283185307179586 * v.ph1) + 0.3 * std::sin(6.283185307179586 * v.ph2) + 6.0 * v.noise) - v.drift);
            PitchCorrector::Settings s = v.corr.settings(); s.extraSemis = hum[i] * 0.3 * std::clamp(v.drift, -1.0, 1.0); v.corr.setSettings(s);
            double y = v.synth.process(an_, v.corr);
            // the delay
            v.delay[v.dpos] = static_cast<float>(y);
            const size_t rd = (v.dpos + v.delay.size() - std::min(dly[i], v.delay.size() - 1)) % v.delay.size();
            y = v.delay[rd]; v.dpos = (v.dpos + 1) % v.delay.size();
            if (on[i]) { l += gl[i] * y; r += gr[i] * y; }
        }
        if (std::abs(l) < 1e-30) l = 0.0;
        if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][k] = static_cast<float>(l); if (nch > 1) ch[1][k] = static_cast<float>(r);
    }
}

}  // namespace sw::vo03
