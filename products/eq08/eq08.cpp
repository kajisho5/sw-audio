#include "eq08/eq08.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace sw::eq08 {

namespace {
constexpr double kMixedSplitHz = 200.0;      // spec proposal
constexpr double kGuardDb = -60.0;           // spec proposal: pre-ring energy vs main peak
constexpr double kGuardWindowMs = 3.0;       // design value: only ringing earlier than 3 ms before the peak counts
const char* kIds[kPerBand] = {"on", "type", "freq", "gain", "q"};
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        static std::vector<std::string> ids;  // keeps the c-strings alive
        ids.reserve(kBands * kPerBand);
        static const double defFreq[5] = {60, 250, 1000, 2400, 10000};
        std::vector<ParamSpec> v;
        for (int b = 0; b < kBands; ++b) {
            for (int f = 0; f < kPerBand; ++f) ids.push_back("eq08.b" + std::to_string(b + 1) + "." + kIds[f]);
            const char* const* id = nullptr; (void)id;
            const size_t base = static_cast<size_t>(b * kPerBand);
            v.push_back({ids[base + 0].c_str(), "On", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
            v.push_back({ids[base + 1].c_str(), "Type", 0, 4, 0, Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Bell", "Lo shelf", "Hi shelf", "Lo cut", "Hi cut"}});
            v.push_back({ids[base + 2].c_str(), "Freq", 10, 30000, b < 5 ? defFreq[b] : 1000, Curve::Log, 1, {}, "Hz"});
            v.push_back({ids[base + 3].c_str(), "Gain", -18, 18, 0, Curve::Lin, 1, {}, "dB"});
            v.push_back({ids[base + 4].c_str(), "Q", 0.1, 10, 0.7, Curve::Log, 1, {}, ""});
        }
        v.push_back({"eq08.phase", "Phase", 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"Linear", "Minimum", "Mixed"}});
        v.push_back({"eq08.evo.on", "Pre-ring guard", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"eq08.ms", "Mid/side", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"Off", "On"}});
        v.push_back({"eq08.out", "Output", -18, 18, 0, Curve::Lin, 1, {}, "dB"});
        v.push_back({"eq08.length", "Length", 2048, 8192, 2048, Curve::Step, 1, {2048, 4096, 8192}, "", {"2048 (spec)", "4096", "8192"}});  // decision: spec default, longer for low-end accuracy
        return v;
    }();
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::kernelLengthFor(double fs, double base) {  // base taps at 48 kHz, same duration at other rates (power of two)
    int L = 2048;
    while (L < base * fs / 48000.0 * 0.92) L *= 2;
    return L;
}

int Processor::latencySamples() const {
    if (static_cast<int>(target_[Phase]) == Minimum) return 0;
    return kernelLengthFor(fs_, target_[Length]) / 2 + B_;
}

namespace {
BandShape shapeOf(const std::array<double, kNumParams>& tt, double fs, int b) {
    const double* t = &tt[static_cast<size_t>(b * kPerBand)];
    static const BandShape::Type types[5] = {BandShape::Bell, BandShape::LowShelf, BandShape::HighShelf, BandShape::LowCut, BandShape::HighCut};
    BandShape s;
    s.type = types[static_cast<int>(t[Type])];
    s.freq = std::min(t[Freq], 0.49 * fs);
    s.gainDb = t[Gain];
    s.q = t[Q];
    s.slope = 12;  // EQ08 has no slope control: cuts are 12 dB/oct following Q
    return s;
}
}  // namespace
BandShape Processor::shape(int b) const { return shapeOf(target_, fs_, b); }
BandShape Processor::Design::shape(int b) const { return shapeOf(t, fs, b); }

bool Processor::Design::guarded(int b) {
    GuardCache& c = cache[static_cast<size_t>(b)];
    const double* tt = &t[static_cast<size_t>(b * kPerBand + Type)];
    if (c.key[0] == tt[0] && c.key[1] == tt[1] && c.key[2] == tt[2] && c.key[3] == tt[3]) return c.guard;
    for (int i = 0; i < 4; ++i) c.key[i] = tt[i];
    const BandShape one = shape(b);
    const auto& h = designer.design([&](double f) { return one.magnitude(f); }, [](double) { return 1.0; }, false, fs);
    const int L = static_cast<int>(h.size());
    const int D = L / 2, edge = D - static_cast<int>(kGuardWindowMs * 0.001 * fs);
    double pre = 0; for (int n = 0; n < edge; ++n) pre += h[static_cast<size_t>(n)] * h[static_cast<size_t>(n)];
    c.guard = 10.0 * std::log10(pre / std::max(h[static_cast<size_t>(D)] * h[static_cast<size_t>(D)], 1e-30) + 1e-300) > kGuardDb;
    return c.guard;
}

const std::vector<double>& Processor::Design::run() {
    ranOn = std::this_thread::get_id();
    lin.clear(); mn.clear();
    const bool guard = t[Guard] > 0.5;
    for (int b = 0; b < kBands; ++b) {
        if (!active(b)) continue;
        if (guard && guarded(b)) mn.push_back(shape(b)); else lin.push_back(shape(b));
    }
    const std::vector<double>* h;
    if (mode == Mixed) {  // split the linear part's magnitude in the log domain around 200 Hz
        auto w = [](double f) { return 1.0 / (1.0 + std::pow(f / kMixedSplitHz, 4.0)); };
        h = &designer.design([&](double f) { return std::pow(totalMagnitude(lin, f), 1.0 - w(f)); },
                             [&](double f) { return totalMagnitude(mn, f) * std::pow(totalMagnitude(lin, f), w(f)); }, true, fs);
    } else {
        h = &designer.design([&](double f) { return totalMagnitude(lin, f); }, [&](double f) { return totalMagnitude(mn, f); }, !mn.empty(), fs);
    }
    result = h;
    return *h;
}

// the design on the calling thread (before playing: snapToTargets; or when there is no worker)
void Processor::rebuildKernel(bool immediate) {
    sync_.t = target_; sync_.fs = fs_; sync_.mode = mode_;
    conv_.setKernel(sync_.run(), immediate);
    lastThread_ = sync_.ranOn;
    applied_.store(applied_.load() + 1);
    sinceKernel_ = 0;
    dirty_ = false;
}

void Processor::updateIir(int ramp) {
    for (int b = 0; b < kBands; ++b) {
        const BandShape s = shape(b);
        const double g = active(b) ? s.gainDb : 0.0;
        Svf::Mode m = Svf::Mode::Bell;
        switch (s.type) {
            case BandShape::LowShelf: m = Svf::Mode::LowShelf; break;
            case BandShape::HighShelf: m = Svf::Mode::HighShelf; break;
            case BandShape::LowCut: m = Svf::Mode::HighPass; break;
            case BandShape::HighCut: m = Svf::Mode::LowPass; break;
            default: break;
        }
        const bool cut = s.type == BandShape::LowCut || s.type == BandShape::HighCut;
        for (auto& c : iir_) {
            if (cut && !active(b)) c[static_cast<size_t>(b)].setupRamp(Svf::Mode::Bell, s.freq, fs_, s.q, 0.0, ramp);  // inactive cut = flat
            else c[static_cast<size_t>(b)].setupRamp(m, s.freq, fs_, s.q, g, ramp);
        }
    }
    iirDirty_ = false;
}

void Processor::prepare(double sampleRate, int) {
    job_.stop();   // (a thread from the last prepare)
    fs_ = sampleRate;
    mode_ = static_cast<int>(target_[Phase]);
    L_ = kernelLengthFor(fs_, target_[Length]);
    fadeLen_ = static_cast<int>(std::lround(0.020 * fs_));
    conv_.prepare(L_, B_, 2);
    conv_.setFadeSamples(fadeLen_);
    sync_.prepare(L_);
    const bool worker = wantWorker_ && mode_ != Minimum;
    if (worker) wd_.prepare(L_);
    st_.store(0); applied_.store(0);
    for (auto& c : iir_) for (auto& f : c) f.reset();
    sideDelay_.assign(static_cast<size_t>(std::max(1, latencySamples())), 0.0f);
    dpos_ = 0;
    scratch_.assign(4096, 0.0f);
    prepared_ = true;
    snapToTargets();
    if (worker) job_.start([this] { wd_.run(); st_.store(2); });
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v));
    if (id < kBands * kPerBand || id == Guard) { dirty_ = true; iirDirty_ = true; }
}

