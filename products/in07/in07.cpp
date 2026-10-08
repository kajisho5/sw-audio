#include "in07/in07.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace sw::in07 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"in07.osc.wave",   "Wave",        0, 3, Saw,     Curve::Step, 1, {0, 1, 2, 3}, "", {"Sine", "Triangle", "Saw", "Square"}},
        {"in07.osc.pw",     "Pulse width", 5, 95, 50,     Curve::Lin, 1, {}, "%"},
        {"in07.osc.octave", "Octave",      -2, 2, 0,      Curve::Step, 1, {-2, -1, 0, 1, 2}, "", {"-2", "-1", "0", "+1", "+2"}},
        {"in07.osc.unison", "Unison",      1, 8, 1,       Curve::Step, 1, {1, 2, 3, 4, 5, 6, 7, 8}, "v", {"1", "2", "3", "4", "5", "6", "7", "8"}},
        {"in07.osc.detune", "Detune",      0, 100, 22,    Curve::Lin, 1, {}, "%"},
        {"in07.osc.spread", "Spread",      0, 100, 80,    Curve::Lin, 1, {}, "%"},
        {"in07.flt.type",   "Filter",      0, 3, LP24,    Curve::Step, 1, {0, 1, 2, 3}, "", {"LP 12", "LP 24", "BP 12", "HP 12"}},
        {"in07.flt.cutoff", "Cutoff",      20, 20000, 2400, Curve::Log, 1, {}, "Hz"},
        {"in07.flt.res",    "Resonance",   0, 100, 30,    Curve::Lin, 1, {}, "%"},
        {"in07.flt.drive",  "Drive",       0, 100, 18,    Curve::Lin, 1, {}, "%"},
        {"in07.flt.env",    "Env",         -100, 100, 40, Curve::Lin, 1, {}, "%"},
        {"in07.flt.key",    "Key track",   0, 100, 50,    Curve::Lin, 1, {}, "%"},
        {"in07.amp.a",      "Attack",      0.5, 10000, 5,   Curve::Log, 1, {}, "ms"},
        {"in07.amp.d",      "Decay",       1, 10000, 320,   Curve::Log, 1, {}, "ms"},
        {"in07.amp.s",      "Sustain",     0, 100, 70,      Curve::Lin, 1, {}, "%"},
        {"in07.amp.r",      "Release",     1, 20000, 420,   Curve::Log, 1, {}, "ms"},
        {"in07.fenv.a",     "Filter attack",  0.5, 10000, 2, Curve::Log, 1, {}, "ms"},
        {"in07.fenv.d",     "Filter decay",   1, 10000, 600, Curve::Log, 1, {}, "ms"},
        {"in07.fenv.s",     "Filter sustain", 0, 100, 20,    Curve::Lin, 1, {}, "%"},
        {"in07.fenv.r",     "Filter release", 1, 20000, 500, Curve::Log, 1, {}, "ms"},
        {"in07.vel",        "Vel sens",    0, 100, 50,    Curve::Lin, 1, {}, "%"},
        {"in07.glide",      "Glide",       0, 2000, 0,    Curve::Skew, 3, {}, "ms", {}, "Off"},
        {"in07.level",      "Level",       -40, 6, -6,    Curve::Lin, 1, {}, "dB"},
    };
    return s;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSqrt2 = 1.41421356237309504880;
constexpr double kQ24a = 0.5412, kQ24b = 1.3066, kQ12 = 0.7071, kQMax = 25.0, kEnvOctaves = 5.0, kDetuneCents = 50.0;

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
    if (on) { stage_ = Attack; count_ = 0; }
    else if (stage_ != Idle) { stage_ = Release; count_ = 0; }
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
            if (++count_ >= dN_) { level_ = s_; stage_ = Sustain; }
            break;
        case Sustain:
            if (level_ != s_) { level_ += (s_ - level_) * 0.004; if (std::abs(level_ - s_) < 1e-6) level_ = s_; }   // a moved Sustain knob: about 5 ms
            break;
        case Release:
            level_ *= cr_;
            if (++count_ >= rN_) { level_ = 0.0; stage_ = Idle; }
            break;
    }
    return level_;
}

