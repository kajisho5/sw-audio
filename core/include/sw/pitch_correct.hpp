// SW AUDIO core — pitch correction logic (VO01 Tune, VO02 Tune Rt): measured pitch -> the pitch factor for sw::PsolaSynth, evaluated at every synthesis mark.
//   m = 69 + 12 log2(f0 / 440) (semitones). c: the singer's slow pitch (one-pole, 120 ms; restarted on a jump of 1.5 semitones), n: the note of the scale nearest to a quicker average of m (40 ms: the
//   note changes soon after the singer moves, with a hysteresis of 0.2 semitone so that it does not flutter at a boundary), cc: the corrected centre, moving towards n with the time constant Speed (0 = at once), humanize keeps 60 % x Humanize of the singer's offset (c - n),
//   vibrato v = m - c is passed on x kV (Natural 1, Reduce 0.4, Flat 0). out = cc + 0.6 h (c - n) + kV v + Transpose; the factor is 2^((out - m) / 12) (taken to the power `strength`, which a screen or
//   VO02's "unstable stretch" logic can lower). Formant: Follow = the same factor, Keep = 1, then times 2^(semitones / 12).
//   Key detection (EVO): a decaying histogram of the pitch classes heard (time weighted, 30 s) is correlated with the Krumhansl-Schmuckler major profile in all 12 keys -> suggestedKey().
#pragma once
#include "sw/pitch_engine.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace sw {

// the note `deg` steps of the scale above (deg > 0) or below (deg < 0) `note` (MIDI), counting only notes the mask allows
inline int stepScale(uint16_t mask, int note, int deg) {
    if (deg == 0 || mask == 0) return note;
    const int dir = deg > 0 ? 1 : -1; int cnt = deg > 0 ? deg : -deg, x = note;
    for (int guard = 0; cnt > 0 && guard < 200; ++guard) { x += dir; if ((mask >> (((x % 12) + 12) % 12)) & 1) --cnt; }
    return x;
}
// the interval of `deg` degrees of a major scale in semitones (+-1 octave = +-7 degrees): the fixed intervals of VO03
inline int majorDegreeSemitones(int deg) {
    static const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    const int oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), idx = deg - 7 * oct;
    return 12 * oct + major[idx];
}

inline uint16_t scaleBits(int type, int key) {   // type 0 major, 1 natural minor, 2 chromatic; bit k = the note k semitones above C is allowed
    static const int major[7] = {0, 2, 4, 5, 7, 9, 11}, minor[7] = {0, 2, 3, 5, 7, 8, 10};
    uint16_t m = 0;
    if (type == 2) return 0x0FFF;
    for (int i = 0; i < 7; ++i) m = static_cast<uint16_t>(m | (1u << ((((type == 1 ? minor[i] : major[i]) + key) % 12))));
    return m;
}

