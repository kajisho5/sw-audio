#include "gt01/gt01.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::gt01 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"gt01.channel",  "Channel",  0, 2, 1,   Curve::Step, 1, {0, 1, 2}, "", {"Clean", "Crunch", "Lead"}},
        {"gt01.gain",     "Gain",     0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.bass",     "Bass",     0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.middle",   "Middle",   0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.treble",   "Treble",   0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.presence", "Presence", 0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.master",   "Master",   0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"gt01.bright",   "Bright",   0, 1, 0,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"gt01.evo.on",   "Volume match", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
// tone stack components (design values: treble pot, bass pot, mid pot, slope resistor, treble cap, bass cap, mid cap)
constexpr double R1 = 250e3, R2 = 1e6, R3 = 25e3, R4 = 56e3, C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
struct ChannelDesign { double gainLoDb, gainHiDb, a2, b1, b2, hp1, hp2, sag, outDb; };
constexpr ChannelDesign kCh[3] = {
    { 0.0, 24.0, 0.7, 0.25, 0.20, 30.0, 30.0, 0.10, 7.0},    // Clean
    {-8.0, 22.0, 3.0, 0.35, 0.30, 70.0, 80.0, 0.30, 0.0},    // Crunch
    {-6.0, 26.0, 6.0, 0.35, 0.30, 140.0, 160.0, 0.22, -5.0}  // Lead
};
constexpr double kReferenceDbfs = -20.0, kMatchLimitDb = 18.0, kMatchSeconds = 5.0, kPlayingGateDbfs = -50.0;
double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
}

double bassPot(double k) { return (std::pow(100.0, std::clamp(k, 0.0, 10.0) / 10.0) - 1.0) / 99.0; }