void Processor::snapToTargets() {
    if (!prepared_) return;  // nothing to build before prepare()
    if (mode_ == Minimum) { updateIir(0); return; }
    if (job_.running()) { job_.waitIdle(); st_.store(0); }   // a design on its way is older than this one
    rebuildKernel(true);
}

void Processor::process(float** ch, int numCh, int n) {
    const int nch = std::min(numCh, 2);
    const bool ms = target_[Ms] > 0.5 && nch == 2;
    if (mode_ == Minimum) {
        if (iirDirty_) updateIir(64);
        for (int i = 0; i < n; ++i) {
            if (ms) {
                const double m = 0.5 * (ch[0][i] + ch[1][i]), s = 0.5 * (ch[0][i] - ch[1][i]);
                double y = m; for (auto& f : iir_[0]) y = f.process(y);
                ch[0][i] = static_cast<float>(y + s); ch[1][i] = static_cast<float>(y - s);
            } else {
                for (int c = 0; c < nch; ++c) { double y = ch[c][i]; for (auto& f : iir_[static_cast<size_t>(c)]) y = f.process(y); ch[c][i] = static_cast<float>(y); }
            }
        }
        return;
    }
    // Linear / Mixed: a new kernel at most once per crossfade (20 ms), only at block boundaries of the host call
    sinceKernel_ += n;
    if (job_.running()) {
        if (st_.load() == 2 && !conv_.fading()) {   // the worker's kernel
            conv_.setKernel(*wd_.result, false); lastThread_ = wd_.ranOn; applied_.store(applied_.load() + 1);
            sinceKernel_ = 0; st_.store(0);
        }
        if (dirty_ && st_.load() == 0 && sinceKernel_ >= fadeLen_ && !conv_.fading()) {   // ask for the next one: a copy of the parameters, then the thread is woken
            wd_.t = target_; wd_.fs = fs_; wd_.mode = mode_;
            dirty_ = false; st_.store(1); job_.kick();
            if (offline_.load()) job_.waitIdle();   // a bounce: the design is done before the next block, whatever the thread's schedule (the kernel takes over at the same sample in every run)
        }
    } else if (dirty_ && sinceKernel_ >= fadeLen_ && !conv_.fading()) rebuildKernel(false);
    if (!ms) { conv_.process(ch, nch, n); return; }
    // M/S: the EQ works on the mid; the side is delayed by the same latency (interpretation, see README)
    const int lat = static_cast<int>(sideDelay_.size());
    for (int off = 0; off < n; off += static_cast<int>(scratch_.size())) {
        const int len = std::min(n - off, static_cast<int>(scratch_.size()));
        for (int i = 0; i < len; ++i) {
            const float l = ch[0][off + i], r = ch[1][off + i];
            scratch_[static_cast<size_t>(i)] = 0.5f * (l + r);
            ch[1][off + i] = 0.5f * (l - r);  // side, held in the right buffer for now
        }
        float* mp[1] = {scratch_.data()};
        conv_.process(mp, 1, len);
        for (int i = 0; i < len; ++i) {
            const float sd = sideDelay_[static_cast<size_t>(dpos_)];
            sideDelay_[static_cast<size_t>(dpos_)] = ch[1][off + i];
            dpos_ = (dpos_ + 1) % lat;
            const float m = scratch_[static_cast<size_t>(i)];
            ch[0][off + i] = m + sd;
            ch[1][off + i] = m - sd;
        }
    }
}

}  // namespace sw::eq08