class PitchCorrector : public RatioSource {
public:
    struct Settings {
        uint16_t mask = 0x0AB5;       // C major
        double speedMs = 20.0, humanize = 0.4, vibrato = 1.0, formantSemis = 0.0, transpose = 0.0, strength = 1.0;
        bool formantFollow = false, enabled = true, weakenWhenUnstable = false;
        // harmony voices: the target is the note `degrees` scale steps from the singer's note (harmonyDegrees) or a fixed interval above it (harmonyFixed, semitones); extraSemis: a drift added to the output
        int harmonyMode = 0;        // 0 off (correction), 1 degrees, 2 fixed
        int harmonyDegrees = 0; double harmonyFixed = 0.0, extraSemis = 0.0;
        uint16_t chordMask = 0;     // harmony voices: when not 0 (bit k = pitch class k is in the chord), the harmony note moves to the nearest chord tone (a tie: the lower one)
    };
    void setSettings(const Settings& s) { s_ = s; }
    const Settings& settings() const { return s_; }
    void reset() { have_ = false; haveN_ = false; cn_ = 0.0; lastN_ = -1000; note_ = -1; cc_ = c_ = 0.0; prevM_ = 0.0; unstable_ = 0; hist_.fill(0.0); lastOut_ = lastM_ = 0.0; }
    void ratio(double f0, bool voiced, double dt, double& r, double& fm) override {
        r = 1.0; fm = 1.0;
        if (!voiced || f0 <= 0.0) { have_ = false; haveN_ = false; lastN_ = -1000; return; }
        const double m = 69.0 + 12.0 * std::log2(f0 / 440.0);
        // key histogram (pitch class of the nearest semitone), 30 s decay
        { const double dec = std::exp(-dt / 30.0); for (auto& h : hist_) h *= dec; const int pc = ((static_cast<int>(std::lround(m)) % 12) + 12) % 12; hist_[static_cast<size_t>(pc)] += dt; }
        if (!have_ || std::abs(m - c_) > 1.5) { c_ = m; if (!have_ || std::abs(m - prevM_) > 1.5) { cc_ = m; note_ = -1; } have_ = true; }
        const double kc = 1.0 - std::exp(-dt / 0.12), kn = 1.0 - std::exp(-dt / 0.04);
        c_ += kc * (m - c_);
        if (!haveN_ || std::abs(m - cn_) > 1.5) cn_ = m; else cn_ += kn * (m - cn_);
        haveN_ = true;
        const int nSinger = nearestNote(cn_);
        int n = s_.harmonyMode == 1 ? stepScale(s_.mask, nSinger, s_.harmonyDegrees) : s_.harmonyMode == 2 ? nSinger + static_cast<int>(std::lround(s_.harmonyFixed)) : nSinger;
        if (s_.chordMask != 0 && s_.harmonyMode != 0) n = nearestInMask(n, s_.chordMask);
        if (nSinger != lastN_) { if (lastN_ != -1000) c_ = cn_; lastN_ = nSinger; }   // a new note: the slow centre starts from where the singer is, so the step is not mistaken for vibrato
        const double tau = s_.speedMs * 0.001;
        if (tau <= 1e-6) cc_ = n; else cc_ += (n - cc_) * (1.0 - std::exp(-dt / tau));
        const double v = m - c_;
        const double out = cc_ + 0.6 * s_.humanize * (c_ - nSinger) + s_.vibrato * v + s_.transpose + s_.extraSemis;
        double strength = s_.strength;
        if (s_.weakenWhenUnstable) { if (std::abs(m - prevM_) > 2.5) unstable_ = 4; if (unstable_ > 0) { strength *= 0.3; --unstable_; } }
        prevM_ = m; lastOut_ = out; lastM_ = m;
        if (!s_.enabled) { r = std::exp2(s_.transpose / 12.0); if (s_.formantFollow) fm = r; fm *= std::exp2(s_.formantSemis / 12.0); return; }
        r = std::exp2((out - m) / 12.0 * strength);
        fm = (s_.formantFollow ? r : 1.0) * std::exp2(s_.formantSemis / 12.0);
    }
    int lastNote() const { return note_; }
    double lastOutputSemitones() const { return lastOut_; }
    double lastMeasuredSemitones() const { return lastM_; }
    // the key (0 = C ... 11 = B) whose major scale fits what was sung best, and how well (correlation, -1 .. 1; 0 when nothing was heard)
    int suggestedKey(double* confidence = nullptr) const {
        static const double prof[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
        double tot = 0; for (double h : hist_) tot += h;
        if (tot < 1.0) { if (confidence) *confidence = 0.0; return 0; }
        int best = 0; double bv = -2.0;
        for (int k = 0; k < 12; ++k) {
            double mh = tot / 12.0, mp = 0; for (double p : prof) mp += p; mp /= 12.0;
            double num = 0, dh = 0, dp = 0;
            for (int i = 0; i < 12; ++i) { const double h = hist_[static_cast<size_t>((i + k) % 12)] - mh, p = prof[i] - mp; num += h * p; dh += h * h; dp += p * p; }
            const double r = num / std::sqrt(dh * dp + 1e-30);
            if (r > bv) { bv = r; best = k; }
        }
        if (confidence) *confidence = bv;
        return best;
    }

private:
    static int nearestInMask(int n, uint16_t mask) {
        for (int d = 0; d <= 6; ++d) {
            if ((mask >> ((((n - d) % 12) + 12) % 12)) & 1) return n - d;   // the lower one first: a tie goes down
            if ((mask >> ((((n + d) % 12) + 12) % 12)) & 1) return n + d;
        }
        return n;
    }
    int nearestNote(double c) {
        const int base = static_cast<int>(std::floor(c));
        int best = -1; double bd = 1e9;
        for (int cand = base - 6; cand <= base + 7; ++cand) {
            if (!((s_.mask >> (((cand % 12) + 12) % 12)) & 1)) continue;
            const double d = std::abs(cand - c) - (cand == note_ ? 0.2 : 0.0);
            if (d < bd) { bd = d; best = cand; }
        }
        if (best < 0) best = static_cast<int>(std::lround(c));   // an empty mask: no correction target, keep the nearest semitone
        note_ = best;
        return best;
    }
    Settings s_;
    bool have_ = false, haveN_ = false;
    double cn_ = 0.0;
    int note_ = -1, unstable_ = 0, lastN_ = -1000;
    double cc_ = 0.0, c_ = 0.0, prevM_ = 0.0, lastOut_ = 0.0, lastM_ = 0.0;
    std::array<double, 12> hist_{};
};

}  // namespace sw