// ---- voice
void Voice::prepare(double fs, const std::array<double, kNumParams>* params) {
    fs_ = fs; params_ = params;
    amp_.prepare(fs); fenv_.prepare(fs); amp_.reset(); fenv_.reset();
    for (auto& ch : svf_) for (auto& f : ch) f.reset();
    for (auto& o : os_) o.reset();
    rng_ = 0x5EED1234u; ctl_ = 0; first_ = true;
}

void Voice::noteOn(int note, double velocity, bool glide) {
    const bool wasActive = amp_.active();
    note_ = note;
    const double s = p(VelSens) / 100.0, v = std::clamp(velocity, 0.0, 1.0);
    velGain_ = (1.0 - s) + s * v * v;
    if (glide && wasActive && p(Glide) > 0.0) {
        glideFrom_ = pitch_;
        glideN_ = glideLeft_ = std::max(1, static_cast<int>(std::lround(p(Glide) * 1e-3 * fs_)));
    } else {
        glideLeft_ = 0;
    }
    if (!wasActive) {   // a fresh start: the voice was silent
        unison_ = std::clamp(static_cast<int>(std::lround(p(Unison))), 1, kMaxUnison);
        for (int i = 0; i < unison_; ++i) {
            double ph = 0.0;
            if (unison_ > 1) { rng_ = rng_ * 1664525u + 1013904223u; ph = (rng_ >> 8) * (1.0 / 16777216.0); }
            osc_[static_cast<size_t>(i)].setPhase(ph);
        }
        oversample_ = p(Drive) > 0.0;
        for (auto& ch : svf_) for (auto& f : ch) f.reset();
        for (auto& o : os_) o.reset();
        first_ = true;
    }
    ctl_ = 0;   // the next sample starts with a control update
    amp_.gate(true); fenv_.gate(true);
}

void Voice::noteOff() { amp_.gate(false); fenv_.gate(false); }

double Voice::frequency() const { return 440.0 * std::exp2((pitch_ - 69.0) / 12.0); }

void Voice::control() {
    // pitch (glide: linear in semitones over a constant time)
    const double target = note_ + 12.0 * p(Octave);
    if (glideLeft_ > 0) {
        const double prog = 1.0 - static_cast<double>(glideLeft_) / glideN_;
        pitch_ = glideFrom_ + (target - glideFrom_) * prog;
        glideLeft_ = std::max(0, glideLeft_ - kCtl);
    } else {
        pitch_ = target;
    }
    const double f = frequency();
    const double det = p(Detune) / 100.0 * kDetuneCents, spread = p(Spread) / 100.0, norm = 1.0 / std::sqrt(static_cast<double>(unison_));
    const int wave = static_cast<int>(std::lround(p(Wave)));
    const double pw = p(PulseWidth) / 100.0;
    bool stereo = false;
    for (int i = 0; i < unison_; ++i) {
        const size_t k = static_cast<size_t>(i);
        const double u = unison_ > 1 ? 2.0 * i / (unison_ - 1) - 1.0 : 0.0;   // -1 .. +1
        osc_[k].setWave(wave); osc_[k].setPulseWidth(pw);
        osc_[k].setIncrement(std::min(0.45, f * std::exp2(det * u / 1200.0) / fs_));
        const double pan = u * spread;
        if (std::abs(pan) < 1e-12) { gl_[k] = gr_[k] = norm; }
        else { const double th = (pan + 1.0) * kPi / 4.0; gl_[k] = norm * kSqrt2 * std::cos(th); gr_[k] = norm * kSqrt2 * std::sin(th); stereo = true; }
    }
    if (stereo && !stereo_) { svf_[1] = svf_[0]; os_[1] = os_[0]; }   // the right chain takes over the left chain's state: no click
    stereo_ = stereo;

    amp_.setTimes(p(AmpA) * 1e-3, p(AmpD) * 1e-3, p(AmpS) / 100.0, p(AmpR) * 1e-3);
    fenv_.setTimes(p(FenvA) * 1e-3, p(FenvD) * 1e-3, p(FenvS) / 100.0, p(FenvR) * 1e-3);

    // filter
    const double oct = kEnvOctaves * p(FilterEnv) / 100.0 * fenv_.level() + p(KeyTrack) / 100.0 * (pitch_ - 60.0) / 12.0;
    cutoff_ = std::clamp(p(Cutoff) * std::exp2(oct), 20.0, 0.45 * fs_);
    const int type = static_cast<int>(std::lround(p(FilterType)));
    if (type != type_) { for (auto& ch : svf_) ch[1].reset(); type_ = type; }
    const double r = std::clamp(p(Resonance) / 100.0, 0.0, 1.0);
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
    drive_ = std::clamp(p(Drive) / 100.0, 0.0, 1.0);
}

