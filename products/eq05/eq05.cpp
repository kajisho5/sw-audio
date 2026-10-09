#include "eq05/eq05.hpp"
#include "sw/base64.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sw::eq05 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"eq05.hf.gain",    "HF Gain",   -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.hf.freq",    "HF Freq",   1500, 16000, 8000, Curve::Log, 1, {}, "Hz"},
        {"eq05.hf.shape",   "HF Shape",  0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Shelf", "Bell"}},
        {"eq05.hmf.gain",   "HMF Gain",  -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.hmf.freq",   "HMF Freq",  600, 7000, 2000,  Curve::Log, 1, {}, "Hz"},
        {"eq05.hmf.q",      "HMF Q",     0.5, 3, 1,        Curve::Log, 1, {}, ""},
        {"eq05.lmf.gain",   "LMF Gain",  -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.lmf.freq",   "LMF Freq",  200, 2500, 600,   Curve::Log, 1, {}, "Hz"},
        {"eq05.lmf.q",      "LMF Q",     0.5, 3, 1,        Curve::Log, 1, {}, ""},
        {"eq05.lf.gain",    "LF Gain",   -15, 15, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.lf.freq",    "LF Freq",   30, 450, 100,     Curve::Log, 1, {}, "Hz"},
        {"eq05.lf.shape",   "LF Shape",  0, 1, 0,          Curve::Step, 1, {0, 1}, "", {"Shelf", "Bell"}},
        {"eq05.hpf",        "HPF",       0, 200, 0,        Curve::Step, 1, {0, 40, 80, 120, 200}, "Hz", {"Off", "40 Hz", "80 Hz", "120 Hz", "200 Hz"}},
        {"eq05.lpf",        "LPF",       0, 20000, 0,      Curve::Step, 1, {0, 8000, 12000, 16000, 20000}, "Hz", {"Off", "8 kHz", "12 kHz", "16 kHz", "20 kHz"}},
        {"eq05.drive",      "Drive",     0, 10, 2,         Curve::Lin, 1, {}, ""},
        {"eq05.drive.pos",  "Drive Pos", 0, 1, 1,          Curve::Step, 1, {0, 1}, "", {"Pre", "Post"}},
        {"eq05.out",        "Output",    -10, 10, 0,       Curve::Lin, 1, {}, "dB"},
        {"eq05.in",         "In",        0, 1, 1,          Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        oversampleSpec("eq05.os"),
            unitSpec("eq05.unit"),
    };
    return s;
}

namespace {
constexpr int kControlRate = 16;     // coefficient update interval while smoothing (samples)
constexpr double kRampMs = 20.0, kFadeMs = 10.0;
constexpr double kShelfBellQ = 0.7, kButterQ = 0.70710678118654752, kHpfStageQ = 1.0;
constexpr double kMatchSeconds = 10.0;           // Match listens to the input until it has heard this much playing (spec)
constexpr size_t kRefMaxBytes = 48u << 20;       // a reference of at most 12 M samples (the screen sends at most 40 s)
}

Processor::Processor() {
    for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def;
}

void Processor::prepare(double sampleRate, int /*maxBlock*/) {
    fs_ = sampleRate;
    fadeLength_ = std::max(1, static_cast<int>(std::lround(fs_ * kFadeMs * 0.001)));
    ch_ = {}; fade_ = {}; fadeRemaining_ = 0;
    auto r = [&](LinearSmoother& s, double ms) { s.reset(fs_, ms, 0.0); };
    for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                              &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive}) r(*s, kRampMs);
    for (LinearSmoother* s : {&ctl_.hfBell, &ctl_.lfBell, &ctl_.hpfOn, &ctl_.lpfOn}) r(*s, kFadeMs);
    for (int i = 0; i < kNumParams; ++i) setParam(i, target_[static_cast<size_t>(i)]);
    snapToTargets();
    input_.prepare(fs_); input_.setLimitSeconds(kMatchSeconds);
    listening_.store(false); fitPending_.store(false); resultReady_.store(false); progress_.store(0.0); nWrites_ = writeAt_ = 0;
}

// HPF/LPF cutoff smoothing happens in the log domain over the full audible range
static double hpfNorm(double hz) { return std::log(hz / 20.0) / std::log(1000.0); }
static double hpfHz(double n) { return 20.0 * std::pow(1000.0, n); }

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));  // clamp / snap to the legal grid
    target_[static_cast<size_t>(id)] = v;
    switch (id) {
        case HfGain:  ctl_.hfGain.setTarget(v); break;
        case HfFreq:  ctl_.hfFreqN.setTarget(sp.toNorm(v)); break;
        case HfShape: ctl_.hfBell.setTarget(v); break;
        case HmfGain: ctl_.hmfGain.setTarget(v); break;
        case HmfFreq: ctl_.hmfFreqN.setTarget(sp.toNorm(v)); break;
        case HmfQ:    ctl_.hmfQN.setTarget(sp.toNorm(v)); break;
        case LmfGain: ctl_.lmfGain.setTarget(v); break;
        case LmfFreq: ctl_.lmfFreqN.setTarget(sp.toNorm(v)); break;
        case LmfQ:    ctl_.lmfQN.setTarget(sp.toNorm(v)); break;
        case LfGain:  ctl_.lfGain.setTarget(v); break;
        case LfFreq:  ctl_.lfFreqN.setTarget(sp.toNorm(v)); break;
        case LfShape: ctl_.lfBell.setTarget(v); break;
        case Hpf:     hpfNow_.store(v); ctl_.hpfOn.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) ctl_.hpfFreqN.setTarget(hpfNorm(v)); break;
        case Lpf:     lpfNow_.store(v); ctl_.lpfOn.setTarget(v > 0 ? 1.0 : 0.0); if (v > 0) ctl_.lpfFreqN.setTarget(hpfNorm(v)); break;
        case Drive:   ctl_.drive.setTarget(v * 1.8); break;  // 0..10 -> 0..+18 dB
        case DrivePos: {
            const int pos = static_cast<int>(v);
            if (pos != drivePos_) { fade_ = ch_; fadePos_ = drivePos_; fadeRemaining_ = fadeLength_; drivePos_ = pos; }
            break;
        }
        case Oversample: for (Chain* c : {&ch_[0], &ch_[1], &fade_[0], &fade_[1]}) c->os.setFactor(static_cast<int>(v)); break;
        case Unit: unit_ = static_cast<int>(v); updateCoefficients(static_cast<int>(0.02 * fs_)); break;   // the coefficients move over 20 ms
        case Output: case In: break;  // handled by sw::Shell (common frame)
        default: break;
    }
}