ToneStackPoly toneStackAnalog(double t, double m, double l) {
    ToneStackPoly p{};
    const double m2 = m * m;
    p.b[0] = 0;
    p.b[1] = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    p.b[2] = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const double c123 = C1 * C2 * C3;
    p.b[3] = l * m * (c123 * R1 * R2 * R3 + c123 * R2 * R3 * R4) - m2 * (c123 * R1 * R3 * R4 + c123 * R3 * R3 * R4)
           + m * (c123 * R3 * R3 * R4 + c123 * R1 * R3 * R3 + c123 * R1 * R3 * R4)
           + t * c123 * R1 * R3 * R4 - t * m * c123 * R1 * R3 * R4 + t * l * c123 * R1 * R2 * R4;
    p.a[0] = 1.0;
    p.a[1] = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    p.a[2] = m * (C1 * C3 * R1 * R3 + C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
           + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    p.a[3] = l * m * (c123 * R1 * R2 * R3 + c123 * R2 * R3 * R4) - m2 * (c123 * R1 * R3 * R4 + c123 * R3 * R3 * R4)
           + m * (c123 * R3 * R3 * R4 + c123 * R1 * R3 * R3 + c123 * R1 * R3 * R4)
           + l * c123 * R1 * R2 * R4 + c123 * R1 * R3 * R4;
    return p;
}

double toneStackAnalogDb(double f, double t, double m, double l) {
    const ToneStackPoly p = toneStackAnalog(t, m, l);
    const std::complex<double> s(0.0, 2.0 * kPi * f);
    const std::complex<double> num = p.b[1] * s + p.b[2] * s * s + p.b[3] * s * s * s;
    const std::complex<double> den = 1.0 + p.a[1] * s + p.a[2] * s * s + p.a[3] * s * s * s;
    return 20.0 * std::log10(std::abs(num / den) + 1e-30);
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateFixed() {
    recovery_ = 1.0 / dbToLin(toneStackAnalogDb(1000.0, 0.5, 0.5, bassPot(5)));   // the recovery stage gives back what the stack takes at noon
    const int ch = static_cast<int>(target_[Channel] + 0.5);
    for (auto& c : c_) {
        const double pres = (target_[Presence] - 5.0) * 1.2;
        c.presence.setup(Svf::Mode::HighShelf, 3200.0, fs_, 0.7, pres);
        c.tHp.setup(Svf::Mode::HighPass, 40.0, fs_, 0.70710678, 0);
        c.tLp.setup(Svf::Mode::LowPass, std::min(9000.0, 0.45 * fs_), fs_, 0.70710678, 0);
        (void)ch;
    }
}

void Processor::updateTone() {
    const double t = tre_.current() * 0.1, m = mid_.current() * 0.1, l = bassPot(bass_.current());
    const ToneStackPoly p = toneStackAnalog(t, m, l);
    const double c = 2.0 * fs_;
    auto ex = [&](const double q[4], double out[4]) {   // sum_k q_k c^k (1-z)^k (1+z)^(3-k)  ->  z^0..z^3
        static const double k0[4] = {1, 3, 3, 1}, k1[4] = {1, 1, -1, -1}, k2[4] = {1, -1, -1, 1}, k3[4] = {1, -3, 3, -1};
        for (int i = 0; i < 4; ++i) out[i] = q[0] * k0[i] + q[1] * c * k1[i] + q[2] * c * c * k2[i] + q[3] * c * c * c * k3[i];
    };
    double bz[4], az[4];
    ex(p.b, bz); ex(p.a, az);
    for (int i = 0; i < 4; ++i) { tb_[i] = bz[i] / az[0]; ta_[i] = az[i] / az[0]; }
}

double Processor::toneResponseDb(double f) const {
    const double w = 2.0 * kPi * f / fs_;
    std::complex<double> n = 0, d = 0;
    for (int i = 0; i < 4; ++i) { const std::complex<double> e = std::exp(std::complex<double>(0, -w * i)); n += tb_[i] * e; d += ta_[i] * e; }
    return 20.0 * std::log10(std::abs(n / d) + 1e-30);
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    for (auto& c : c_) c = Ch{};
    gain_.reset(fs_, 20.0, target_[Gain]); bass_.reset(fs_, 20.0, target_[Bass]); mid_.reset(fs_, 20.0, target_[Middle]);
    tre_.reset(fs_, 20.0, target_[Treble]); master_.reset(fs_, 20.0, target_[Master]);
    match_.reset(fs_, 1000.0, 1.0);
    accSq_ = accT_ = blockSq_ = 0; blockN_ = 0; matchDb_ = 0; matchDone_ = false; inGain_ = 1.0; ctl_ = 0;
    updateFixed(); updateTone();
    for (auto& c : c_) c.bright.setup(Svf::Mode::HighShelf, 2500.0, fs_, 0.7, 0.0);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case Gain: gain_.setTarget(v); break;
        case Bass: bass_.setTarget(v); break;
        case Middle: mid_.setTarget(v); break;
        case Treble: tre_.setTarget(v); break;
        case Master: master_.setTarget(v); break;
        case Presence: if (prepared_) updateFixed(); break;
        case VolumeMatch:
            accSq_ = accT_ = blockSq_ = 0; blockN_ = 0; matchDb_ = 0; matchDone_ = false;
            if (prepared_) match_.setTarget(1.0);
            break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    for (LinearSmoother* s : {&gain_, &bass_, &mid_, &tre_, &master_, &match_}) s->skip(1 << 30);
    updateFixed(); updateTone();
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const int chn = static_cast<int>(target_[Channel] + 0.5);
    const ChannelDesign& d = kCh[chn];
    const double fs4 = 4.0 * fs_;
    const double aHp1 = 1.0 - std::exp(-2.0 * kPi * d.hp1 / fs4), aHp2 = 1.0 - std::exp(-2.0 * kPi * d.hp2 / fs4), aLp = 1.0 - std::exp(-2.0 * kPi * 12000.0 / fs4);
    const double tb1 = std::tanh(d.b1), s1 = 1.0 / (1.0 - tb1 * tb1), tb2 = std::tanh(d.b2), s2 = 1.0 / (1.0 - tb2 * tb2);
    const double outLin = dbToLin(d.outDb);
    const bool bright = target_[Bright] > 0.5, matching = target_[VolumeMatch] > 0.5;
    const int blockLen = std::max(1, static_cast<int>(fs_ * 0.1));
    double a1 = 1.0, pg = 1.0, brightDb = 0.0;
    for (int i = 0; i < n; ++i) {
        if (ctl_ == 0) {
            ctl_ = 32;
            const double g = gain_.skip(32);
            a1 = dbToLin(d.gainLoDb + (d.gainHiDb - d.gainLoDb) * g * 0.1);
            const double mk = master_.skip(32);
            pg = dbToLin(-60.0 + 78.0 * std::sqrt(std::clamp(mk, 0.0, 10.0) * 0.1));
            brightDb = bright ? 9.0 * (1.0 - g * 0.1) : 0.0;
            bass_.skip(32); mid_.skip(32); tre_.skip(32);
            if (bass_.isSmoothing() || mid_.isSmoothing() || tre_.isSmoothing()) updateTone(); 
            inGain_ = match_.skip(32);
            for (int c = 0; c < nch; ++c) c_[static_cast<size_t>(c)].bright.setup(Svf::Mode::HighShelf, 2500.0, fs_, 0.7, brightDb);
        }
        --ctl_;
        if (matching && !matchDone_) {
            double e = 0; for (int c = 0; c < nch; ++c) e += double(ch[c][i]) * ch[c][i];
            blockSq_ += e / nch;
            if (++blockN_ >= blockLen) {
                const double ms = blockSq_ / blockN_;
                if (10.0 * std::log10(ms + 1e-20) > kPlayingGateDbfs) { accSq_ += ms; accT_ += 0.1; }
                blockSq_ = 0; blockN_ = 0;
                if (accT_ >= kMatchSeconds) {
                    const double rms = std::sqrt(accSq_ / (accT_ * 10.0));
                    matchDb_ = std::clamp(kReferenceDbfs - 20.0 * std::log10(rms + 1e-12), -kMatchLimitDb, kMatchLimitDb);
                    matchDone_ = true; match_.setTarget(dbToLin(matchDb_));
                }
            }
        }
        for (int c = 0; c < nch; ++c) {
            Ch& s = c_[static_cast<size_t>(c)];
            double x = ch[c][i] * inGain_;
            if (bright) x = s.bright.process(x);
            double up[4];
            s.pre.up(x, up);
            for (double& u : up) {
                double y = s1 * (std::tanh(a1 * u + d.b1) - tb1);
                s.dc1 += aHp1 * (y - s.dc1); y -= s.dc1;
                s.lp1 += aLp * (y - s.lp1); y = s.lp1;
                double z = s2 * (std::tanh(d.a2 * y + d.b2) - tb2);
                s.dc2 += aHp2 * (z - s.dc2); z -= s.dc2;
                u = z;
            }
            double v = s.pre.down(up);
            // tone stack (transposed direct form II), recovery gain, master
            const double t0 = tb_[0] * v + s.tz[0];
            s.tz[0] = tb_[1] * v - ta_[1] * t0 + s.tz[1];
            s.tz[1] = tb_[2] * v - ta_[2] * t0 + s.tz[2];
            s.tz[2] = tb_[3] * v - ta_[3] * t0;
            v = t0 * recovery_ * pg;
            // power stage with supply sag: the supply falls with the power drawn, the headroom with it
            const double h = std::max(0.4, 1.0 - d.sag * std::min(1.0, 2.0 * s.sag));
            double pu[4];
            s.pow.up(v, pu);
            for (double& u : pu) u = h * std::tanh(u / h);
            double y = s.pow.down(pu);
            const double ay = y * y;
            s.sag += (ay - s.sag) * (ay > s.sag ? 1.0 / (0.04 * fs_) : 1.0 / (0.2 * fs_));
            y = s.presence.process(y);
            y = s.tLp.process(s.tHp.process(y)) * outLin;
            if (std::abs(y) < 1e-30) y = 0.0;
            ch[c][i] = static_cast<float>(y);
        }
    }
}

}  // namespace sw::gt01
