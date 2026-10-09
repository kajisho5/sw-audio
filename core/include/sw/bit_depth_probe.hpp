// SW AUDIO core — "which grid is the input on" (MS07 Dither, Truncation check; spec: 入力の実効ビット数を調べ、すでに切り捨てられた信号…を表示する): every non-zero sample of the input is a float with 24 significant
// bits; a sample that came from an n-bit file at unity gain is a multiple of 2^(1-n), so its lowest set bit says how coarse it is: n(x) = 25 - exponent - trailing zero bits of the mantissa. The probe listens until it
// has seen 5 s of signal (frames with a non-zero sample; digital silence does not count; it gives up after 60 s) and answers with the smallest of 8 / 12 / 16 / 20 / 24 bits that 99.9 % of the non-zero samples
// fit in, or kFloat when they fit in none (a float signal, or one that was processed after it was quantized: the grid is lost). Per sample, allocates nothing.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace sw {

class BitDepthProbe {
public:
    static constexpr int kFloat = 32;                 // bits(): on no grid up to 24 bit
    static constexpr double kSignalSeconds = 5.0, kGiveUpSeconds = 60.0, kFraction = 0.999;

    void prepare(double fs) { fs_ = fs; cancel(); }
    void start() { cancel(); listening_ = true; }
    void cancel() { listening_ = false; done_ = false; bits_ = 0; signal_ = total_ = 0; hist_.fill(0); count_ = 0; }
    bool listening() const { return listening_; }
    bool done() const { return done_; }
    int bits() const { return bits_; }                // 0 until done
    double progress() const { return listening_ || done_ ? std::min(1.0, static_cast<double>(signal_) / (kSignalSeconds * fs_)) : 0.0; }

    // one sample of each channel (a mono input passes it twice)
    void add(float l, float r) {
        if (!listening_) return;
        const bool nz = l != 0.0f || r != 0.0f;
        if (nz) { note(l); if (r != l) note(r); ++signal_; }
        if (signal_ >= static_cast<long>(kSignalSeconds * fs_)) { finish(); return; }
        if (++total_ >= static_cast<long>(kGiveUpSeconds * fs_)) listening_ = false;
    }

private:
    // the number of bits of the grid x is on (up to 60; a denormal or a tiny value is far beyond 24)
    static int gridBits(float x) {
        int e; const double f = std::frexp(static_cast<double>(x), &e);                  // x = f 2^e, 0.5 <= |f| < 1
        uint32_t m = static_cast<uint32_t>(std::ldexp(std::fabs(f), 24));                // the 24-bit integer mantissa (exact for a float)
        int tz = 0; while (m != 0 && !(m & 1u)) { m >>= 1; ++tz; }
        return std::min(60, std::max(1, 25 - e - tz));
    }
    void note(float x) { if (x != 0.0f && std::isfinite(x)) { ++hist_[static_cast<size_t>(gridBits(x))]; ++count_; } }
    void finish() {
        listening_ = false; done_ = true; bits_ = kFloat;
        long cum = 0; size_t k = 0;
        for (int cand : {8, 12, 16, 20, 24}) {
            for (; k <= static_cast<size_t>(cand); ++k) cum += hist_[k];
            if (count_ > 0 && static_cast<double>(cum) >= kFraction * static_cast<double>(count_)) { bits_ = cand; return; }
        }
    }
    double fs_ = 48000.0;
    bool listening_ = false, done_ = false;
    int bits_ = 0;
    long signal_ = 0, total_ = 0, count_ = 0;
    std::array<long, 61> hist_{};
};

}  // namespace sw