void Voice::render(float* l, float* r, int n) {
    if (!amp_.active()) return;   // sleep
    int off = 0;
    while (off < n) {
        if (ctl_ == 0) { control(); ctl_ = kCtl; }
        const int m = std::min(n - off, ctl_);
        const double d = drive_, g = 1.0 + 9.0 * d;
        const bool lp24 = type_ == LP24;
        for (int i = 0; i < m; ++i) {
            double xl = 0.0, xr = 0.0;
            for (int u = 0; u < unison_; ++u) {
                const size_t k = static_cast<size_t>(u);
                const double s = osc_[k].next();
                xl += gl_[k] * s; xr += gr_[k] * s;
            }
            const int chans = stereo_ ? 2 : 1;
            double y[2] = {xl, xr};
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
            const double a = amp_.next() * velGain_;
            l[off + i] += static_cast<float>(y[0] * a);
            r[off + i] += static_cast<float>(y[1] * a);
        }
        ctl_ -= m; off += m;
        if (!amp_.active()) {   // the release ended inside this block: the rest stays silent
            fenv_.reset();
            for (auto& ch : svf_) for (auto& f : ch) f.reset();
            for (auto& o : os_) o.reset();
            break;
        }
    }
}

// ---- monophonic shell
Processor::Processor() {
    for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def;
    held_.reserve(128);
}

void Processor::prepare(double sampleRate, int maxBlock) {
    (void)minBlep();   // build the table here, not on the audio thread
    fs_ = sampleRate;
    const size_t m = static_cast<size_t>(std::max(1, maxBlock));
    l_.assign(m, 0.0f); r_.assign(m, 0.0f);
    held_.clear();
    level_.reset(fs_, 20.0, dbToGain(target_[Level]));
    voice_.prepare(fs_, &target_);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    if (id < 0 || id >= kNumParams) return;
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (id == Level) level_.setTarget(dbToGain(v));
}

void Processor::noteOn(int note, double velocity) {
    if (!prepared_) return;
    held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
    if (held_.size() < held_.capacity()) held_.push_back(note);
    lastVel_ = velocity;
    voice_.noteOn(note, velocity, voice_.active());
}

void Processor::noteOff(int note) {
    if (!prepared_) return;
    const bool current = !held_.empty() && held_.back() == note;
    held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
    if (!current) return;
    if (held_.empty()) voice_.noteOff();
    else voice_.noteOn(held_.back(), lastVel_, true);   // back to the note still held (legato)
}

void Processor::allNotesOff() { held_.clear(); voice_.noteOff(); }

void Processor::process(float** ch, int numCh, int n) {
    for (int c = 0; c < numCh; ++c) std::fill(ch[c], ch[c] + n, 0.0f);
    if (!prepared_ || n <= 0) return;
    const int cap = static_cast<int>(l_.size());
    for (int off = 0; off < n; off += cap) {
        const int m = std::min(cap, n - off);
        std::fill(l_.begin(), l_.begin() + m, 0.0f);
        std::fill(r_.begin(), r_.begin() + m, 0.0f);
        voice_.render(l_.data(), r_.data(), m);
        for (int i = 0; i < m; ++i) {
            const double g = level_.next();
            double yl = l_[static_cast<size_t>(i)] * g, yr = r_[static_cast<size_t>(i)] * g;
            if (std::abs(yl) < 1e-30) yl = 0.0;
            if (std::abs(yr) < 1e-30) yr = 0.0;
            if (numCh == 1) { ch[0][off + i] = static_cast<float>(0.5 * (yl + yr)); continue; }
            ch[0][off + i] = static_cast<float>(yl);
            if (numCh > 1) ch[1][off + i] = static_cast<float>(yr);
        }
    }
}

}  // namespace sw::in07