void Processor::snapToTargets() {
    for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN, &ctl_.lmfQN,
                              &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive,
                              &ctl_.hfBell, &ctl_.lfBell, &ctl_.hpfOn, &ctl_.lpfOn})
        s->skip(1 << 30);
    fadeRemaining_ = 0;
    updateCoefficients();
}

bool Processor::anySmoothing() const {
    for (const LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                                    &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive})
        if (s->isSmoothing()) return true;
    return false;
}

void Processor::updateCoefficients(int n) {
    const auto& s = specs();
    const double hf = s[HfFreq].toValue(ctl_.hfFreqN.current()), hmf = s[HmfFreq].toValue(ctl_.hmfFreqN.current()),
                 lmf = s[LmfFreq].toValue(ctl_.lmfFreqN.current()), lf = s[LfFreq].toValue(ctl_.lfFreqN.current());
    const double hmfQ = s[HmfQ].toValue(ctl_.hmfQN.current()), lmfQ = s[LmfQ].toValue(ctl_.lmfQN.current());
    const double hpf = hpfHz(ctl_.hpfFreqN.current()), lpf = hpfHz(ctl_.lpfFreqN.current());
    // n > 1: move coefficients linearly over the next n samples (no zipper); otherwise jump
    int k = 0;
    for (Chain* c : {&ch_[0], &ch_[1], &fade_[0], &fade_[1]}) {   // the chains and their copies for the Drive Pos crossfade: channel k & 1 (Unit B / C: each channel's parts have their own tolerance)
        const int ch = k++ & 1;
        auto fm = [&](int slot, double f) { return std::min(f * Unit::freqMul(unit_, ch, slot), 0.45 * fs_); };
        c->hfShelf.setupRamp(Svf::Mode::HighShelf, fm(0, hf), fs_, kButterQ, ctl_.hfGain.current(), n);
        c->hfBell.setupRamp(Svf::Mode::Bell, fm(0, hf), fs_, kShelfBellQ, ctl_.hfGain.current(), n);
        c->hmf.setupRamp(Svf::Mode::Bell, fm(1, hmf), fs_, hmfQ, ctl_.hmfGain.current(), n);
        c->lmf.setupRamp(Svf::Mode::Bell, fm(2, lmf), fs_, lmfQ, ctl_.lmfGain.current(), n);
        c->lfShelf.setupRamp(Svf::Mode::LowShelf, fm(3, lf), fs_, kButterQ, ctl_.lfGain.current(), n);
        c->lfBell.setupRamp(Svf::Mode::Bell, fm(3, lf), fs_, kShelfBellQ, ctl_.lfGain.current(), n);
        c->hpf1.setupRamp(OnePole::Mode::HighPass, fm(4, hpf), fs_, n);
        c->hpf2.setupRamp(Svf::Mode::HighPass, fm(4, hpf), fs_, kHpfStageQ, 0, n);
        c->lpf.setupRamp(Svf::Mode::LowPass, fm(5, lpf), fs_, kButterQ, 0, n);
        c->onset = std::pow(10.0, Unit::satDb(unit_, ch, 0) / 20.0);
    }
    sat_.setHeadroom(2.0);  // +6 dBFS, same as every analog output stage (README)
    sat_.setDriveDb(ctl_.drive.current());
}

