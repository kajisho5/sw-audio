#include "rv04/rv04.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sw::rv04 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rv04.category", "Category", 0, 4, 0,   Curve::Step, 1, {0, 1, 2, 3, 4}, "", {"Halls", "Rooms", "Churches", "Gear", "Custom"}},
        {"rv04.predelay", "Pre-delay", 0, 500, 12, Curve::Skew, 2, {}, "ms"},
        {"rv04.length",   "Length",   10, 100, 100, Curve::Lin, 1, {}, "%"},
        {"rv04.size",     "Size",     50, 150, 100, Curve::Lin, 1, {}, "%"},
        {"rv04.lowcut",   "Low cut",  20, 1000, 80, Curve::Log, 1, {}, "Hz"},
        {"rv04.highcut",  "High cut", 1000, 20000, 12000, Curve::Log, 1, {}, "Hz"},
        {"rv04.reverse",  "Reverse",  0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"rv04.mix",      "Mix",      0, 100, 20,  Curve::Lin, 1, {}, "%"},
        {"rv04.evo.on",   "Bar fit",  0, 1, 0,     Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBandHz[7] = {125, 250, 500, 1000, 2000, 4000, 8000};
struct CatDesign { double rt[7]; double seconds; int erCount; double erMs; bool disperse; };
constexpr CatDesign kCat[4] = {
    {{3.4, 3.2, 2.9, 2.6, 2.0, 1.4, 0.8}, 5.0, 24, 90.0, false},      // Halls
    {{0.9, 0.85, 0.75, 0.65, 0.55, 0.4, 0.25}, 1.6, 16, 35.0, false},  // Rooms
    {{7.0, 6.8, 6.2, 5.5, 4.2, 2.8, 1.4}, 8.0, 28, 140.0, false},     // Churches
    {{2.8, 2.6, 2.4, 2.2, 2.0, 1.8, 1.4}, 3.5, 0, 0.0, true}};        // Gear (dispersive, dense from the start)
constexpr size_t kSynthChunk = 16384, kTransformChunk = 65536, kNormChunk = 32768;   // one chunk per process call: about 2 ms or less each (-O2 measurement)
constexpr int kPartsPerStep = 1;
double u01(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r / 4294967296.0; }
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateFilters() {
    for (int c = 0; c < 2; ++c) {
        hp_[static_cast<size_t>(c)].setup(Svf::Mode::HighPass, target_[LowCut], fs_, 0.70710678, 0);
        lp_[static_cast<size_t>(c)].setup(Svf::Mode::LowPass, std::min(target_[HighCut], 0.45 * fs_), fs_, 0.70710678, 0);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    maxLen_ = static_cast<size_t>(10.0 * fs_);
    for (int c = 0; c < 2; ++c) {
        conv_[static_cast<size_t>(c)].prepare(static_cast<int>(maxLen_), static_cast<int>(0.02 * fs_), c);
        base_[static_cast<size_t>(c)].assign(maxLen_, 0.0f);
        kernel_[static_cast<size_t>(c)].assign(maxLen_, 0.0);
        pre_[static_cast<size_t>(c)].assign(static_cast<size_t>(0.5 * fs_) + 8, 0.0f);
    }
    prePos_ = 0; preLen_ = target_[PreDelay] * 0.001 * fs_;
    baseLen_ = 0; synthCat_ = -1; stage_ = Idle; need_ = 2;
    updateFilters();
    prepared_ = true;
    runAllSync();
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    switch (id) {
        case Category: markSynth(); break;
        case Length: case Size: case Reverse: case BarFit: markTransform(); break;
        case LowCut: case HighCut: updateFilters(); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateFilters();
    preLen_ = target_[PreDelay] * 0.001 * fs_;
    if (categoryOf() != synthCat_ || categoryOf() == Custom) markSynth(); else markTransform();
    runAllSync();
}

void Processor::runAllSync() {   // prepare / state load: everything now, and the new IR in place at once
    sync_ = true;
    while (need_ > 0 || stage_ != Idle) {
        if (stage_ == Idle) startJob();
        while (stage_ != Idle) stepJob();
    }
    sync_ = false;
}

// ---- the job: [Synth ->] Transform -> Normalise -> Load (-> commit)
void Processor::startJob() {
    const int cat = categoryOf();
    const bool needSynth = need_ >= 2 || cat != synthCat_;
    need_ = 0;
    jobPos_ = 0;
    if (cat == Custom && needSynth) {
        // build the base from the loaded IR (or a unit impulse)
        for (int c = 0; c < 2; ++c) std::fill(base_[static_cast<size_t>(c)].begin(), base_[static_cast<size_t>(c)].end(), 0.0f);
        if (custom_.empty()) { base_[0][0] = base_[1][0] = 1.0f; baseLen_ = 1; }
        else {
            const size_t frames = custom_.size() / static_cast<size_t>(customCh_);
            const double ratio = customRate_ / fs_;
            baseLen_ = std::min(maxLen_, static_cast<size_t>(static_cast<double>(frames) / ratio));
            for (size_t i = 0; i < baseLen_; ++i) {
                const double pos = static_cast<double>(i) * ratio; const size_t i0 = static_cast<size_t>(pos); const double fr = pos - static_cast<double>(i0);
                const size_t i1 = std::min(frames - 1, i0 + 1);
                for (int c = 0; c < 2; ++c) {
                    const size_t ch = static_cast<size_t>(std::min(c, customCh_ - 1));
                    const double a = custom_[i0 * static_cast<size_t>(customCh_) + ch], b = custom_[i1 * static_cast<size_t>(customCh_) + ch];
                    base_[static_cast<size_t>(c)][i] = static_cast<float>(a + fr * (b - a));
                }
            }
        }
        synthCat_ = Custom;
        stage_ = Transform;
    } else if (needSynth) {
        const CatDesign& d = kCat[cat];
        synthLen_ = std::min(maxLen_, static_cast<size_t>(d.seconds * fs_));
        for (int c = 0; c < 2; ++c) {
            SynthCh& y = sy_[static_cast<size_t>(c)];
            y = SynthCh{};
            y.rng = c == 0 ? 0x1badf00du : 0x2545f491u;
            for (int b = 0; b < 7; ++b) {
                y.band[static_cast<size_t>(b)].bp.setup(Svf::Mode::BandPass, std::min(kBandHz[b], 0.45 * fs_), fs_, 1.4, 0);
                y.band[static_cast<size_t>(b)].env = 1.0;
                y.band[static_cast<size_t>(b)].step = std::exp(-6.907755 / (d.rt[b] * fs_));   // -60 dB in rt seconds (amplitude)
            }
        }
        synthCat_ = cat; baseLen_ = synthLen_;
        stage_ = Synth;
    } else {
        stage_ = Transform;
    }
    // snapshot of what the transform uses
    jobCat_ = cat;
    if (stage_ == Transform) beginTransform();
}

void Processor::beginTransform() {
    const double natural = static_cast<double>(baseLen_);
    double t = target_[Length] * 0.01 * natural;
    if (target_[BarFit] > 0.5 && bpm_ > 0.0) {   // the largest whole number of beats (1, 2, 4, 8, 16, 32) not above the length asked for
        const double beat = 60.0 / bpm_ * fs_;
        double best = 0;
        for (int k : {1, 2, 4, 8, 16, 32}) if (k * beat <= t + 1e-6 && k * beat <= natural) best = k * beat;
        if (best > 0) t = best;
    }
    lt_ = std::max<size_t>(1, std::min(baseLen_, static_cast<size_t>(std::lround(t))));
    fade_ = std::max<size_t>(1, static_cast<size_t>(std::max(0.1 * static_cast<double>(lt_), 0.02 * fs_)));
    if (fade_ > lt_) fade_ = lt_;
    sizeRatio_ = target_[Size] * 0.01;
    outLen_ = std::min(maxLen_, std::max<size_t>(1, static_cast<size_t>(std::lround(static_cast<double>(lt_) * sizeRatio_))));
    rev_ = target_[Reverse] > 0.5;
    energy_ = 0; jobPos_ = 0;
}

void Processor::stepJob() {
    switch (stage_) {
        case Synth: synthChunk(); break;
        case Transform: transformChunk(); break;
        case Normalise: {
            const double g = energy_ > 1e-30 ? 1.0 / std::sqrt(energy_) : 1.0;
            const size_t end = std::min(maxLen_, jobPos_ + kNormChunk);
            for (int c = 0; c < 2; ++c) { auto& k = kernel_[static_cast<size_t>(c)]; for (size_t i = jobPos_; i < end; ++i) k[i] *= g; }
            jobPos_ = end;
            if (jobPos_ >= maxLen_) { stage_ = Load; loaded_ = false; commitIdx_ = 0; for (int c = 0; c < 2; ++c) conv_[static_cast<size_t>(c)].beginKernel(kernel_[static_cast<size_t>(c)], static_cast<int>(outLen_)); irLen_ = static_cast<double>(outLen_); }
            break;
        }
        case Load: {
            if (!loaded_) {
                bool done = true;
                for (int c = 0; c < 2; ++c) done = conv_[static_cast<size_t>(c)].stepKernel(sync_ ? (1 << 20) : kPartsPerStep) && done;
                loaded_ = done;
                if (!sync_) break;
            }
            // commit one channel per call (the left and right crossfades start a block apart, which does not matter over 20 ms)
            conv_[static_cast<size_t>(commitIdx_)].commitKernel(sync_);
            if (sync_ || ++commitIdx_ >= 2) { if (sync_) conv_[1].commitKernel(true); stage_ = Idle; }
            break;
        }
        default: stage_ = Idle; break;
    }
}

void Processor::synthChunk() {
    const CatDesign& d = kCat[jobCat_];
    const size_t end = std::min(synthLen_, jobPos_ + kSynthChunk);
    for (int c = 0; c < 2; ++c) {
        SynthCh& y = sy_[static_cast<size_t>(c)];
        auto& out = base_[static_cast<size_t>(c)];
        for (size_t i = jobPos_; i < end; ++i) {
            const double white = (u01(y.rng) + u01(y.rng) + u01(y.rng) - 1.5) * 2.0;
            double sum = 0;
            for (int b = 0; b < 7; ++b) { SynthBand& sb = y.band[static_cast<size_t>(b)]; sum += sb.bp.process(white) * sb.env * std::sqrt(125.0 / kBandHz[b]); sb.env *= sb.step; }
            if (d.disperse) {   // 48 first-order all-passes, a = -0.7: the highs first (as in RV02)
                double v = sum;
                for (size_t k = 0; k < 48; ++k) { const double yy = -0.7 * v + y.ax[k] + 0.7 * y.ay[k]; y.ax[k] = v; y.ay[k] = yy; v = yy; }
                sum = v;
            }
            const double ramp = std::min(1.0, static_cast<double>(i) / (0.001 * fs_));
            out[i] = static_cast<float>(sum * ramp);
        }
    }
    jobPos_ = end;
    if (jobPos_ >= synthLen_) {
        // early reflections: sparse impulses, a little above the noise level at the start
        for (int c = 0; c < 2; ++c) {
            SynthCh& y = sy_[static_cast<size_t>(c)];
            auto& out = base_[static_cast<size_t>(c)];
            double ms = 0; const size_t n0 = std::min<size_t>(synthLen_, static_cast<size_t>(0.2 * fs_));
            for (size_t i = 0; i < n0; ++i) ms += double(out[i]) * out[i];
            const double rms = std::sqrt(ms / std::max<size_t>(1, n0));
            for (int k = 0; k < d.erCount; ++k) {
                const double t = (k + 1 + 0.7 * u01(y.rng)) / (d.erCount + 1.0) * d.erMs * 0.001 * fs_;
                const size_t at = static_cast<size_t>(t);
                if (at < synthLen_) out[at] += static_cast<float>((u01(y.rng) < 0.5 ? -1.0 : 1.0) * 4.0 * rms * std::exp(-t / (0.5 * d.erMs * 0.001 * fs_)));
            }
        }
        stage_ = Transform; beginTransform();
    }
}

void Processor::transformChunk() {
    const size_t end = std::min(outLen_, jobPos_ + kTransformChunk);
    for (int c = 0; c < 2; ++c) {
        const auto& src = base_[static_cast<size_t>(c)]; auto& k = kernel_[static_cast<size_t>(c)];
        for (size_t i = jobPos_; i < end; ++i) {
            const double pos = static_cast<double>(i) / sizeRatio_;   // in base time
            double v = 0;
            if (pos <= static_cast<double>(lt_) - 1.0) {
                const size_t i0 = static_cast<size_t>(pos); const double fr = pos - static_cast<double>(i0);
                const size_t i1 = std::min(i0 + 1, lt_ - 1);
                v = src[i0] + fr * (src[i1] - src[i0]);
                const double fadeStart = static_cast<double>(lt_ - fade_);
                if (pos > fadeStart) { const double q = (pos - fadeStart) / static_cast<double>(fade_); v *= 0.5 * (1.0 + std::cos(kPi * q)); }
            }
            const size_t dst = rev_ ? outLen_ - 1 - i : i;
            k[dst] = v;
            energy_ += 0.5 * v * v;
        }
    }
    jobPos_ = end;
    if (jobPos_ >= outLen_) {
        for (int c = 0; c < 2; ++c) std::fill(kernel_[static_cast<size_t>(c)].begin() + static_cast<std::ptrdiff_t>(outLen_), kernel_[static_cast<size_t>(c)].end(), 0.0);
        stage_ = Normalise; jobPos_ = 0;
    }
}

void Processor::loadIr(const float* data, size_t frames, int channels, double sourceRate) {
    channels = std::clamp(channels, 1, 2);
    custom_.assign(data, data + frames * static_cast<size_t>(channels));
    // an IR from a shared project or a file: values that are not numbers are silence, the rest at most +24 dBFS; a rate outside
    // 1 kHz .. 768 kHz (not a number, zero, negative) is taken as 48 kHz (a tiny rate would ask for an impossible length)
    for (float& v : custom_) v = std::isfinite(v) ? std::clamp(v, -16.0f, 16.0f) : 0.0f;
    customCh_ = channels; customRate_ = std::isfinite(sourceRate) && sourceRate >= 1000.0 && sourceRate <= 768000.0 ? sourceRate : 48000.0;
    if (prepared_ && categoryOf() == Custom) markSynth();
}

void Processor::saveExtra(std::vector<uint8_t>& out) const {
    out.clear();
    if (custom_.empty()) return;
    const uint32_t frames = static_cast<uint32_t>(custom_.size() / static_cast<size_t>(customCh_)); const float rate = static_cast<float>(customRate_);
    out = {'R', '4', 'I', 'R', static_cast<uint8_t>(customCh_), static_cast<uint8_t>(frames), static_cast<uint8_t>(frames >> 8), static_cast<uint8_t>(frames >> 16), static_cast<uint8_t>(frames >> 24)};
    uint8_t rb[4]; std::memcpy(rb, &rate, 4); out.insert(out.end(), rb, rb + 4);
    const size_t at = out.size(); out.resize(at + custom_.size() * 4);
    std::memcpy(out.data() + at, custom_.data(), custom_.size() * 4);
}

void Processor::loadExtra(const uint8_t* d, size_t n) {
    if (n < 13 || d[0] != 'R' || d[1] != '4' || d[2] != 'I' || d[3] != 'R') return;
    const int ch = std::clamp<int>(d[4], 1, 2);
    const uint32_t frames = static_cast<uint32_t>(d[5]) | static_cast<uint32_t>(d[6]) << 8 | static_cast<uint32_t>(d[7]) << 16 | static_cast<uint32_t>(d[8]) << 24;
    float rate; std::memcpy(&rate, d + 9, 4);
    if (n < 13 + static_cast<size_t>(frames) * static_cast<size_t>(ch) * 4) return;
    std::vector<float> v(static_cast<size_t>(frames) * static_cast<size_t>(ch));
    std::memcpy(v.data(), d + 13, v.size() * 4);
    loadIr(v.data(), frames, ch, rate);
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    if (stage_ == Idle && need_ > 0) startJob();
    if (stage_ != Idle) stepJob();
    const size_t preSz = pre_[0].size();
    float* wet[2] = {ch[0], nch > 1 ? ch[1] : nullptr};
    // pre-delay (gliding length, linear interpolation), into the convolvers
    for (int i = 0; i < n; ++i) {
        preLen_ += std::clamp(target_[PreDelay] * 0.001 * fs_ - preLen_, -1.0, 1.0);
        for (int c = 0; c < nch; ++c) {
            auto& b = pre_[static_cast<size_t>(c)];
            b[prePos_] = ch[c][i];
            double rp = static_cast<double>(prePos_) - preLen_; if (rp < 0) rp += static_cast<double>(preSz);
            const size_t i0 = static_cast<size_t>(rp) % preSz, i1 = (i0 + 1) % preSz; const double fr = rp - std::floor(rp);
            ch[c][i] = static_cast<float>(b[i0] + fr * (b[i1] - b[i0]));
        }
        prePos_ = (prePos_ + 1) % preSz;
    }
    for (int c = 0; c < nch; ++c) conv_[static_cast<size_t>(c)].process(wet[c], n);
    for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) {
        double y = lp_[static_cast<size_t>(c)].process(hp_[static_cast<size_t>(c)].process(ch[c][i]));
        if (std::abs(y) < 1e-30) y = 0.0;
        ch[c][i] = static_cast<float>(y);
    }
}

}  // namespace sw::rv04
