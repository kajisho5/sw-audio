#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include <string>

namespace sw::in07 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v;
        std::vector<double> voices; std::vector<std::string> voiceLabels;
        for (int i = 1; i <= Processor::kMaxVoices; ++i) { voices.push_back(i); voiceLabels.push_back(std::to_string(i)); }
        std::vector<double> semis; std::vector<std::string> semiLabels;
        for (int i = -12; i <= 12; ++i) { semis.push_back(i); semiLabels.push_back(i > 0 ? "+" + std::to_string(i) : std::to_string(i)); }
        std::vector<double> bends; std::vector<std::string> bendLabels;
        for (int i = 0; i <= 24; ++i) { bends.push_back(i); bendLabels.push_back(std::to_string(i)); }
        v.push_back({"in07.voices", "Voices", 1, Processor::kMaxVoices, 16, Curve::Step, 1, voices, "", voiceLabels});
        v.push_back({"in07.mode",   "Mode",   0, 2, Poly, Curve::Step, 1, {0, 1, 2}, "", {"Poly", "Mono", "Legato"}});
        v.push_back({"in07.glide",  "Glide",  0, 2000, 0, Curve::Skew, 3, {}, "ms", {}, "Off"});
        v.push_back({"in07.bend",   "Bend range", 0, 24, 2, Curve::Step, 1, bends, "st", bendLabels});
        v.push_back({"in07.level",  "Level",  -40, 6, -6, Curve::Lin, 1, {}, "dB"});
        // the ids and names of the layer blocks live here (ParamSpec keeps pointers)
        static std::deque<std::string> text;
        auto str = [](const std::string& x) { text.push_back(x); return text.back().c_str(); };
        static const int kWave[kLayers] = {Saw, Square, Triangle, Sine}, kOct[kLayers] = {0, -1, 1, 0};
        std::vector<double> tableSteps, ratioSteps, sampleSteps;
        std::vector<std::string> tableLabels, ratioLabels, sampleLabels;
        for (int i = 0; i < kWaveTables; ++i) { tableSteps.push_back(i); tableLabels.push_back(kWaveTableNames[i]); }
        for (int i = 0; i < kFmRatios; ++i) { ratioSteps.push_back(i); ratioLabels.push_back(kFmRatioLabels[i]); }
        for (int i = 0; i < kSamples; ++i) { sampleSteps.push_back(i); sampleLabels.push_back(kSampleNames[i]); }
        for (int l = 0; l < kLayers; ++l) {
            const std::string id = "in07.l" + std::to_string(l + 1) + ".", nm = "L" + std::to_string(l + 1) + " ";
            auto add = [&](const char* key, const char* name, ParamSpec ps) { ps.id = str(id + key); ps.name = str(nm + name); v.push_back(ps); };
            add("on",         "On",          {"", "", 0, 1, l == 0 ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            add("level",      "Level",       {"", "", -60, 6, 0, Curve::Lin, 1, {}, "dB", {}, "Off"});
            add("pan",        "Pan",         {"", "", -100, 100, 0, Curve::Lin, 1, {}, ""});
            add("osc.type",   "Osc type",    {"", "", 0, 3, OscAnalog, Curve::Step, 1, {0, 1, 2, 3}, "", {"Analog", "Wavetable", "FM", "Sample"}});
            add("osc.wave",   "Wave",        {"", "", 0, 3, static_cast<double>(kWave[l]), Curve::Step, 1, {0, 1, 2, 3}, "", {"Sine", "Triangle", "Saw", "Square"}});
            add("osc.pw",     "Pulse width", {"", "", 5, 95, 50, Curve::Lin, 1, {}, "%"});
            add("wt.table",   "Table",       {"", "", 0, kWaveTables - 1, 0, Curve::Step, 1, tableSteps, "", tableLabels});
            add("wt.pos",     "Position",    {"", "", 0, 100, 0, Curve::Lin, 1, {}, "%"});
            add("fm.ratio",   "FM ratio",    {"", "", 0, kFmRatios - 1, 1, Curve::Step, 1, ratioSteps, "", ratioLabels});
            add("fm.index",   "FM index",    {"", "", 0, 100, 30, Curve::Lin, 1, {}, "%"});
            add("fm.decay",   "FM decay",    {"", "", 10, 10000, 10000, Curve::Log, 1, {}, "ms", {}, nullptr, "Hold"});
            add("fm.feedback", "FM feedback", {"", "", 0, 100, 0, Curve::Lin, 1, {}, "%"});
            add("smp.id",     "Sample",      {"", "", 0, kSamples - 1, 0, Curve::Step, 1, sampleSteps, "", sampleLabels});
            add("osc.octave", "Octave",      {"", "", -2, 2, static_cast<double>(kOct[l]), Curve::Step, 1, {-2, -1, 0, 1, 2}, "", {"-2", "-1", "0", "+1", "+2"}});
            add("osc.semi",   "Semi",        {"", "", -12, 12, 0, Curve::Step, 1, semis, "st", semiLabels});
            add("osc.fine",   "Fine",        {"", "", -100, 100, 0, Curve::Lin, 1, {}, "ct"});
            add("osc.unison", "Unison",      {"", "", 1, 8, 1, Curve::Step, 1, {1, 2, 3, 4, 5, 6, 7, 8}, "v", {"1", "2", "3", "4", "5", "6", "7", "8"}});
            add("osc.detune", "Detune",      {"", "", 0, 100, 22, Curve::Lin, 1, {}, "%"});
            add("osc.spread", "Spread",      {"", "", 0, 100, 80, Curve::Lin, 1, {}, "%"});
            add("osc.gravity", "Gravity",    {"", "", 0, 100, 0, Curve::Lin, 1, {}, "%"});
            add("flt.type",   "Filter",      {"", "", 0, 3, LP24, Curve::Step, 1, {0, 1, 2, 3}, "", {"LP 12", "LP 24", "BP 12", "HP 12"}});
            add("flt.cutoff", "Cutoff",      {"", "", 20, 20000, 2400, Curve::Log, 1, {}, "Hz"});
            add("flt.res",    "Resonance",   {"", "", 0, 100, 30, Curve::Lin, 1, {}, "%"});
            add("flt.drive",  "Drive",       {"", "", 0, 100, 18, Curve::Lin, 1, {}, "%"});
            add("flt.env",    "Filter env",  {"", "", -100, 100, 40, Curve::Lin, 1, {}, "%"});
            add("flt.key",    "Key track",   {"", "", 0, 100, 50, Curve::Lin, 1, {}, "%"});
            add("amp.a",      "Attack",      {"", "", 0.5, 10000, 5, Curve::Log, 1, {}, "ms"});
            add("amp.d",      "Decay",       {"", "", 1, 10000, 320, Curve::Log, 1, {}, "ms"});
            add("amp.s",      "Sustain",     {"", "", 0, 100, 70, Curve::Lin, 1, {}, "%"});
            add("amp.r",      "Release",     {"", "", 1, 20000, 420, Curve::Log, 1, {}, "ms"});
            add("fenv.a",     "Filter attack",  {"", "", 0.5, 10000, 2, Curve::Log, 1, {}, "ms"});
            add("fenv.d",     "Filter decay",   {"", "", 1, 10000, 600, Curve::Log, 1, {}, "ms"});
            add("fenv.s",     "Filter sustain", {"", "", 0, 100, 20, Curve::Lin, 1, {}, "%"});
            add("fenv.r",     "Filter release", {"", "", 1, 20000, 500, Curve::Log, 1, {}, "ms"});
            add("vel",        "Vel sens",    {"", "", 0, 100, 50, Curve::Lin, 1, {}, "%"});
        }
        // the effects: six order slots (Drive, Chorus, Delay, Reverb, EQ, Limit by default), then each effect's On and three settings
        const std::vector<std::string> fxNames = {"Drive", "Chorus", "Delay", "Reverb", "EQ", "Limit"};
        for (int i = 0; i < kFx; ++i) {
            ParamSpec ps{"", "", 0, kFx - 1, static_cast<double>(i), Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", fxNames};
            ps.id = str("in07.fx.slot" + std::to_string(i + 1)); ps.name = str("FX slot " + std::to_string(i + 1)); ps.automatable = false;
            v.push_back(ps);
        }
        auto onOff = [&](const char* id, const char* name, bool on) { v.push_back({id, name, 0, 1, on ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}}); };
        onOff("in07.fx.drive.on", "Drive on", true);
        v.push_back({"in07.fx.drive.amount", "Drive amount", 0, 100, 30, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.fx.drive.tone",   "Drive tone",   0, 100, 60, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.fx.drive.mix",    "Drive mix",    0, 100, 100, Curve::Lin, 1, {}, "%"});
        onOff("in07.fx.chorus.on", "Chorus on", true);
        v.push_back({"in07.fx.chorus.rate",  "Chorus rate",  0.05, 5, 0.2, Curve::Log, 1, {}, "Hz"});
        v.push_back({"in07.fx.chorus.depth", "Chorus depth", 0, 100, 40, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.fx.chorus.mix",   "Chorus mix",   0, 100, 35, Curve::Lin, 1, {}, "%"});
        onOff("in07.fx.delay.on", "Delay on", true);
        v.push_back({"in07.fx.delay.time",   "Delay time",   0, 5, 2, Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2"}});
        v.push_back({"in07.fx.delay.feedback", "Delay feedback", 0, 90, 35, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.fx.delay.mix",    "Delay mix",    0, 100, 22, Curve::Lin, 1, {}, "%"});
        onOff("in07.fx.reverb.on", "Reverb on", true);
        v.push_back({"in07.fx.reverb.size",  "Reverb size",  0.3, 12, 2.3, Curve::Log, 1, {}, "s"});
        v.push_back({"in07.fx.reverb.damp",  "Reverb damp",  0, 100, 40, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.fx.reverb.mix",   "Reverb mix",   0, 100, 30, Curve::Lin, 1, {}, "%"});
        onOff("in07.fx.eq.on", "EQ on", false);
        v.push_back({"in07.fx.eq.low",       "EQ low",       -12, 12, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"in07.fx.eq.mid",       "EQ mid",       -12, 12, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"in07.fx.eq.high",      "EQ high",      -12, 12, 0, Curve::Lin, 1, {}, "dB"});
        onOff("in07.fx.limit.on", "Limit on", true);
        v.push_back({"in07.fx.limit.gain",   "Limit gain",   0, 12, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"in07.fx.limit.ceiling", "Limit ceiling", -12, 0, -1, Curve::Lin, 1, {}, "dB"});
        v.push_back({"in07.fx.limit.release", "Limit release", 10, 500, 50, Curve::Log, 1, {}, "ms"});
        // modulation
        const std::vector<double> syncSteps = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        const std::vector<std::string> syncLabels = {"Off", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T"};
        for (int i = 0; i < 2; ++i) {
            const std::string id = "in07.lfo" + std::to_string(i + 1) + ".", nm = "LFO" + std::to_string(i + 1) + " ";
            auto add = [&](const char* key, const char* name, ParamSpec ps) { ps.id = str(id + key); ps.name = str(nm + name); v.push_back(ps); };
            add("shape",   "shape",   {"", "", 0, 4, i == 0 ? 0.0 : 1.0, Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Orbit", "Triangle", "Saw", "Square", "Random"}});
            add("rate",    "rate",    {"", "", 0.02, 20, i == 0 ? 0.8 : 0.25, Curve::Log, 1, {}, "Hz"});
            add("sync",    "sync",    {"", "", 0, 10, 0, Curve::Step, 1, syncSteps, "", syncLabels});
            add("orbit",   "orbit",   {"", "", 0, 95, 0, Curve::Lin, 1, {}, "%"});
            add("trigger", "trigger", {"", "", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Free", "Note"}});
        }
        static const char* const kMacro[8] = {"M1 Bright", "M2 Reso", "M3 Attack", "M4 Release", "M5 Drive", "M6 Width", "M7 Delay", "M8 Reverb"};
        for (int i = 0; i < 8; ++i) v.push_back({str("in07.macro" + std::to_string(i + 1)), kMacro[i], 0, 100, 50, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.flyby.mode",  "Flyby",           0, 3, FlybyOff, Curve::Step, 1, {0, 1, 2, 3}, "", {"Off", "Arrive", "Pass", "Leave"}});
        v.push_back({"in07.flyby.depth", "Flyby depth",     0, 100, 60, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.flyby.time",  "Flyby time",      0.05, 8, 1.5, Curve::Log, 1, {}, "s"});
        v.push_back({"in07.flyby.near",  "Flyby closeness", 0, 100, 50, Curve::Lin, 1, {}, "%"});
        v.push_back({"in07.flyby.side",  "Flyby side",      0, 2, 2, Curve::Step, 1, {0, 1, 2}, "", {"L > R", "R > L", "Alternate"}});
        std::vector<double> srcSteps, dstSteps;
        for (int i = 0; i < kModSources; ++i) srcSteps.push_back(i);
        for (int i = 0; i < kModDests; ++i) dstSteps.push_back(i);
        const std::vector<std::string> srcLabels = {"None", "LFO 1", "LFO 2", "Env 2", "Velocity", "Mod wheel", "Aftertouch", "Key",
                                                    "M1 Bright", "M2 Reso", "M3 Attack", "M4 Release", "M5 Drive", "M6 Width", "M7 Delay", "M8 Reverb"};
        const std::vector<std::string> dstLabels = {"None", "Cutoff", "Resonance", "Pitch", "Drive", "Pan", "Level", "L1 level", "L2 level", "L3 level", "L4 level",
                                                    "LFO 1 rate", "LFO 2 rate", "Pulse width", "Detune", "Gravity", "WT position", "FM index"};
        for (int i = 0; i < kModSlots; ++i) {
            const std::string id = "in07.mod" + std::to_string(i + 1) + ".", nm = "Mod " + std::to_string(i + 1) + " ";
            auto add = [&](const char* key, const char* name, ParamSpec ps) { ps.id = str(id + key); ps.name = str(nm + name); v.push_back(ps); };
            add("on",     "on",     {"", "", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            add("src",    "source", {"", "", 0, kModSources - 1, SrcNone, Curve::Step, 1, srcSteps, "", srcLabels});
            add("dst",    "target", {"", "", 0, kModDests - 1, DstNone, Curve::Step, 1, dstSteps, "", dstLabels});
            add("amount", "amount", {"", "", -100, 100, 0, Curve::Lin, 1, {}, "%"});
        }
        std::vector<double> presetSteps; std::vector<std::string> presetLabels = {"Init"};
        for (const auto& n : presetNames()) presetLabels.push_back(n);
        for (size_t i = 0; i < presetLabels.size(); ++i) presetSteps.push_back(static_cast<double>(i));
        ParamSpec ps{"in07.preset", "Preset", 0, static_cast<double>(presetLabels.size() - 1), 0, Curve::Step, 1, presetSteps, "", presetLabels};
        ps.automatable = false;
        v.push_back(ps);
        return v;
    }();
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSqrt2 = 1.41421356237309504880;
constexpr double kQ24a = 0.5412, kQ24b = 1.3066, kQ12 = 0.7071, kQMax = 25.0, kEnvOctaves = 5.0, kDetuneCents = 50.0;
// modulation scales (at amount 100 % and a source of 1): pitch 12 semitones, cutoff 5 octaves (kEnvOctaves), resonance / drive / detune /
// gravity 100 points, pulse width 45 points, pan full; the macros +-50 points (Bright +-2 octaves, Attack / Release x4 .. /4)
constexpr double kModPitch = 12.0, kModRange = 100.0, kModPw = 45.0, kMacroRange = 50.0;
// flyby: up to 7 semitones of Doppler and 3 octaves of air at full depth; gravity: a floor of 2 Hz of pull (copies with no detune still gather)
constexpr double kFlyPitch = 7.0, kFlyCutOct = 3.0, kGravityFloorHz = 2.0;

// 2-point polyBLAMP (the integral of the BLEP): the correction for a slope change of 1 per sample at phase 0
inline double blamp(double t, double dt) {
    if (t < dt) { const double u = 1.0 - t / dt; return u * u * u / 6.0; }
    if (t > 1.0 - dt) { const double u = 1.0 + (t - 1.0) / dt; return u * u * u / 6.0; }
    return 0.0;
}
inline double wrap(double t) { return t >= 1.0 ? t - 1.0 : t; }
// tanh as a [7/6] Pade approximant: within 1e-4 of tanh, monotonic, 1.0 from |x| = 4.97 on (the step there is 6e-7)
inline double fastTanh(double x) {
    if (x > 4.97) return 1.0;
    if (x < -4.97) return -1.0;
    const double x2 = x * x;
    return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
}
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
// 4-point cubic Hermite (Catmull-Rom) between p[0] and p[1] (p[-1] and p[2] are read too)
inline double hermite(const float* p, double t) {
    const double xm = p[-1], x0 = p[0], x1 = p[1], x2 = p[2];
    const double c1 = 0.5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
}  // namespace

// ---- minBLEP table (built once, in prepare)
double MinBlep::operator()(double x) const {
    const double idx = x * kRes;
    if (idx >= kTaps * kRes) return 1.0;
    if (idx <= 0.0) return step[0];
    const int i = static_cast<int>(idx);
    const double f = idx - i;
    return step[static_cast<size_t>(i)] + f * (step[static_cast<size_t>(i) + 1] - step[static_cast<size_t>(i)]);
}

const MinBlep& minBlep() {
    static const MinBlep table = [] {
        MinBlep m;
        constexpr int N = MinBlep::kTaps * MinBlep::kRes, M = 4 * N;
        constexpr double fc = 0.45;   // of fs
        std::vector<std::complex<double>> x(static_cast<size_t>(M));
        for (int i = 0; i < N; ++i) {   // Blackman-windowed sinc, centred, at kRes x the output rate
            const double t = (i - N / 2.0) / MinBlep::kRes, u = 2.0 * fc * t;
            const double sinc = std::abs(u) < 1e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u);
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (N - 1)) + 0.08 * std::cos(4.0 * kPi * i / (N - 1));
            x[static_cast<size_t>(i)] = sinc * w;
        }
        Fft f(M);
        f.forward(x);
        for (auto& v : x) v = std::log(std::max(std::abs(v), 1e-9));   // log magnitude
        f.inverse(x);                                                    // real cepstrum
        for (int i = 1; i < M / 2; ++i) { x[static_cast<size_t>(i)] *= 2.0; x[static_cast<size_t>(M - i)] = 0.0; }   // fold onto the causal side
        f.forward(x);
        for (auto& v : x) v = std::exp(v);
        f.inverse(x);                                                    // the minimum-phase impulse
        double sum = 0.0;
        for (int i = 0; i < N; ++i) sum += x[static_cast<size_t>(i)].real();
        double acc = 0.0;
        m.step[0] = 0.0;
        for (int i = 0; i < N; ++i) { acc += x[static_cast<size_t>(i)].real(); m.step[static_cast<size_t>(i) + 1] = acc / sum; }
        double area = 0.0;
        for (int i = 0; i < N; ++i) area += 1.0 - 0.5 * (m.step[static_cast<size_t>(i)] + m.step[static_cast<size_t>(i) + 1]);
        m.delay = area / MinBlep::kRes;
        return m;
    }();
    return table;
}

// ---- LFO shapes
double lfoShape(int shape, double phase, double ecc, uint32_t seed) {
    const double ph = phase - std::floor(phase);
    switch (shape) {
        case LfoTriangle: return ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
        case LfoSaw: return 2.0 * ph - 1.0;
        case LfoSquare: return ph < 0.5 ? 1.0 : -1.0;
        case LfoRandom: { uint32_t h = seed * 2654435761u; h ^= h >> 16; h *= 2246822519u; h ^= h >> 13; return (h >> 8) * (2.0 / 16777216.0) - 1.0; }
        default: {   // Orbit: Kepler's equation E - e sin E = M, Newton from E = M + e sin M (converges for e < 1)
            const double e = std::clamp(ecc, 0.0, 0.95), m = 2.0 * kPi * ph;
            if (e <= 0.0) return std::cos(m);
            double E = m + e * std::sin(m);
            for (int i = 0; i < 8; ++i) { const double d = (E - e * std::sin(E) - m) / (1.0 - e * std::cos(E)); E -= d; if (std::abs(d) < 1e-12) break; }
            return std::cos(E);
        }
    }
}

// ---- oscillator
void BlepOsc::jump(double after, double height) {
    const MinBlep& mb = minBlep();
    for (int k = 0; k < MinBlep::kTaps; ++k)
        ring_[static_cast<size_t>((pos_ + k) & (MinBlep::kTaps - 1))] += static_cast<float>(height * (mb(after + k) - (after + k >= mb.delay ? 1.0 : 0.0)));
}

double BlepOsc::late(double t) const {   // the phase minBLEP's delay ago
    double u = t - minBlep().delay * inc_;
    while (u < 0.0) u += 1.0;
    return u;
}

double BlepOsc::next() {
    const double t = ph_, dt = inc_;
    double y;
    switch (wave_) {
        case Sine: y = std::sin(2.0 * kPi * t); break;
        case Triangle:
            y = t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t;
            if (correct_) y += 8.0 * dt * (blamp(t, dt) - blamp(wrap(t + 0.5), dt));
            break;
        case Square: { const double td = correct_ ? late(t) : t; y = (td < pw_ ? 1.0 : -1.0) - (2.0 * pw_ - 1.0); break; }   // minus the pulse's mean: no DC at any width
        default: y = 2.0 * (correct_ ? late(t) : t) - 1.0; break;   // Saw
    }
    if (correct_) {
        y += ring_[static_cast<size_t>(pos_)];
        ring_[static_cast<size_t>(pos_)] = 0.0f;
        pos_ = (pos_ + 1) & (MinBlep::kTaps - 1);
    }
    // the jumps between this sample and the next
    double b = t + dt;
    if (wave_ == Saw) {
        if (b >= 1.0) { b -= 1.0; if (correct_) jump(b / dt, -2.0); }
    } else if (wave_ == Square) {
        if (correct_ && t < pw_ && b >= pw_) jump((b - pw_) / dt, -2.0);
        if (b >= 1.0) {
            b -= 1.0;
            if (correct_) { jump(b / dt, 2.0); if (b >= pw_) jump((b - pw_) / dt, -2.0); }
        }
    } else if (b >= 1.0) {
        b -= 1.0;
    }
    ph_ = b;
    return y;
}

// ---- envelope
void Adsr::setTimes(double a, double d, double s, double r) {
    if (!dirty_ && a == a_ && d == d_ && s == s_ && r == r_) return;
    a_ = a; d_ = d; s_ = s; r_ = r;
    update();
}

void Adsr::update() {
    dirty_ = false;
    const double na = std::max(1.0, a_ * fs_);
    ca_ = std::pow(1.0 / 6.0, 1.0 / na);              // from 0 toward 1.2: 1/6 of the way is left (level 1.0) after na samples
    dN_ = std::max(1, static_cast<int>(std::lround(d_ * fs_)));
    cd_ = std::pow(1e-3, 1.0 / dN_);
    rN_ = std::max(1, static_cast<int>(std::lround(r_ * fs_)));
    cr_ = std::pow(1e-4, 1.0 / rN_);
}

void Adsr::gate(bool on) {
    if (dirty_) update();
    if (on) { stage_ = Attack; count_ = 0; quick_ = false; }
    else if (stage_ != Idle && !quick_) { stage_ = Release; count_ = 0; }
}

void Adsr::quickRelease(double seconds) {
    if (stage_ == Idle) return;
    qN_ = std::max(1, static_cast<int>(std::lround(seconds * fs_)));
    cq_ = std::pow(1e-4, 1.0 / qN_);
    quick_ = true; stage_ = Release; count_ = 0;
}

double Adsr::next() {
    switch (stage_) {
        case Idle: return 0.0;
        case Attack:
            level_ = 1.2 + (level_ - 1.2) * ca_;
            if (level_ >= 1.0 - 1e-12) { level_ = 1.0; stage_ = Decay; count_ = 0; }
            break;
        case Decay:
            level_ = s_ + (level_ - s_) * cd_;
            if (++count_ >= dN_) { level_ = s_; stage_ = s_ <= 0.0 ? Idle : Sustain; }   // Sustain 0: the sound has ended (the voice sleeps, held or not)
            break;
        case Sustain:
            if (level_ != s_) { level_ += (s_ - level_) * 0.004; if (std::abs(level_ - s_) < 1e-6) level_ = s_; }   // a moved Sustain knob: about 5 ms
            break;
        case Release:
            level_ *= quick_ ? cq_ : cr_;
            if (++count_ >= (quick_ ? qN_ : rN_)) { level_ = 0.0; stage_ = Idle; quick_ = false; }
            break;
    }
    return level_;
}

// ---- voice
void Voice::prepare(double fs, const double* layerParams, const Shared* shared, int layer) {
    fs_ = fs; lp_ = layerParams; sh_ = shared; layer_ = layer;
    dca_ = std::exp(-2.0 * kPi * 5.0 / fs);
    amp_.prepare(fs); fenv_.prepare(fs);
    reset();
    rng_ = 0x5EED1234u;
}

void Voice::reset() {
    amp_.reset(); fenv_.reset();
    for (auto& ch : svf_) for (auto& f : ch) f.reset();
    for (auto& o : os_) o.reset();
    ctl_ = 0; first_ = true; killed_ = false; glideLeft_ = 0; flyOn_ = false; coherence_ = 0.0;
}

void Voice::noteOn(int key, double velocity, double glideFrom) {
    const bool wasActive = amp_.active();
    note_ = key; killed_ = false;
    const double s = p(VelSens) / 100.0, v = std::clamp(velocity, 0.0, 1.0);
    velGain_ = (1.0 - s) + s * v * v; vel_ = v;
    // the flyby: Arrive and Pass start with the note, Leave with its release
    const int fm = sh_ ? sh_->flyMode : FlybyOff;
    flyOn_ = fm == FlybyArrive || fm == FlybyPass; flyT_ = 0.0; flySign_ = sh_ ? sh_->flySign : 1.0;
    const double gms = sh_ ? sh_->glideMs : 0.0;
    if (glideFrom >= 0.0 && gms > 0.0 && glideFrom != key) {
        glideFrom_ = glideFrom;
        glideN_ = glideLeft_ = std::max(1, static_cast<int>(std::lround(gms * 1e-3 * fs_)));
    } else {
        glideLeft_ = 0;
    }
    if (!wasActive) {   // a fresh start: the voice was silent
        unison_ = std::clamp(static_cast<int>(std::lround(p(Unison))), 1, kMaxUnison);
        otype_ = std::clamp(static_cast<int>(std::lround(p(OscType))), 0, 3);
        sample_ = std::clamp(static_cast<int>(std::lround(p(SampleId))), 0, kSamples - 1);
        fmRatio_ = fmRatioOf(static_cast<int>(std::lround(p(FmRatio))));
        const auto& smp = sampleBank().s[static_cast<size_t>(sample_)];
        for (int i = 0; i < unison_; ++i) {
            const size_t k = static_cast<size_t>(i);
            double ph = 0.0;
            if (unison_ > 1) { rng_ = rng_ * 1664525u + 1013904223u; ph = (rng_ >> 8) * (1.0 / 16777216.0); }
            osc_[k].setPhase(ph);
            ph_[k] = ph;
            pm_[k] = fmRatio_ * ph - std::floor(fmRatio_ * ph);   // the modulator in step with the carrier (no DC where a sideband meets 0 Hz)
            fb0_[k] = fb1_[k] = 0.0;
            dcx_ = {}; dcy_ = {};
            spos_[k] = smp.loop ? ph * smp.length : 0.0;          // loops: the copies start at different places (one copy: the start)
            sdone_[k] = false;
        }
        oversample_ = p(Drive) > 0.0 || (sh_ && sh_->driveRouted);
        for (auto& ch : svf_) for (auto& f : ch) f.reset();
        for (auto& o : os_) o.reset();
        first_ = true;
    }
    fmEnv_ = 1.0;   // the FM index starts again
    if (otype_ == OscSample && !sampleBank().s[static_cast<size_t>(sample_)].loop)   // a one-shot plays again
        for (int i = 0; i < unison_; ++i) { spos_[static_cast<size_t>(i)] = 0.0; sdone_[static_cast<size_t>(i)] = false; }
    ctl_ = 0;   // the next sample starts with a control update
    align_ = sh_ ? sh_->gridLeft : 0;   // and the one after it falls on the synth's grid (the periods are not split later)
    amp_.gate(true); fenv_.gate(true);
}

void Voice::legato(int key, double glideFrom) {
    note_ = key;
    const double gms = sh_ ? sh_->glideMs : 0.0;
    if (glideFrom >= 0.0 && gms > 0.0 && glideFrom != key) {
        glideFrom_ = glideFrom;
        glideN_ = glideLeft_ = std::max(1, static_cast<int>(std::lround(gms * 1e-3 * fs_)));
    } else {
        glideLeft_ = 0;
    }
    ctl_ = 0; align_ = sh_ ? sh_->gridLeft : 0;
}

void Voice::noteOff() {
    amp_.gate(false); fenv_.gate(false);
    if (sh_ && sh_->flyMode == FlybyLeave && amp_.active() && !killed_) { flyOn_ = true; flyT_ = 0.0; flySign_ = sh_->flySign; }
}

void Voice::kill() { killed_ = true; amp_.quickRelease(0.003); }

double Voice::frequency() const { return 440.0 * std::exp2((pitch_ - 69.0) / 12.0); }

void Voice::control() {
    // modulation: the matrix's sums per destination (amount x source; the per-voice sources are this voice's)
    double md[kModDests] = {};
    double m[8] = {};   // the macros, -1..1 (the middle = 0)
    if (sh_) {
        double src[kModSources];
        std::copy(std::begin(sh_->src), std::end(sh_->src), src);
        src[SrcEnv2] = fenv_.level(); src[SrcVelocity] = vel_; src[SrcKey] = (note_ - 60.0) / 60.0;
        for (int i = 0; i < sh_->nSlots; ++i) { const auto& sl = sh_->slots[static_cast<size_t>(i)]; md[sl.dst] += sl.amount * src[sl.src]; }
        for (int i = 0; i < 8; ++i) m[i] = sh_->src[SrcM1 + i];
    }
    // the flyby: tau runs -1 (far, before) .. 0 (closest) .. +1 (far, after); delta = how near it passes
    double flyPitch = 0.0, flyGain = 1.0, flyPan = 0.0, flyCut = 0.0;
    if (flyOn_ && sh_ && sh_->flyMode != FlybyOff) {
        const double T = std::max(0.01, sh_->flyTime), u = flyT_ / T, dl = sh_->flyDelta, dep = sh_->flyDepth;
        double tau;
        switch (sh_->flyMode) {
            case FlybyArrive: tau = std::min(0.0, -1.0 + u); break;
            case FlybyPass:   tau = std::min(1.0, -1.0 + 2.0 * u); break;
            default:          tau = std::min(1.0, u); break;   // Leave
        }
        const double pos = tau / std::sqrt(tau * tau + dl * dl), near = dl / std::sqrt(dl * dl + tau * tau);
        flyPitch = -dep * kFlyPitch * pos;            // Doppler: higher while it comes, lower while it goes, the true pitch at the closest point
        flyGain = 1.0 - dep * (1.0 - near);           // inverse distance
        flyPan = flySign_ * dep * pos;                // across the stereo field
        flyCut = -kFlyCutOct * dep * (1.0 - near);    // the air takes the highs with distance
        flyT_ += kCtl / fs_;
    }
    // pitch (glide: linear in semitones over a constant time)
    if (glideLeft_ > 0) {   // the key at the end of this control period (it lands on the note when the glide time is up)
        glideLeft_ = std::max(0, glideLeft_ - kCtl);
        const double prog = 1.0 - static_cast<double>(glideLeft_) / glideN_;
        key_ = glideFrom_ + (note_ - glideFrom_) * prog;
    } else {
        key_ = note_;
    }
    pitch_ = key_ + 12.0 * p(Octave) + p(Semi) + p(Fine) / 100.0 + (sh_ ? sh_->bendSemis : 0.0) + kModPitch * md[DstPitch] + flyPitch;
    const double f = frequency();
    const double detPct = std::clamp(p(Detune) + kModRange * md[DstDetune] + kMacroRange * m[5], 0.0, 100.0);
    const double det = detPct / 100.0 * kDetuneCents, spread = p(Spread) / 100.0;
    const int wave = static_cast<int>(std::lround(p(Wave)));
    const double pw = std::clamp(p(PulseWidth) + kModPw * md[DstPulseWidth], 5.0, 95.0) / 100.0;
    // gravity: the copies pull toward their common phase (Kuramoto coupling, updated here at control rate)
    const double grav = std::clamp((p(Gravity) + kModRange * md[DstGravity]) / 100.0, 0.0, 1.0);
    double lo = 1.0, hi = 0.0;
    for (int i = 0; i < unison_; ++i) {
        const size_t k = static_cast<size_t>(i);
        const double u = unison_ > 1 ? 2.0 * i / (unison_ - 1) - 1.0 : 0.0;   // -1 .. +1
        inc0_[k] = std::min(0.45, f * std::exp2(det * u / 1200.0) / fs_);
        lo = std::min(lo, inc0_[k]); hi = std::max(hi, inc0_[k]);
    }
    double cEff = 0.0;
    if (unison_ > 1 && grav > 0.0 && otype_ != OscSample) {
        double X = 0.0, Y = 0.0;
        for (int i = 0; i < unison_; ++i) { const double th = 2.0 * kPi * phaseOf(i); X += std::cos(th); Y += std::sin(th); }
        X /= unison_; Y /= unison_;
        const double r = std::sqrt(X * X + Y * Y), psi = std::atan2(Y, X);
        const double K = grav * ((hi - lo) + kGravityFloorHz / fs_);   // cycles per sample: at 100 % enough to hold the widest copies
        for (int i = 0; i < unison_; ++i) {
            const size_t k = static_cast<size_t>(i);
            inc0_[k] = std::clamp(inc0_[k] + K * r * std::sin(psi - 2.0 * kPi * phaseOf(i)), 0.0, 0.45);
        }
        coherence_ = r;
        const double N = unison_, c = std::max(0.0, (r * r * N - 1.0) / (N - 1.0));
        cEff = c * std::min(1.0, grav / 0.2);   // the level follows the coherence (a locked stack is not louder), fully from 20 % on
    } else {
        coherence_ = unison_ > 1 ? 0.0 : 1.0;
    }
    const double norm = 1.0 / std::sqrt(unison_ + (static_cast<double>(unison_) * unison_ - unison_) * cEff);
    bool stereo = false;
    for (int i = 0; i < unison_; ++i) {
        const size_t k = static_cast<size_t>(i);
        const double u = unison_ > 1 ? 2.0 * i / (unison_ - 1) - 1.0 : 0.0;
        if (otype_ == OscAnalog) { osc_[k].setWave(wave); osc_[k].setPulseWidth(pw); osc_[k].setIncrement(inc0_[k]); }
        const double pan = u * spread;
        if (std::abs(pan) < 1e-12) { gl_[k] = gr_[k] = norm; }
        else { const double th = (pan + 1.0) * kPi / 4.0; gl_[k] = norm * kSqrt2 * std::cos(th); gr_[k] = norm * kSqrt2 * std::sin(th); stereo = true; }
    }
    // the other types' settings (ramped across the period where a jump would click)
    switch (otype_) {
        case OscWavetable: {
            const WaveBank& wb = waveBank();
            table_ = std::clamp(static_cast<int>(std::lround(p(Table))), 0, kWaveTables - 1);
            const double pos = std::clamp((p(Position) + kModRange * md[DstWtPos]) / 100.0, 0.0, 1.0) * (WaveBank::kFrames - 1);
            wp0_ = first_ ? pos : wp1_; wp1_ = pos;
            for (int i = 0; i < unison_; ++i) {
                const size_t k = static_cast<size_t>(i);
                const int lev = wb.level(inc0_[k]);
                wbase_[k] = wb.at(table_, 0, lev); wsize_[k] = wb.size[static_cast<size_t>(lev)];
            }
            break;
        }
        case OscFm: {
            fmRatio_ = fmRatioOf(static_cast<int>(std::lround(p(FmRatio))));
            double hiInc = 0.0;
            for (int i = 0; i < unison_; ++i) hiInc = std::max(hiInc, inc0_[static_cast<size_t>(i)]);
            const double lim = fmIndexLimit(hiInc, fmRatio_), toCyc = 1.0 / (2.0 * kPi);
            const double base = std::clamp((p(FmIndex) + kModRange * md[DstFmIndex]) / 100.0, 0.0, 1.0) * kFmIndexMax;
            const double start = first_ ? std::min(base, lim) * toCyc : fi1_;
            if (p(FmDecay) < 10000.0 - 1e-6) fmEnv_ *= std::exp(std::log(1e-3) * kCtl / (p(FmDecay) * 1e-3 * fs_));   // to -60 dB in the set time
            fi1_ = std::min(base * fmEnv_, lim) * toCyc; fi0_ = start;
            const double beta = std::min(p(FmFeedback) / 100.0 * kFmFeedbackMax, fmFeedbackLimit(hiInc, fmRatio_, fi1_ / toCyc)) * toCyc;
            fb0k_ = first_ ? beta : fb1k_; fb1k_ = beta;
            break;
        }
        case OscSample: {
            const auto& smp = sampleBank().s[static_cast<size_t>(sample_)];
            for (int i = 0; i < unison_; ++i) {
                const size_t k = static_cast<size_t>(i);
                const double speed = inc0_[k] * fs_ / SampleBank::kRootHz;
                double blend = 0.0;
                const int lev = SampleBank::level(speed, fs_, blend);
                sptr_[k] = smp.at(lev); sscale_[k] = 2.0 / static_cast<double>(1 << lev);   // stored samples per sample at kRate
                sptr2_[k] = blend > 0.0 ? smp.at(lev + 1) : nullptr; sblend_[k] = blend;
                sinc_[k] = speed * SampleBank::kRate / fs_;
            }
            break;
        }
        default: break;
    }

    // the layer: level (the bottom of the range is Off) and pan, equal power with the centre at unity; tremolo-like level modulation
    const double levMod = std::max(0.0, 1.0 + md[DstLevel]) * std::max(0.0, 1.0 + md[DstL1Level + std::clamp(layer_, 0, 3)]);
    if (p(LayerLevel) != levDb_) { levDb_ = p(LayerLevel); levGain_ = levDb_ <= -60.0 + 1e-9 ? 0.0 : dbToGain(levDb_); }
    const double lev = levGain_ * levMod * flyGain;
    const double pan = std::clamp(p(Pan) / 100.0 + md[DstPan] + flyPan, -1.0, 1.0);
    double nl = lev, nr = lev;
    if (std::abs(pan) > 1e-12) { const double th = (pan + 1.0) * kPi / 4.0; nl = lev * kSqrt2 * std::cos(th); nr = lev * kSqrt2 * std::sin(th); stereo = true; }
    gL0_ = first_ ? nl : gL_; gR0_ = first_ ? nr : gR_;   // ramp from the last period's gains (the first period of a note starts there)
    gL_ = nl; gR_ = nr;
    if (stereo && !stereo_) { svf_[1] = svf_[0]; os_[1] = os_[0]; }   // the right chain takes over the left chain's state: no click
    stereo_ = stereo;

    amp_.setTimes(p(AmpA) * 1e-3 * (m[2] == 0.0 ? 1.0 : std::pow(4.0, m[2])), p(AmpD) * 1e-3, p(AmpS) / 100.0,
                  p(AmpR) * 1e-3 * (m[3] == 0.0 ? 1.0 : std::pow(4.0, m[3])));
    fenv_.setTimes(p(FenvA) * 1e-3, p(FenvD) * 1e-3, p(FenvS) / 100.0, p(FenvR) * 1e-3);

    // filter
    const double oct = kEnvOctaves * p(FilterEnv) / 100.0 * fenv_.level() + p(KeyTrack) / 100.0 * (pitch_ - 60.0) / 12.0
                     + kEnvOctaves * md[DstCutoff] + 2.0 * m[0] + flyCut;
    cutoff_ = std::clamp(p(Cutoff) * std::exp2(oct), 20.0, 0.45 * fs_);
    const int type = static_cast<int>(std::lround(p(FilterType)));
    if (type != type_) { for (auto& ch : svf_) ch[1].reset(); type_ = type; }
    const double r = std::clamp((p(Resonance) + kModRange * md[DstResonance] + kMacroRange * m[1]) / 100.0, 0.0, 1.0);
    const int n = first_ ? 0 : kCtl;   // the first update of a note is immediate
    for (int c = 0; c < 2; ++c) {
        auto& st = svf_[static_cast<size_t>(c)];
        switch (type_) {
            case LP24:
                st[0].setupRamp(Svf::Mode::LowPass, cutoff_, fs_, kQ24a, 0.0, n);
                st[1].setupRamp(Svf::Mode::LowPass, cutoff_, fs_, kQ24b * std::pow(kQMax / kQ24b, r), 0.0, n);
                break;
            case BP12: st[0].setupRamp(Svf::Mode::BandPass, cutoff_, fs_, kQ12 * std::pow(kQMax / kQ12, r), 0.0, n); break;
            case HP12: st[0].setupRamp(Svf::Mode::HighPass, cutoff_, fs_, kQ12 * std::pow(kQMax / kQ12, r), 0.0, n); break;
            default:   st[0].setupRamp(Svf::Mode::LowPass, cutoff_, fs_, kQ12 * std::pow(kQMax / kQ12, r), 0.0, n); break;
        }
    }
    first_ = false;
    drive_ = std::clamp((p(Drive) + kModRange * md[DstDrive] + kMacroRange * m[4]) / 100.0, 0.0, 1.0);
}

void Voice::oscillate(double* xl, double* xr, int m, int done) {
    const int U = unison_;
    switch (otype_) {
        case OscWavetable: {   // two frames, cubic (Hermite) between samples, linear between frames; the position ramps across the period
            const size_t stride = waveBank().stride;
            const double dw = (wp1_ - wp0_) / kCtl;
            double w = wp0_ + dw * done;
            for (int i = 0; i < m; ++i) {
                w += dw;
                const int f = std::clamp(static_cast<int>(w), 0, WaveBank::kFrames - 2);
                const double fw = w - f;
                double sl = 0.0, sr = 0.0;
                for (int u = 0; u < U; ++u) {
                    const size_t k = static_cast<size_t>(u);
                    const float* A = wbase_[k] + static_cast<size_t>(f) * stride;
                    const float* B = A + stride;
                    const double x = ph_[k] * wsize_[k];
                    const int j = static_cast<int>(x);
                    const double fr = x - j;
                    const double a = hermite(A + j, fr), b = hermite(B + j, fr);
                    const double s = a + fw * (b - a);
                    double ph = ph_[k] + inc0_[k];
                    if (ph >= 1.0) ph -= 1.0;
                    ph_[k] = ph;
                    sl += gl_[k] * s; sr += gr_[k] * s;
                }
                xl[i] = sl; xr[i] = sr;
            }
            break;
        }
        case OscFm: {   // modulator (with feedback: the average of its last two outputs) -> carrier phase; index and feedback ramp across the period
            const float* T = sineTable();
            const double di = (fi1_ - fi0_) / kCtl, db = (fb1k_ - fb0k_) / kCtl, R = fmRatio_;
            double I = fi0_ + di * done, B = fb0k_ + db * done;
            for (int i = 0; i < m; ++i) {
                I += di; B += db;
                double sl = 0.0, sr = 0.0;
                for (int u = 0; u < U; ++u) {
                    const size_t k = static_cast<size_t>(u);
                    const double mo = sinCycles(T, pm_[k] + 0.5 * B * (fb0_[k] + fb1_[k]));
                    fb1_[k] = fb0_[k]; fb0_[k] = mo;
                    const double s = sinCycles(T, ph_[k] + I * mo);
                    double ph = ph_[k] + inc0_[k];
                    if (ph >= 1.0) ph -= 1.0;
                    ph_[k] = ph;
                    double q = pm_[k] + R * inc0_[k];
                    if (q >= 1.0) q -= static_cast<double>(static_cast<int>(q));
                    pm_[k] = q;
                    sl += gl_[k] * s; sr += gr_[k] * s;
                }
                // a sideband can land on 0 Hz (ratio 1 or 0.5) and feedback shifts its phase off the zero: a 5 Hz DC blocker
                const double yl = sl - dcx_[0] + dca_ * dcy_[0], yr = sr - dcx_[1] + dca_ * dcy_[1];
                dcx_[0] = sl; dcy_[0] = yl; dcx_[1] = sr; dcy_[1] = yr;
                xl[i] = yl; xr[i] = yr;
            }
            break;
        }
        case OscSample: {   // the 8-tap sinc read in the level chosen for the speed; loops wrap, one-shots stop
            const auto& smp = sampleBank().s[static_cast<size_t>(sample_)];
            const SincTable& kt = sincTable();
            const double len = smp.length, end = smp.length + SampleBank::kTail;
            const bool loop = smp.loop;
            for (int i = 0; i < m; ++i) {
                double sl = 0.0, sr = 0.0;
                for (int u = 0; u < U; ++u) {
                    const size_t k = static_cast<size_t>(u);
                    if (sdone_[k]) continue;
                    const double x = spos_[k] * sscale_[k];
                    double s = sampleRead(kt, sptr_[k], x);
                    if (sptr2_[k]) s += sblend_[k] * (sampleRead(kt, sptr2_[k], 0.5 * x) - s);
                    double q = spos_[k] + sinc_[k];
                    if (loop) { if (q >= len) q -= len; }
                    else if (q >= end) sdone_[k] = true;
                    spos_[k] = q;
                    sl += gl_[k] * s; sr += gr_[k] * s;
                }
                xl[i] = sl; xr[i] = sr;
            }
            break;
        }
        default:
            for (int i = 0; i < m; ++i) {
                double sl = 0.0, sr = 0.0;
                for (int u = 0; u < U; ++u) {
                    const size_t k = static_cast<size_t>(u);
                    const double s = osc_[k].next();
                    sl += gl_[k] * s; sr += gr_[k] * s;
                }
                xl[i] = sl; xr[i] = sr;
            }
            break;
    }
}

void Voice::render(float* l, float* r, int n) {
    if (!amp_.active()) return;   // sleep
    double xs[2][kCtl];
    int off = 0;
    while (off < n) {
        if (ctl_ == 0) { control(); ctl_ = (align_ > 0 && align_ < kCtl) ? align_ : kCtl; align_ = 0; }
        const int m = std::min(n - off, ctl_);
        const int done = std::max(0, kCtl - ctl_);   // samples of this control period already played
        oscillate(xs[0], xs[1], m, done);
        const double d = drive_, g = 1.0 + 9.0 * d;
        const bool lp24 = type_ == LP24;
        // the layer gains ramp linearly across the control period
        const double dl = (gL_ - gL0_) / kCtl, dr = (gR_ - gR0_) / kCtl;
        double cl = gL0_ + dl * done, cr = gR0_ + dr * done;
        for (int i = 0; i < m; ++i) {
            const int chans = stereo_ ? 2 : 1;
            double y[2] = {xs[0][i], xs[1][i]};
            for (int c = 0; c < chans; ++c) {
                const size_t cc = static_cast<size_t>(c);
                double x = y[c];
                if (oversample_) {   // drive at 2x: unity gain for small signals
                    double up[2];
                    os_[cc].up(x, up);
                    for (double& v : up) v += d * (fastTanh(g * v) / g - v);
                    x = os_[cc].down(up);
                }
                x = svf_[cc][0].process(x);
                if (lp24) x = svf_[cc][1].process(x);
                y[c] = x;
            }
            if (!stereo_) y[1] = y[0];
            fenv_.next();
            cl += dl; cr += dr;
            const double a = amp_.next() * velGain_;
            l[off + i] += static_cast<float>(y[0] * a * cl);
            r[off + i] += static_cast<float>(y[1] * a * cr);
        }
        ctl_ -= m; off += m;
        if (!amp_.active()) {   // the release ended inside this block: the rest stays silent
            fenv_.reset();
            for (auto& ch : svf_) for (auto& f : ch) f.reset();
            for (auto& o : os_) o.reset();
            killed_ = false;
            break;
        }
    }
}

// ---- the synth: notes, layers, voice allocation
bool Processor::Slot::sounding() const {
    for (const auto& x : v) if (x.active()) return true;
    return false;
}

Processor::Processor() : slots_(static_cast<size_t>(kSlots)) {
    for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def;
    for (int l = 0; l < kLayers; ++l) layerOn_[static_cast<size_t>(l)] = target_[static_cast<size_t>(lp(l, On))] > 0.5;
    held_.reserve(128);
}

void Processor::prepare(double sampleRate, int maxBlock) {
    staging_ = false; fading_ = false;
    applyStaged();     // a patch change that was waiting goes in now (nothing sounds after a prepare)
    (void)minBlep();   // build the tables here, not on the audio thread
    (void)waveBank(); (void)sampleBank(); (void)sineTable(); (void)sincTable();
    fs_ = sampleRate;
    const size_t m = static_cast<size_t>(std::max(1, maxBlock));
    l_.assign(m, 0.0f); r_.assign(m, 0.0f);
    held_.clear(); monoSlot_ = -1; pedal_ = false; bend_ = 0.0; lastKey_ = -1.0; clock_ = 0;
    endHead_ = endTail_ = 0;
    level_.reset(fs_, 20.0, dbToGain(target_[Level]));
    shared_.bendSemis = 0.0; shared_.glideMs = p(Glide);
    mode_ = static_cast<int>(std::lround(p(Mode)));
    for (auto& s : slots_) {
        for (int l = 0; l < kLayers; ++l) s.v[static_cast<size_t>(l)].prepare(fs_, &target_[static_cast<size_t>(lp(l, 0))], &shared_, l);
        s.key = -1; s.held = s.sustained = s.stolen = false;
    }
    lfoPhase_ = {}; lfoValue_ = {}; lfoSeed_ = {{0x1234567u, 0x7654321u}}; gctl_ = 0; wheel_ = after_ = 0.0; flyFlip_ = 1.0;
    updateMod();
    tick(); gctl_ = 0;   // the sources are ready before the first note (the next sample ticks again, from where it starts)
    updateFx();
    fx_.prepare(fs_, fxParams_);
    fresh_ = true; fxIdle_ = 0;
    prepared_ = true;
}

void Processor::updateFx() {
    static const double kBeats[6] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};   // 1/16, 1/8, 1/8 D, 1/4, 1/4 D, 1/2
    FxParams& f = fxParams_;
    for (int i = 0; i < kFx; ++i) f.on[static_cast<size_t>(i)] = p(fxOnId(i)) > 0.5;
    f.driveAmount = p(FxDriveAmount); f.driveTone = p(FxDriveTone); f.driveMix = p(FxDriveMix);
    f.chorusRate = p(FxChorusRate); f.chorusDepth = p(FxChorusDepth); f.chorusMix = p(FxChorusMix);
    f.delayBeats = kBeats[std::clamp(static_cast<int>(std::lround(p(FxDelayTime))), 0, 5)]; f.delayFeedback = p(FxDelayFeedback); f.delayMix = p(FxDelayMix);
    // M7 Delay and M8 Reverb: +-50 points of mix from the middle
    f.delayMix = std::clamp(f.delayMix + kMacroRange * (p(Macro7) - 50.0) / 50.0, 0.0, 100.0);
    f.reverbSize = p(FxReverbSize); f.reverbDamp = p(FxReverbDamp);
    f.reverbMix = std::clamp(p(FxReverbMix) + kMacroRange * (p(Macro8) - 50.0) / 50.0, 0.0, 100.0);
    f.eqLow = p(FxEqLow); f.eqMid = p(FxEqMid); f.eqHigh = p(FxEqHigh);
    f.limitGain = p(FxLimitGain); f.limitCeiling = p(FxLimitCeiling); f.limitRelease = p(FxLimitRelease);
    fx_.setParams(f);
    // the order: the slots in turn, each effect the first time it appears; the ones no slot names follow in their default order
    std::array<int, kFx> o{}; std::array<bool, kFx> used{};
    int n = 0;
    for (int i = 0; i < kFx; ++i) {
        const int e = std::clamp(static_cast<int>(std::lround(p(FxSlot1 + i))), 0, kFx - 1);
        if (!used[static_cast<size_t>(e)]) { used[static_cast<size_t>(e)] = true; o[static_cast<size_t>(n++)] = e; }
    }
    for (int e = 0; e < kFx; ++e) if (!used[static_cast<size_t>(e)]) o[static_cast<size_t>(n++)] = e;
    fx_.setOrder(o);
}

void Processor::setParam(int id, double v) {
    if (id < 0 || id >= kNumParams) return;
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    if (staging_ || fading_) {   // part of a patch change, or set while one fades: waits for the change
        staged_[static_cast<size_t>(id)] = v; stagedSet_[static_cast<size_t>(id)] = true; anyStaged_ = true;
        return;
    }
    setNow(id, v);
}

void Processor::applyStaged() {
    if (!anyStaged_) return;
    anyStaged_ = false;
    for (int i = 0; i < kNumParams; ++i)
        if (stagedSet_[static_cast<size_t>(i)]) { stagedSet_[static_cast<size_t>(i)] = false; setNow(i, staged_[static_cast<size_t>(i)]); }
}

void Processor::endPatch() {
    staging_ = false;
    if (fading_) return;   // a fade under way takes these values too
    if (!prepared_ || fresh_ || (!active() && fxIdle_ > 0)) { applyStaged(); return; }   // silent (no voice, effects under -120 dBFS): at once
    fadeLen_ = std::max(1, static_cast<int>(std::lround(kPatchFadeMs * 1e-3 * fs_)));
    fadeLeft_ = fadeLen_;
    fading_ = true;
}

// the end of a patch change's fade (the output is silent here): the new values go in, the voices and the effects start again from
// silence, and the notes still held (keys, or the pedal) play again with the new patch, oldest first. Notes that do not play again
// (released, in their tail) end and are reported; the ones that do keep their note ids and are not reported.
void Processor::swapPatch() {
    struct Again { int key, channel, noteId, slot; double vel; bool held; };
    std::array<Again, kSlots + 128> again{};
    int na = 0;
    if (mode_ == Poly) {
        for (int i = 0; i < kSlots; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if (s.key >= 0 && !s.stolen && (s.held || s.sustained)) again[static_cast<size_t>(na++)] = {s.key, s.channel, s.noteId, i, s.vel, s.held};
        }
        std::sort(again.begin(), again.begin() + na, [&](const Again& a, const Again& b) {
            return slots_[static_cast<size_t>(a.slot)].age < slots_[static_cast<size_t>(b.slot)].age; });
    } else {
        const Slot* ms = monoSlot_ >= 0 ? &slots_[static_cast<size_t>(monoSlot_)] : nullptr;
        if (ms && (ms->key < 0 || ms->stolen)) ms = nullptr;
        for (size_t k = 0; k < held_.size(); ++k) {   // the keys held, in order; the sounding one keeps its slot's id
            const bool top = ms && ms->key == held_[k];
            again[static_cast<size_t>(na++)] = {held_[k], top ? ms->channel : 0, top ? ms->noteId : -1, top ? monoSlot_ : -1, top ? ms->vel : lastVel_, true};
        }
        if (held_.empty() && ms && ms->sustained)   // only the pedal holds it
            again[static_cast<size_t>(na++)] = {ms->key, ms->channel, ms->noteId, monoSlot_, ms->vel, false};
    }
    // silence: every voice; the notes that do not come back end
    const int newMode = static_cast<int>(std::lround(stagedSet_[Mode] ? staged_[Mode] : target_[Mode]));
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[static_cast<size_t>(i)];
        for (auto& x : s.v) x.reset();
        if (s.key < 0) continue;
        bool back = false;   // poly: every note in the list sounds again; mono / legato: only the last one
        for (int a = 0; a < na; ++a)
            if (again[static_cast<size_t>(a)].slot == i && (newMode == Poly || a == na - 1)) back = true;
        if (back) { s.key = -1; s.held = s.sustained = s.stolen = false; }
        else end(s);
    }
    held_.clear(); monoSlot_ = -1;
    fading_ = false;
    const bool pedal = pedal_;
    applyStaged();      // (a change of Mode lets all notes go, the pedal too: it is still down)
    pedal_ = pedal;
    level_.reset(fs_, 20.0, dbToGain(target_[Level]));
    fx_.restart();
    fxIdle_ = 0;
    // play again: poly, every note (no glide between them); mono / legato, the last one, with the others held behind it
    if (na == 0) return;
    if (mode_ == Poly) {
        for (int a = 0; a < na; ++a) {
            const Again& g = again[static_cast<size_t>(a)];
            Slot* s = allocate();
            start(*s, g.key, g.vel, g.channel, g.noteId, -1.0);
            if (!g.held) release(*s);   // the pedal holds it
        }
    } else {
        for (int a = 0; a + 1 < na; ++a) if (again[static_cast<size_t>(a)].held && held_.size() < held_.capacity()) held_.push_back(again[static_cast<size_t>(a)].key);
        const Again& g = again[static_cast<size_t>(na - 1)];
        Slot* s = allocate();
        start(*s, g.key, g.vel, g.channel, g.noteId, -1.0);
        monoSlot_ = static_cast<int>(s - slots_.data());
        if (g.held) { if (held_.size() < held_.capacity()) held_.push_back(g.key); }
        else release(*s);
    }
    lastKey_ = again[static_cast<size_t>(na - 1)].key;
}

void Processor::setNow(int id, double v) {
    target_[static_cast<size_t>(id)] = v;
    if (id == PresetSelect) return;   // kept only: the plug-in layer loads presets
    if (id >= kFxEnd) { updateMod(); if (id == Macro7 || id == Macro8) updateFx(); return; }
    if (id >= kFxBase) { updateFx(); if (fresh_) fx_.snapSwitches(); return; }
    if (id == Level) level_.setTarget(dbToGain(v));
    else if (id == Glide) shared_.glideMs = v;
    else if (id == Bend) shared_.bendSemis = bend_ * v;
    else if (id == Mode) {
        const int m = static_cast<int>(std::lround(v));
        if (m != mode_ && prepared_) allNotesOff();   // switching between poly and mono lets the notes go
        mode_ = m;
    } else if (id >= kNumGlobal && (id - kNumGlobal) % kLayerParams == On) {
        const int l = (id - kNumGlobal) / kLayerParams;
        const bool on = v > 0.5;
        if (layerOn_[static_cast<size_t>(l)] && !on && prepared_)   // a layer turned off: its sounding voices fade
            for (auto& s : slots_) { Voice& x = s.v[static_cast<size_t>(l)]; if (x.active()) x.kill(); }
        layerOn_[static_cast<size_t>(l)] = on;
    }
}

void Processor::updateMod() {
    int n = 0;
    shared_.driveRouted = std::abs(p(Macro5) - 50.0) > 1e-9;
    for (int i = 0; i < kModSlots; ++i) {
        const int src = std::clamp(static_cast<int>(std::lround(p(modId(i, ModSrc)))), 0, kModSources - 1);
        const int dst = std::clamp(static_cast<int>(std::lround(p(modId(i, ModDst)))), 0, kModDests - 1);
        const double amt = p(modId(i, ModAmount)) / 100.0;
        if (p(modId(i, ModOn)) < 0.5 || src == SrcNone || dst == DstNone || amt == 0.0) continue;
        shared_.slots[static_cast<size_t>(n++)] = {src, dst, amt};
        if (dst == DstDrive) shared_.driveRouted = true;
    }
    shared_.nSlots = n;
    for (int i = 0; i < 8; ++i) shared_.src[SrcM1 + i] = (p(Macro1 + i) - 50.0) / 50.0;
    shared_.flyMode = std::clamp(static_cast<int>(std::lround(p(FlybyMode))), 0, 3);
    shared_.flyDepth = p(FlybyDepth) / 100.0;
    shared_.flyTime = p(FlybyTime);
    shared_.flyDelta = 0.05 + 0.6 * (1.0 - p(FlybyNear) / 100.0);
}

// every 32 samples on the synth's own grid (the same whatever the host's block size): the LFOs and the global sources
void Processor::tick() {
    static const double kSyncBeats[11] = {0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0};
    shared_.src[SrcModWheel] = wheel_; shared_.src[SrcAftertouch] = after_;
    // the LFO rates may be modulated by the global sources (an LFO by the other, the wheel, the macros); per-voice sources count 0 here
    double rateMod[2] = {0.0, 0.0};
    for (int i = 0; i < shared_.nSlots; ++i) {
        const auto& sl = shared_.slots[static_cast<size_t>(i)];
        if (sl.dst != DstLfo1Rate && sl.dst != DstLfo2Rate) continue;
        if (sl.src == SrcEnv2 || sl.src == SrcVelocity || sl.src == SrcKey) continue;
        rateMod[sl.dst == DstLfo1Rate ? 0 : 1] += sl.amount * shared_.src[sl.src];
    }
    for (int k = 0; k < 2; ++k) {
        const int base = k == 0 ? Lfo1Shape : Lfo2Shape;
        const int sync = std::clamp(static_cast<int>(std::lround(p(base + 2))), 0, 10);
        double hz = sync > 0 ? bpm_ / 60.0 / kSyncBeats[sync] : p(base + 1);
        hz *= std::exp2(4.0 * rateMod[k]);   // +-4 octaves at 100 %
        lfoValue_[static_cast<size_t>(k)] = lfoShape(static_cast<int>(std::lround(p(base))), lfoPhase_[static_cast<size_t>(k)], p(base + 3) / 100.0, lfoSeed_[static_cast<size_t>(k)]);
        double ph = lfoPhase_[static_cast<size_t>(k)] + std::clamp(hz, 0.0, 100.0) * Voice::kCtl / fs_;
        if (ph >= 1.0) { ph -= std::floor(ph); lfoSeed_[static_cast<size_t>(k)] = lfoSeed_[static_cast<size_t>(k)] * 1664525u + 1013904223u; }   // Random: a new value each cycle
        lfoPhase_[static_cast<size_t>(k)] = ph;
    }
    shared_.src[SrcLfo1] = lfoValue_[0]; shared_.src[SrcLfo2] = lfoValue_[1];
}

void Processor::modWheel(double v) { wheel_ = std::clamp(v, 0.0, 1.0); }
void Processor::aftertouch(double v) { after_ = std::clamp(v, 0.0, 1.0); }

void Processor::pushEnded(int key, int channel, int noteId) {
    if (endHead_ - endTail_ >= kEnded) ++endTail_;   // full: the oldest report is dropped
    ended_[endHead_ % kEnded] = {key, channel, noteId};
    ++endHead_;
}

bool Processor::takeEnded(int& key, int& channel, int& noteId) {
    if (endTail_ == endHead_) return false;
    const Ended& e = ended_[endTail_ % kEnded];
    key = e.key; channel = e.channel; noteId = e.noteId;
    ++endTail_;
    return true;
}

void Processor::end(Slot& s) {
    if (s.key >= 0) pushEnded(s.key, s.channel, s.noteId);
    s.key = -1; s.held = s.sustained = s.stolen = false;
}

void Processor::start(Slot& s, int key, double vel, int channel, int noteId, double glideFrom) {
    s.key = key; s.channel = channel; s.noteId = noteId; s.vel = vel;
    s.held = true; s.sustained = false; s.stolen = false; s.age = ++clock_;
    for (int l = 0; l < kLayers; ++l)
        if (layerOn_[static_cast<size_t>(l)]) s.v[static_cast<size_t>(l)].noteOn(key, vel, glideFrom);
}

void Processor::release(Slot& s) {
    s.held = false;
    if (pedal_) { s.sustained = true; return; }
    s.sustained = false;
    for (auto& x : s.v) x.noteOff();
}

// a free slot; at the voice limit the oldest released note (else the oldest held one) is faded out first
Processor::Slot* Processor::allocate() {
    const int limit = std::clamp(static_cast<int>(std::lround(p(Voices))), 1, kMaxVoices);
    int count = 0;
    for (const auto& s : slots_) if (s.key >= 0 && !s.stolen) ++count;
    while (count >= limit) {
        Slot* victim = nullptr;
        for (auto& s : slots_) {
            if (s.key < 0 || s.stolen) continue;
            const bool rel = !s.held && !s.sustained, vrel = victim && !victim->held && !victim->sustained;
            if (!victim || (rel && !vrel) || (rel == vrel && s.age < victim->age)) victim = &s;
        }
        if (!victim) break;
        victim->stolen = true;
        for (auto& x : victim->v) if (x.active()) x.kill();
        --count;
    }
    for (auto& s : slots_) if (s.key < 0 && !s.sounding()) return &s;
    // every slot busy (all spares still fading): cut the oldest fading one
    Slot* oldest = nullptr;
    for (auto& s : slots_) if (s.stolen && (!oldest || s.age < oldest->age)) oldest = &s;
    if (!oldest) oldest = &slots_[0];
    for (auto& x : oldest->v) x.reset();
    end(*oldest);
    return oldest;
}

void Processor::noteOn(int key, double velocity, int channel, int noteId) {
    if (!prepared_ || key < 0 || key > 127) return;
    shared_.gridLeft = gctl_;
    lastVel_ = velocity;
    bool any = false;
    for (bool on : layerOn_) any = any || on;
    if (!any) { pushEnded(key, channel, noteId); return; }   // nothing to play: the note ends at once
    // LFOs set to Note restart with a note played while no key is held
    bool keysHeld = !held_.empty();
    for (const auto& s : slots_) keysHeld = keysHeld || (s.key >= 0 && !s.stolen && s.held);
    if (!keysHeld)
        for (int k = 0; k < 2; ++k)
            if (p(k == 0 ? Lfo1Trigger : Lfo2Trigger) > 0.5) { lfoPhase_[static_cast<size_t>(k)] = 0.0; gctl_ = 0; }
    // the flyby's side: fixed, or turn about with each note
    const int side = static_cast<int>(std::lround(p(FlybySide)));
    if (side == 2) { shared_.flySign = flyFlip_; flyFlip_ = -flyFlip_; } else shared_.flySign = side == 0 ? 1.0 : -1.0;
    if (mode_ != Poly) { monoOn(key, velocity, channel, noteId); return; }
    // poly glide: from the previous note while one is held
    bool anyHeld = false;
    for (const auto& s : slots_) anyHeld = anyHeld || (s.key >= 0 && !s.stolen && s.held);
    const double from = anyHeld ? lastKey_ : -1.0;
    lastKey_ = key;
    for (auto& s : slots_)   // the same key again: its own slot re-triggers (from the current level)
        if (s.key == key && s.channel == channel && !s.stolen && s.sounding()) {
            if (s.noteId != noteId) pushEnded(s.key, s.channel, s.noteId);   // the old note id has no voice any more
            start(s, key, velocity, channel, noteId, from);
            return;
        }
    Slot* s = allocate();
    start(*s, key, velocity, channel, noteId, from);
}

void Processor::noteOff(int key, int channel) {
    if (!prepared_) return;
    shared_.gridLeft = gctl_;
    if (mode_ != Poly) { monoOff(key); return; }
    for (auto& s : slots_)
        if (s.key >= 0 && !s.stolen && s.held && (key < 0 || s.key == key) && (channel < 0 || s.channel == channel)) release(s);
}

void Processor::choke(int key, int channel) {
    if (!prepared_) return;
    for (auto& s : slots_)
        if (s.key >= 0 && !s.stolen && (key < 0 || s.key == key) && (channel < 0 || s.channel == channel)) {
            s.stolen = true; s.held = s.sustained = false;
            for (auto& x : s.v) if (x.active()) x.kill();
        }
    if (mode_ != Poly) { held_.clear(); monoSlot_ = -1; }
}

// mono / legato: one slot, last-note priority
void Processor::monoOn(int key, double vel, int channel, int noteId) {
    const bool legatoHeld = !held_.empty();
    held_.erase(std::remove(held_.begin(), held_.end(), key), held_.end());
    if (held_.size() < held_.capacity()) held_.push_back(key);
    Slot* s = monoSlot_ >= 0 ? &slots_[static_cast<size_t>(monoSlot_)] : nullptr;
    if (s && (s->stolen || s->key < 0 || !s->sounding())) s = nullptr;
    if (s) {
        double cur = -1.0;   // the (gliding) key in use: a glide continues from where it is
        for (const auto& x : s->v) if (x.active()) { cur = x.keyInUse(); break; }
        if (s->key != key || s->noteId != noteId) pushEnded(s->key, s->channel, s->noteId);   // the old note has no voice any more
        s->key = key; s->channel = channel; s->noteId = noteId; s->vel = vel; s->held = true; s->sustained = false; s->age = ++clock_;
        if (mode_ == Legato && legatoHeld) {
            for (auto& x : s->v) if (x.active()) x.legato(key, cur);
        } else {
            for (int l = 0; l < kLayers; ++l)
                if (layerOn_[static_cast<size_t>(l)]) s->v[static_cast<size_t>(l)].noteOn(key, vel, mode_ == Mono ? cur : -1.0);
        }
        lastKey_ = key;
        return;
    }
    // nothing sounding: a fresh note (the limit is one note)
    for (auto& o : slots_) if (o.key >= 0 && !o.stolen) { o.stolen = true; for (auto& x : o.v) if (x.active()) x.kill(); }
    Slot* n = allocate();
    start(*n, key, vel, channel, noteId, -1.0);
    monoSlot_ = static_cast<int>(n - slots_.data());
    lastKey_ = key;
}

void Processor::monoOff(int key) {
    const bool top = !held_.empty() && (key < 0 || held_.back() == key);
    if (key < 0) held_.clear();
    else held_.erase(std::remove(held_.begin(), held_.end(), key), held_.end());
    if (!top || monoSlot_ < 0) return;
    Slot& s = slots_[static_cast<size_t>(monoSlot_)];
    if (s.key < 0 || s.stolen) return;
    if (held_.empty()) { release(s); return; }
    // back to the key still held
    const int k = held_.back();
    double cur = -1.0;
    for (const auto& x : s.v) if (x.active()) { cur = x.keyInUse(); break; }
    if (s.key != k) pushEnded(s.key, s.channel, s.noteId);
    s.key = k; s.noteId = -1; s.held = true; s.age = ++clock_;
    if (mode_ == Legato) { for (auto& x : s.v) if (x.active()) x.legato(k, cur); }
    else for (int l = 0; l < kLayers; ++l) if (layerOn_[static_cast<size_t>(l)]) s.v[static_cast<size_t>(l)].noteOn(k, lastVel_, cur);
    lastKey_ = k;
}

void Processor::pitchBend(double v) {
    bend_ = std::clamp(v, -1.0, 1.0);
    shared_.bendSemis = bend_ * p(Bend);
}

void Processor::sustain(bool down) {
    pedal_ = down;
    if (down || !prepared_) return;
    for (auto& s : slots_)
        if (s.key >= 0 && !s.stolen && s.sustained) { s.sustained = false; for (auto& x : s.v) x.noteOff(); }
}

void Processor::allNotesOff() {
    if (!prepared_) return;
    pedal_ = false; held_.clear();
    for (auto& s : slots_) if (s.key >= 0 && !s.stolen) { s.held = false; s.sustained = false; for (auto& x : s.v) x.noteOff(); }
}

void Processor::allSoundOff() {
    if (!prepared_) return;
    pedal_ = false; held_.clear(); monoSlot_ = -1;
    for (auto& s : slots_) { for (auto& x : s.v) x.reset(); if (s.key >= 0) end(s); }
}

bool Processor::active() const {
    for (const auto& s : slots_) if (s.sounding()) return true;
    return false;
}

bool Processor::fxAsleep() const { return fxIdle_ >= static_cast<int64_t>(0.5 * fs_); }

int Processor::notes() const {
    int n = 0;
    for (const auto& s : slots_) if (s.key >= 0 && !s.stolen && s.sounding()) ++n;
    return n;
}

const Voice* Processor::find(int key, int layer) const {
    if (layer < 0 || layer >= kLayers) return nullptr;
    for (const auto& s : slots_) {
        const Voice& x = s.v[static_cast<size_t>(layer)];
        if (s.key == key && !s.stolen && x.active() && !x.killed()) return &x;
    }
    return nullptr;
}

void Processor::process(float** ch, int numCh, int n) {
    for (int c = 0; c < numCh; ++c) std::fill(ch[c], ch[c] + n, 0.0f);
    if (!prepared_ || n <= 0) return;
    fresh_ = false;
    const int cap = static_cast<int>(l_.size());
    for (int off = 0; off < n;) {
        int m = std::min(cap, n - off);
        if (fading_) m = std::min(m, fadeLeft_);   // the patch change happens at the end of its fade
        std::fill(l_.begin(), l_.begin() + m, 0.0f);
        std::fill(r_.begin(), r_.begin() + m, 0.0f);
        // the slots that sound (notes only start between calls, so the list holds for this block)
        int na = 0;
        for (int i = 0; i < kSlots; ++i) { const Slot& s = slots_[static_cast<size_t>(i)]; if (s.key >= 0 || s.sounding()) active_[static_cast<size_t>(na++)] = i; }
        const bool voices = na > 0;
        for (int pos = 0; pos < m;) {   // the voices in steps of the global control grid
            if (gctl_ == 0) { tick(); gctl_ = Voice::kCtl; }
            const int k = std::min(m - pos, gctl_);
            for (int a = 0; a < na; ++a) {
                Slot& s = slots_[static_cast<size_t>(active_[static_cast<size_t>(a)])];
                for (auto& x : s.v) x.render(l_.data() + pos, r_.data() + pos, k);
                if (s.key >= 0 && !s.sounding()) {   // the note's sound has ended
                    if (active_[static_cast<size_t>(a)] == monoSlot_) monoSlot_ = -1;
                    end(s);
                }
            }
            gctl_ -= k; pos += k;
        }
        // the effects sleep once no voice sounds and their tails have been under -120 dBFS for half a second (no cost while idle)
        if (voices) fxIdle_ = 0;
        if (fxIdle_ < static_cast<int64_t>(0.5 * fs_)) {
            fx_.process(l_.data(), r_.data(), m);
            float pk = 0.0f;
            for (int i = 0; i < m; ++i) pk = std::max({pk, std::abs(l_[static_cast<size_t>(i)]), std::abs(r_[static_cast<size_t>(i)])});
            fxIdle_ = (!voices && pk < 1e-6f) ? fxIdle_ + m : 0;
        } else {
            std::fill(l_.begin(), l_.begin() + m, 0.0f);
            std::fill(r_.begin(), r_.begin() + m, 0.0f);
        }
        for (int i = 0; i < m; ++i) {
            double g = level_.next();
            if (fading_) g *= static_cast<double>(fadeLeft_ - 1 - i) / fadeLen_;   // down to 0 at the fade's last sample
            double yl = l_[static_cast<size_t>(i)] * g, yr = r_[static_cast<size_t>(i)] * g;
            if (std::abs(yl) < 1e-30) yl = 0.0;
            if (std::abs(yr) < 1e-30) yr = 0.0;
            if (numCh == 1) { ch[0][off + i] = static_cast<float>(0.5 * (yl + yr)); continue; }
            ch[0][off + i] = static_cast<float>(yl);
            if (numCh > 1) ch[1][off + i] = static_cast<float>(yr);
        }
        if (fading_) { fadeLeft_ -= m; if (fadeLeft_ <= 0) swapPatch(); }
        off += m;
    }
}

}  // namespace sw::in07