// all alternatives run continuously so that switching only crossfades valid outputs
double Processor::eq(Chain& c, double x) const {
    const double h = c.hpf2.process(c.hpf1.process(x));
    x += hpfOn_ * (h - x);
    const double l = c.lpf.process(x);
    x += lpfOn_ * (l - x);
    const double ls = c.lfShelf.process(x), lb = c.lfBell.process(x);
    x = ls + lfBell_ * (lb - ls);
    x = c.lmf.process(x);
    x = c.hmf.process(x);
    const double hs = c.hfShelf.process(x), hb = c.hfBell.process(x);
    return hs + hfBell_ * (hb - hs);
}

double Processor::drive(Chain& c, double x) const {
    return c.os.process(x, [&](double u) { return sat_.process(u, c.onset); });
}

double Processor::runChain(Chain& c, double x, int pos) const {
    return pos == 0 ? eq(c, drive(c, x)) : drive(c, eq(c, x));
}

void Processor::process(float** chans, int numCh, int n) {
    numCh = std::min(numCh, 2);
    if (resultReady_.load()) applyMatch();
    if (listening_.load()) listen(chans, numCh, n);
    for (int start = 0; start < n; start += kControlRate) {
        const int len = std::min(kControlRate, n - start);
        if (anySmoothing()) {
            for (LinearSmoother* s : {&ctl_.hfGain, &ctl_.hfFreqN, &ctl_.hmfGain, &ctl_.hmfFreqN, &ctl_.hmfQN, &ctl_.lmfGain, &ctl_.lmfFreqN,
                                      &ctl_.lmfQN, &ctl_.lfGain, &ctl_.lfFreqN, &ctl_.hpfFreqN, &ctl_.lpfFreqN, &ctl_.drive})
                s->skip(len);
            updateCoefficients(len);
        }
        for (int i = start; i < start + len; ++i) {
            hfBell_ = ctl_.hfBell.next(); lfBell_ = ctl_.lfBell.next();
            hpfOn_ = ctl_.hpfOn.next();   lpfOn_ = ctl_.lpfOn.next();
            double xf = 0;
            if (fadeRemaining_ > 0) xf = static_cast<double>(fadeRemaining_--) / fadeLength_;
            for (int c = 0; c < numCh; ++c) {
                const double x = chans[c][i];
                double y = runChain(ch_[static_cast<size_t>(c)], x, drivePos_);
                if (xf > 0) y += xf * (runChain(fade_[static_cast<size_t>(c)], x, fadePos_) - y);
                if (std::abs(y) < 1e-30) y = 0.0;  // below -600 dBFS: flush, never emit subnormals
                chans[c][i] = static_cast<float>(y);
            }
        }
    }
}

