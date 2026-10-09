#include "eq02/eq02.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>

namespace sw::eq02 {

namespace {
const char* kIds[kPerBand] = {"on", "type", "freq", "gain", "q", "slope", "place", "dynrange", "dynthresh"};
constexpr double kDynFullDb = 12.0;  // design value: the full range is reached 12 dB above the threshold
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        static std::vector<std::string> ids;
        ids.reserve(kBands * kPerBand);
        static const double defFreq[5] = {100, 400, 1600, 6300, 12000};
        std::vector<ParamSpec> v;
        for (int b = 0; b < kBands; ++b) {
            for (int f = 0; f < kPerBand; ++f) ids.push_back("eq02.b" + std::to_string(b + 1) + "." + kIds[f]);
            const size_t i = static_cast<size_t>(b * kPerBand);
            v.push_back({ids[i + 0].c_str(), "On", 0, 1, b < 5 ? 1.0 : 0.0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            v.push_back({ids[i + 1].c_str(), "Type", 0, 5, 0, Curve::Step, 1, {0, 1, 2, 3, 4, 5}, "", {"Bell", "Lo shelf", "Hi shelf", "Lo cut", "Hi cut", "Notch"}});
            v.push_back({ids[i + 2].c_str(), "Freq", 10, 30000, b < 5 ? defFreq[b] : 1000, Curve::Log, 1, {}, "Hz"});
            v.push_back({ids[i + 3].c_str(), "Gain", -30, 30, 0, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[i + 4].c_str(), "Q", 0.1, 40, 1, Curve::Log, 1, {}, ""});
            v.push_back({ids[i + 5].c_str(), "Slope", 6, 96, 24, Curve::Step, 1, {6, 12, 18, 24, 36, 48, 72, 96}, "dB/oct",
                         {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct"}});
            v.push_back({ids[i + 6].c_str(), "Place", 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"Stereo", "Mid", "Side"}});
            v.push_back({ids[i + 7].c_str(), "Dyn Range", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[i + 8].c_str(), "Dyn Thresh", -60, 0, -30, Curve::Lin, 1, {}, "dB"});
        }
        v.push_back({"eq02.phase", "Phase", 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"Zero latency", "Natural", "Linear"}});
        v.push_back({"eq02.ms", "Mid/side", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"eq02.out", "Output", -24, 24, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"eq02.length", "Length", 2048, 8192, 2048, Curve::Step, 1, {2048, 4096, 8192}, "", {"2048 (spec)", "4096", "8192"}});  // Linear mode only
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::kernelLengthFor(double fs, double base) { int L = 2048; while (L < base * fs / 48000.0 * 0.92) L *= 2; return L; }

int Processor::latencySamples() const {
    switch (static_cast<int>(target_[PhaseMode])) {
        case Natural: return kNatDelay;
        case Linear: return kernelLengthFor(fs_, target_[Length]) / 2 + 128;
        default: return 0;
    }
}

bool Processor::onPath(int b, int p) const {
    if (target_[Ms] < 0.5) return true;
    const int place = static_cast<int>(t(b, Place));
    return place == 0 || (place == 1 && p == 0) || (place == 2 && p == 1);
}

bool Processor::dynamic(int b) const {
    const int ty = static_cast<int>(t(b, Type));
    return std::abs(t(b, DynRange)) > 1e-6 && ty != BandShape::LowCut && ty != BandShape::HighCut;
}

BandShape Processor::shape(int b, double off) const {
    static const BandShape::Type types[6] = {BandShape::Bell, BandShape::LowShelf, BandShape::HighShelf, BandShape::LowCut, BandShape::HighCut, BandShape::Notch};
    BandShape s;
    s.type = types[static_cast<int>(t(b, Type))];
    s.freq = std::min(t(b, Freq), 0.49 * fs_);
    s.gainDb = t(b, Gain) + off;
    s.q = t(b, Q);
    s.slope = t(b, Slope);
    return s;
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    mode_ = static_cast<int>(target_[PhaseMode]);
    L_ = kernelLengthFor(fs_, target_[Length]);
    fadeLen_ = static_cast<int>(std::lround(0.020 * fs_));
    res_.setup(fs_);
    for (auto& c : conv_) { c.prepare(L_, 128, 1); c.setFadeSamples(fadeLen_); }
    for (auto& b : band_) { for (auto& f : b.f) f.reset(); for (auto& d : b.det) d.reset(); b.env = {}; b.offset = {}; }
    for (int p = 0; p < 2; ++p) { nat_[static_cast<size_t>(p)].assign(kNatTaps, 0.0); nat_[static_cast<size_t>(p)][kNatDelay] = 1.0; natHist_[static_cast<size_t>(p)].assign(kNatTaps, 0.0); }
    natPos_ = 0;
    atk_ = std::exp(-1.0 / (0.005 * fs_)); rel_ = std::exp(-1.0 / (0.080 * fs_));  // detector: 5 ms / 80 ms (design)
    prepared_ = true;
    snapToTargets();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id < kBands * kPerBand || id == Ms) { dirty_ = true; kernelDirty_ = true; }
}

void Processor::configure(int ramp) {
    for (int b = 0; b < kBands; ++b) {
        Band& bd = band_[static_cast<size_t>(b)];
        const BandShape s = shape(b);
        // detector key: around the band (bell / notch), below (low shelf), above (high shelf)
        const Svf::Mode dm = s.type == BandShape::LowShelf ? Svf::Mode::LowPass : s.type == BandShape::HighShelf ? Svf::Mode::HighPass : Svf::Mode::BandPass;
        for (auto& d : bd.det) d.setup(dm, s.freq, fs_, s.type == BandShape::Bell || s.type == BandShape::Notch ? s.q : 0.7071, 0);
        for (int p = 0; p < 2; ++p) {
            BandShape fs = s;
            if (mode_ == Linear) { fs.gainDb = bd.offset[static_cast<size_t>(p)]; if (fs.type != BandShape::LowShelf && fs.type != BandShape::HighShelf) fs.type = BandShape::Bell; }
            else fs.gainDb += bd.offset[static_cast<size_t>(p)];
            if (!active(b) || !onPath(b, p)) { fs.type = BandShape::Bell; fs.gainDb = 0; }
            bd.f[static_cast<size_t>(p)].setup(fs, fs_, ramp, true);
        }
    }
    dirty_ = false;
}

void Processor::buildNatural() {
    // phase-only correction: arg(analog) - arg(digital) of the static bands on each path, as a short linear FIR
    const int N = 256;
    Fft fft(N);
    for (int p = 0; p < 2; ++p) {
        std::vector<std::complex<double>> C(static_cast<size_t>(N));
        for (int k = 0; k <= N / 2; ++k) {
            const double f = std::min(static_cast<double>(k) * fs_ / N, 0.499 * fs_);
            std::complex<double> ratio = 1.0;
            for (int b = 0; b < kBands; ++b) {
                if (!active(b) || !onPath(b, p)) continue;
                const BandShape s = shape(b);
                const double qd = s.type == BandShape::Bell ? fittedBellQ(s.q, s.freq, s.gainDb, fs_) : s.type == BandShape::Notch ? crampCorrectedQ(s.q, s.freq, fs_) : s.q;
                const double wd = std::tan(3.14159265358979323846 * f / fs_) / std::tan(3.14159265358979323846 * s.freq / fs_);  // TPT = prewarped bilinear
                const std::complex<double> ha = s.responseNorm(f / s.freq, s.q), hd = s.responseNorm(wd, qd);
                if (std::abs(hd) > 1e-12 && std::abs(ha) > 1e-12) ratio *= (ha / std::abs(ha)) / (hd / std::abs(hd));
            }
            C[static_cast<size_t>(k)] = ratio * std::polar(1.0, -2.0 * 3.14159265358979323846 * k * kNatDelay / N);
        }
        C[static_cast<size_t>(N / 2)] = std::abs(C[static_cast<size_t>(N / 2)].real());  // Nyquist bin must be real
        for (int k = 1; k < N / 2; ++k) C[static_cast<size_t>(N - k)] = std::conj(C[static_cast<size_t>(k)]);
        fft.inverse(C);
        auto& h = nat_[static_cast<size_t>(p)];
        for (int n = 0; n < kNatTaps; ++n) {
            const double w = 0.5 + 0.5 * std::cos(3.14159265358979323846 * (n - kNatDelay) / (kNatDelay + 1));
            h[static_cast<size_t>(n)] = C[static_cast<size_t>(n)].real() * w;
        }
    }
}

void Processor::buildLinear(bool immediate) {
    for (int p = 0; p < 2; ++p) {
        std::vector<BandShape> lin;
        for (int b = 0; b < kBands; ++b) if (active(b) && onPath(b, p)) lin.push_back(shape(b));
        conv_[static_cast<size_t>(p)].setKernel(designKernel(lin, {}, L_, fs_), immediate);
    }
    sinceKernel_ = 0;
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    configure(0);
    if (mode_ == Natural) buildNatural();
    if (mode_ == Linear) buildLinear(true);
    kernelDirty_ = false;
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    if (assist_) res_.process(ch, nch, n);   // the input, before anything touches it
    const bool ms = target_[Ms] > 0.5 && nch == 2;
    if (mode_ == Linear) {  // static part: convolution (kernel rebuilt at most once per 20 ms crossfade)
        sinceKernel_ += n;
        if (kernelDirty_ && sinceKernel_ >= fadeLen_ && !conv_[0].fading() && !conv_[1].fading()) { buildLinear(false); kernelDirty_ = false; }
    } else if (kernelDirty_ && mode_ == Natural) { buildNatural(); kernelDirty_ = false; }
    for (int start = 0; start < n; start += kControl) {
        const int len = std::min(kControl, n - start);
        // encode
        for (int i = start; i < start + len; ++i)
            if (ms) { const float l = ch[0][i], r = ch[1][i]; ch[0][i] = 0.5f * (l + r); ch[1][i] = 0.5f * (l - r); }
        // dynamic offsets from the band-limited detectors (on this block's input)
        bool moved = dirty_;
        for (int b = 0; b < kBands; ++b) {
            if (!active(b) || !dynamic(b)) {
                for (double& o : band_[static_cast<size_t>(b)].offset) if (o != 0.0) { o = 0.0; moved = true; }
                continue;
            }
            Band& bd = band_[static_cast<size_t>(b)];
            for (int p = 0; p < nch; ++p) {
                if (!onPath(b, p)) continue;
                double e = bd.env[static_cast<size_t>(p)];
                for (int i = start; i < start + len; ++i) {
                    const double a = std::abs(bd.det[static_cast<size_t>(p)].process(ch[p][i]));
                    e = a + (a > e ? atk_ : rel_) * (e - a);
                }
                bd.env[static_cast<size_t>(p)] = e;
                const double over = 20.0 * std::log10(std::max(e, 1e-9)) - t(b, DynThresh);
                const double off = t(b, DynRange) * std::clamp(over / kDynFullDb, 0.0, 1.0);
                if (std::abs(off - bd.offset[static_cast<size_t>(p)]) > 0.01) { bd.offset[static_cast<size_t>(p)] = off; moved = true; }
            }
        }
        if (moved) configure(len);
        if (mode_ == Linear) {
            for (int p = 0; p < nch; ++p) { float* c[1] = {ch[p] + start}; conv_[static_cast<size_t>(p)].process(c, 1, len); }
        }
        for (int i = start; i < start + len; ++i) {
            for (int p = 0; p < nch; ++p) {
                double y = ch[p][i];
                for (int b = 0; b < kBands; ++b) {
                    if (!active(b) || !onPath(b, p)) continue;
                    if (mode_ == Linear && !dynamic(b)) continue;  // static bands live in the FIR
                    y = band_[static_cast<size_t>(b)].f[static_cast<size_t>(p)].process(y);
                }
                if (mode_ == Natural) {  // short phase-correction FIR
                    auto& hist = natHist_[static_cast<size_t>(p)]; const auto& h = nat_[static_cast<size_t>(p)];
                    hist[static_cast<size_t>(natPos_)] = y;
                    double acc = 0; int idx = natPos_;
                    for (int k = 0; k < kNatTaps; ++k) { acc += h[static_cast<size_t>(k)] * hist[static_cast<size_t>(idx)]; idx = idx == 0 ? kNatTaps - 1 : idx - 1; }
                    y = acc;
                }
                ch[p][i] = static_cast<float>(std::abs(y) < 1e-30 ? 0.0 : y);
            }
            if (mode_ == Natural) natPos_ = (natPos_ + 1) % kNatTaps;
            if (ms) { const float m = ch[0][i], s = ch[1][i]; ch[0][i] = m + s; ch[1][i] = m - s; }
        }
    }
}

// the tail: a band with a high Q rings like a resonator, 2.9 Q / f seconds to fall by 80 dB (the cap is the longest a sane setting needs)
double Processor::tailSeconds() const {
    double t = 0.0;
    for (int b = 0; b < kBands; ++b) if (active(b) && std::abs(this->t(b, Gain)) > 0.05) t = std::max(t, 2.93 * this->t(b, Q) / std::max(this->t(b, Freq), 10.0));
    return std::min(t, 10.0);
}

}  // namespace sw::eq02
