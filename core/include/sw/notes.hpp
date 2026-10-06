// SW AUDIO core — note lengths for tempo-synced times (DL01 Echo, DL04 Multitap, DL05 Reverse, MD ...)
//   A fixed, sorted list of 18 note lengths in quarter notes: 1/64 ... 2 bars (4/4), with triplets (T) and dotted notes (D).
//   noteIndexFromNorm maps a knob position 0..1 evenly onto the list.
#pragma once
#include <algorithm>
#include <cmath>

namespace sw {

constexpr int kNumNotes = 18;
inline double noteQuarters(int i) {
    static const double q[kNumNotes] = {0.0625, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.1875, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0, 8.0};
    return q[std::clamp(i, 0, kNumNotes - 1)];
}
inline const char* noteName(int i) {
    static const char* n[kNumNotes] = {"1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 bar", "2 bars"};
    return n[std::clamp(i, 0, kNumNotes - 1)];
}
inline int noteIndexFromNorm(double x) { return std::clamp(static_cast<int>(std::lround(std::clamp(x, 0.0, 1.0) * (kNumNotes - 1))), 0, kNumNotes - 1); }
inline double noteSeconds(int i, double bpm) { return noteQuarters(i) * 60.0 / std::max(bpm, 1.0); }

}  // namespace sw