// ---- Match -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The model of what the knobs do to the tone curve, in dB at the centre of each 1/6-octave band: the analog prototypes at the warped frequency (what the TPT filters are: see sw/svf.hpp), HF / LF as a shelf or a bell
// as the Shape knob says, the high-pass and low-pass as they are set (they are not fitted: they are the user's, and the fit makes up for them). The level of the whole is not matched (Output is the user's): the
// difference curve has its weighted mean taken out. Weights: 1 from 30 Hz to 16 kHz, falling to 0 over a sixth of an octave outside (the ends of a recording are the codec's and the room's, not the tone's).
namespace {
inline double sq_(double x) { return x * x; }
constexpr int kBands = BandSpectrum::kBands, kN = 10, kRes = kBands + 4;
constexpr double kPiM = 3.14159265358979323846;

struct MatchModel {
    static constexpr int kP = BandSpectrum::kMaxPoints;
    double xs[kBands][kP], pw[kBands][kP]; int np[kBands];   // tan(pi f / fs) of the sample points of each band and their weights (BandSpectrum::samplePoints: what the measurement reads of a band)
    double w[kBands], d[kBands];   // weights; the target (reference - input - fixed filters) in dB
    double fs = 48000.0, w2sum = 1.0, rho = 0.01;
    int hfBell = 0, lfBell = 0;
    double lo[kN], hi[kN];

    static double t(double fc, double fs) { return std::tan(kPiM * std::min(fc, 0.49 * fs) / fs); }
    // |H|^2 of the prototypes at u = j w (w the frequency over the corner)
    static double bell2(double w, double A, double q) { const double a = (1 - w * w) * (1 - w * w); return (a + sq(w * A / q)) / (a + sq(w / (A * q))); }
    static double lowShelf2(double w, double A, double q) { const double c = w * w * A / (q * q); return A * A * (sq(A - w * w) + c) / (sq(1 - A * w * w) + c); }
    static double highShelf2(double w, double A, double q) { const double c = w * w * A / (q * q); return A * A * (sq(1 - A * w * w) + c) / (sq(A - w * w) + c); }
    static double sq(double x) { return x * x; }

    // the model (dB) of band b for the parameters th = [hfG, ln hfF, hmfG, ln hmfF, ln hmfQ, lmfG, ln lmfF, ln lmfQ, lfG, ln lfF]
    void model(const double* th, double* m) const {
        const double thf = t(std::exp(th[1]), fs), thm = t(std::exp(th[3]), fs), tlm = t(std::exp(th[6]), fs), tlf = t(std::exp(th[9]), fs);
        const double Ah = std::pow(10.0, th[0] / 40.0), Am = std::pow(10.0, th[2] / 40.0), Al = std::pow(10.0, th[5] / 40.0), Af = std::pow(10.0, th[8] / 40.0);
        const double qm = std::exp(th[4]), ql = std::exp(th[7]);
        for (int b = 0; b < kBands; ++b) {
            if (w[b] <= 0.0) { m[b] = 0.0; continue; }
            double sum = 0;
            for (int i = 0; i < np[b]; ++i) {
                const double x = xs[b][i];
                double p = hfBell ? bell2(x / thf, Ah, kShelfBellQ) : highShelf2(x / thf, Ah, kButterQ);
                p *= bell2(x / thm, Am, qm) * bell2(x / tlm, Al, ql);
                p *= lfBell ? bell2(x / tlf, Af, kShelfBellQ) : lowShelf2(x / tlf, Af, kButterQ);
                sum += pw[b][i] * p;
            }
            m[b] = 10.0 * std::log10(std::max(sum, 1e-30));
        }
    }
    // residuals (the mean square of the weighted difference goes to *ms) and the regularisation (a pull toward flat, small)
    double residuals(const double* th, double* r, double* ms) const {
        double m[kBands]; model(th, m);
        double sd = 0; for (int b = 0; b < kBands; ++b) sd += w[b] * w[b] * (d[b] - m[b]);
        const double c = sd / w2sum, k = 1.0 / std::sqrt(w2sum);
        double e2 = 0; for (int b = 0; b < kBands; ++b) { r[b] = w[b] * (d[b] - m[b] - c) * k; e2 += r[b] * r[b]; }
        const int gi[4] = {0, 2, 5, 8}; double reg = 0;
        for (int i = 0; i < 4; ++i) { r[kBands + i] = rho * th[gi[i]]; reg += r[kBands + i] * r[kBands + i]; }
        if (ms) *ms = e2;
        return e2 + reg;
    }
};

// 10 x 10 normal equations, Gaussian elimination with partial pivoting; false when singular
bool solve(double A[kN][kN], double* b) {
    for (int i = 0; i < kN; ++i) {
        int piv = i; for (int j = i + 1; j < kN; ++j) if (std::abs(A[j][i]) > std::abs(A[piv][i])) piv = j;
        if (std::abs(A[piv][i]) < 1e-14) return false;
        if (piv != i) { for (int k = 0; k < kN; ++k) std::swap(A[i][k], A[piv][k]); std::swap(b[i], b[piv]); }
        for (int j = i + 1; j < kN; ++j) { const double f = A[j][i] / A[i][i]; for (int k = i; k < kN; ++k) A[j][k] -= f * A[i][k]; b[j] -= f * b[i]; }
    }
    for (int i = kN - 1; i >= 0; --i) { double s = b[i]; for (int k = i + 1; k < kN; ++k) s -= A[i][k] * b[k]; b[i] = s / A[i][i]; }
    return true;
}

// Levenberg-Marquardt within the bounds (steps are clamped to them); th is the start and the result; returns the cost
double levenbergMarquardt(const MatchModel& md, double* th) {
    double r[kRes], rp[kRes], J[kRes][kN], A[kN][kN], g[kN], trial[kN];
    double cost = md.residuals(th, r, nullptr), lambda = 0.01;
    for (int it = 0; it < 80; ++it) {
        for (int j = 0; j < kN; ++j) {
            const double h = 1e-4, save = th[j]; const bool back = save + h > md.hi[j];
            th[j] = back ? save - h : save + h; md.residuals(th, rp, nullptr); th[j] = save;
            for (int i = 0; i < kRes; ++i) J[i][j] = (back ? r[i] - rp[i] : rp[i] - r[i]) / h;
        }
        for (int a = 0; a < kN; ++a) { g[a] = 0; for (int i = 0; i < kRes; ++i) g[a] += J[i][a] * r[i]; for (int b = 0; b < kN; ++b) { double s = 0; for (int i = 0; i < kRes; ++i) s += J[i][a] * J[i][b]; A[a][b] = s; } }
        bool improved = false; double newCost = cost;
        for (int tries = 0; tries < 12 && !improved; ++tries) {
            double M[kN][kN], rhs[kN];
            for (int a = 0; a < kN; ++a) { for (int b = 0; b < kN; ++b) M[a][b] = A[a][b]; M[a][a] += lambda * (A[a][a] + 1e-6); rhs[a] = -g[a]; }
            if (!solve(M, rhs)) { lambda *= 10; continue; }
            for (int a = 0; a < kN; ++a) trial[a] = std::clamp(th[a] + rhs[a], md.lo[a], md.hi[a]);
            newCost = md.residuals(trial, rp, nullptr);
            if (newCost < cost) { improved = true; lambda = std::max(lambda * 0.3, 1e-7); } else lambda *= 4;
        }
        if (!improved) break;
        const double gain = cost - newCost;
        for (int a = 0; a < kN; ++a) th[a] = trial[a];
        cost = md.residuals(th, r, nullptr);
        if (gain < 1e-9 * (1.0 + cost)) break;
    }
    return cost;
}
}  // namespace

void Processor::refBegin(double rate) {
    refBytes_.clear(); refBytes_.shrink_to_fit();
    refOpen_ = rate >= 8000.0 && rate <= 384000.0; refBroken_ = false; refRate_ = rate;
}
bool Processor::refAppendBase64(const char* text) {
    if (!refOpen_ || refBroken_) return false;
    std::vector<uint8_t> bytes; bytes.reserve(text ? std::strlen(text) / 4 * 3 : 0);
    if (!base64Decode(text, bytes) || refBytes_.size() + bytes.size() > kRefMaxBytes) { refBroken_ = true; refBytes_.clear(); refBytes_.shrink_to_fit(); return false; }
    refBytes_.insert(refBytes_.end(), bytes.begin(), bytes.end());
    return true;
}
// the reference's long-term spectrum (its own sample rate: the bands are in Hz); it needs 3 s of playing, or it is not a reference
bool Processor::refCommit() {
    if (!refOpen_) { refFailed_.store(refFailed_.load() + 1.0); return false; }
    refOpen_ = false;
    bool ok = false;
    if (!refBroken_ && refBytes_.size() >= 4 * 1024 && refBytes_.size() % 4 == 0) {
        std::vector<float> f(refBytes_.size() / 4); std::memcpy(f.data(), refBytes_.data(), refBytes_.size());
        BandSpectrum s; s.prepare(refRate_); s.start();
        for (size_t off = 0; off < f.size(); off += 4096) s.process(f.data() + off, static_cast<int>(std::min<size_t>(4096, f.size() - off)));
        std::array<double, kBands> lv{};
        if (s.playingSeconds() >= 3.0 && s.levels(lv.data())) { refBands_ = lv; ok = true; }
    }
    refBytes_.clear(); refBytes_.shrink_to_fit();
    if (ok) refReady_.store(true);   // (a reference that fails to load leaves the previous one)
    if (ok) refDone_.store(refDone_.load() + 1.0); else refFailed_.store(refFailed_.load() + 1.0);
    return ok;
}
void Processor::refAbort() { refOpen_ = false; refBytes_.clear(); refBytes_.shrink_to_fit(); }
void Processor::refClear() { refOpen_ = false; refBytes_.clear(); refBytes_.shrink_to_fit(); refReady_.store(false); }

// the audio thread: a press starts listening to the input (10 s of playing), another press cancels
void Processor::match() {
    if (listening_.load()) { listening_.store(false); progress_.store(0.0); return; }
    if (!hasReference()) return;
    input_.start(); progress_.store(0.0); fitPending_.store(false); resultReady_.store(false); nWrites_ = writeAt_ = 0;
    listening_.store(true);
}
void Processor::listen(float** ch, int numCh, int n) {
    float mono[256];
    for (int off = 0; off < n && !input_.full(); off += 256) {
        const int len = std::min(256, n - off);
        for (int i = 0; i < len; ++i) { float s = 0.0f; for (int c = 0; c < numCh; ++c) s += ch[c][off + i]; mono[i] = numCh > 0 ? s / static_cast<float>(numCh) : 0.0f; }
        input_.process(mono, len);
    }
    progress_.store(std::min(1.0, input_.playingSeconds() / kMatchSeconds));
    if (input_.full()) {
        double lv[kBands];
        if (input_.levels(lv)) { for (int b = 0; b < kBands; ++b) inBands_[static_cast<size_t>(b)] = lv[b]; fitPending_.store(true); }
        listening_.store(false);
    }
}

// the GUI thread: fit the knobs to (reference - input) with the high-pass and low-pass as they are set
void Processor::fit() {
    if (!fitPending_.load()) return;
    const auto& sp = specs();
    MatchModel md; md.fs = fs_;
    const double hpf = hpfNow_.load(), lpf = lpfNow_.load();
    for (int b = 0; b < kBands; ++b) {
        const double f = BandSpectrum::centerHz(b);
        double pf[BandSpectrum::kMaxPoints]; md.np[b] = input_.samplePoints(b, pf, md.pw[b]);
        for (int i = 0; i < md.np[b]; ++i) md.xs[b][i] = std::tan(kPiM * std::min(pf[i], 0.49 * fs_) / fs_);
        md.w[b] = std::clamp(std::min((f - 30.0 / 1.1225) / (30.0 - 30.0 / 1.1225), (16000.0 * 1.1225 - f) / (16000.0 * 1.1225 - 16000.0)), 0.0, 1.0);
        double fixedSum = 0;   // the fixed filters (18 dB/oct high-pass, 12 dB/oct low-pass), read the way the measurement reads a band
        for (int i = 0; i < md.np[b]; ++i) {
            double p = 1.0;
            if (hpf > 0) { const double w2 = sq_(md.xs[b][i] / MatchModel::t(hpf, fs_)); p *= w2 / (1.0 + w2) * (w2 * w2) / (sq_(1.0 - w2) + w2 / (kHpfStageQ * kHpfStageQ)); }
            if (lpf > 0) { const double w2 = sq_(md.xs[b][i] / MatchModel::t(lpf, fs_)); p /= sq_(1.0 - w2) + w2 / (kButterQ * kButterQ); }
            fixedSum += md.pw[b][i] * p;
        }
        const double fixedDb = 10.0 * std::log10(std::max(fixedSum, 1e-30));
        md.d[b] = refBands_[static_cast<size_t>(b)] - inBands_[static_cast<size_t>(b)] - fixedDb;
    }
    md.w2sum = 0; for (int b = 0; b < kBands; ++b) md.w2sum += md.w[b] * md.w[b];
    md.w2sum = std::max(md.w2sum, 1e-9);
    // bounds from the knobs' ranges
    auto lg = [&](int id, bool hiEnd) { return std::log(hiEnd ? sp[static_cast<size_t>(id)].max : sp[static_cast<size_t>(id)].min); };
    const int gid[kN] = {HfGain, HfFreq, HmfGain, HmfFreq, HmfQ, LmfGain, LmfFreq, LmfQ, LfGain, LfFreq};
    const bool isLog[kN] = {false, true, false, true, true, false, true, true, false, true};
    for (int j = 0; j < kN; ++j) { md.lo[j] = isLog[j] ? lg(gid[j], false) : sp[static_cast<size_t>(gid[j])].min; md.hi[j] = isLog[j] ? lg(gid[j], true) : sp[static_cast<size_t>(gid[j])].max; }
    // the start points: the corner frequencies spread in four ways across their ranges, and each of the shapes
    const double frac[4][4] = {{0.5, 0.5, 0.5, 0.5}, {0.2, 0.2, 0.2, 0.2}, {0.8, 0.8, 0.8, 0.8}, {0.85, 0.85, 0.15, 0.15}};   // HF, HMF, LMF, LF
    double best[kN] = {}, bestCost = 1e30; int bestHf = 0, bestLf = 0;
    for (int hb = 0; hb < 2; ++hb) for (int lb = 0; lb < 2; ++lb) for (int s = 0; s < 4; ++s) {
        md.hfBell = hb; md.lfBell = lb;
        double th[kN] = {};
        const int fid[4] = {1, 3, 6, 9};
        for (int i = 0; i < 4; ++i) th[fid[i]] = md.lo[fid[i]] + frac[s][i] * (md.hi[fid[i]] - md.lo[fid[i]]);
        th[4] = std::log(1.0); th[7] = std::log(1.0);
        double c = levenbergMarquardt(md, th);
        c += 1e-3 * (hb + lb);   // (shelves are the plainer reading when both fit as well)
        if (c < bestCost) { bestCost = c; bestHf = hb; bestLf = lb; for (int j = 0; j < kN; ++j) best[j] = th[j]; }
    }
    md.hfBell = bestHf; md.lfBell = bestLf;
    double r[kRes], ms0 = 0, ms1 = 0, flat[kN] = {};
    for (int j = 0; j < kN; ++j) { flat[j] = 0.0; }
    flat[1] = std::log(sp[HfFreq].def); flat[3] = std::log(sp[HmfFreq].def); flat[4] = 0; flat[6] = std::log(sp[LmfFreq].def); flat[7] = 0; flat[9] = std::log(sp[LfFreq].def);
    md.residuals(flat, r, &ms0); md.residuals(best, r, &ms1);
    result_ = {{{HfGain, best[0]}, {HfFreq, std::exp(best[1])}, {HfShape, static_cast<double>(bestHf)}, {HmfGain, best[2]}, {HmfFreq, std::exp(best[3])}, {HmfQ, std::exp(best[4])},
                {LmfGain, best[5]}, {LmfFreq, std::exp(best[6])}, {LmfQ, std::exp(best[7])}, {LfGain, best[8]}, {LfFreq, std::exp(best[9])}, {LfShape, static_cast<double>(bestLf)}}};
    before_.store(std::sqrt(ms0)); after_.store(std::sqrt(ms1));
    fitPending_.store(false); resultReady_.store(true);
}

// the audio thread: the fitted values go into the EQ and are handed to the host
void Processor::applyMatch() {
    resultReady_.store(false);
    for (size_t i = 0; i < result_.size(); ++i) { setParam(result_[i].first, result_[i].second); writes_[i] = {result_[i].first, target_[static_cast<size_t>(result_[i].first)]}; }
    nWrites_ = kMatchParams; writeAt_ = 0;
    applied_.store(applied_.load() + 1.0);
}

int Processor::takeParamWrite(int& id, double& plain) {
    if (writeAt_ >= nWrites_) { nWrites_ = writeAt_ = 0; return 0; }
    id = writes_[static_cast<size_t>(writeAt_)].first; plain = writes_[static_cast<size_t>(writeAt_)].second; ++writeAt_;
    return 7;
}

}  // namespace sw::eq05
