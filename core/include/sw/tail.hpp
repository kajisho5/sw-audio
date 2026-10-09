// SW AUDIO core — how long a product rings on after its input has stopped (the CLAP tail extension / VST3 getTailSamples: a host bounces or freezes a track with it).
//   Cores report seconds from their current settings (tailSeconds()); "until it has fallen by 80 dB" is the measure (host_smoke --tails checks it against an impulse). kInfinite: it never stops (self-oscillation).
#pragma once
#include <algorithm>
#include <cmath>

namespace sw::tail {
constexpr double kInfinite = 1e9;
// a loop of `delaySec` whose gain per round trip is `gain` (linear): the time until a repeat is `db` below the first
inline double loop(double delaySec, double gain, double db = 80.0) {
    if (gain >= 0.995) return kInfinite;
    if (gain <= 1e-4) return delaySec;
    return delaySec * (1.0 + std::ceil(db / (-20.0 * std::log10(gain))));
}
// a decay given as the time to fall by 60 dB (RT60), taken down by `db`
inline double fromRt60(double rt60Sec, double db = 80.0) { return rt60Sec * db / 60.0; }
// a loop fed back through several taps of delays d (seconds), each of gain `gainPerTap`: y(t) = x(t) + sum(g y(t - d)). The decay rate s (per second) is the root of sum(g exp(s d)) = 1
// (the sum rises from sum(g) < 1 at s = 0); the time to fall by `db` is db / (8.686 s), and the longest tap is added (the first repeat)
inline double multi(const double* delaySec, int count, double gainPerTap, double db = 80.0) {
    double total = 0, longest = 0; for (int i = 0; i < count; ++i) { total += gainPerTap; longest = std::max(longest, delaySec[i]); }
    if (total >= 0.995) return kInfinite;
    if (total <= 1e-4 || longest <= 0.0) return longest;
    double lo = 0.0, hi = 400.0 / longest;   // exp(400) is far beyond 1/g for any g worth the name
    for (int it = 0; it < 100; ++it) {
        const double s = 0.5 * (lo + hi); double f = 0; for (int i = 0; i < count; ++i) f += gainPerTap * std::exp(std::min(s * delaySec[i], 700.0));
        if (f > 1.0) hi = s; else lo = s;
    }
    return db / (8.685889638 * std::max(0.5 * (lo + hi), 1e-9)) + longest;
}
// the same for taps at whole multiples of one unit
inline double taps(double unitSec, const int* mult, int count, double gainPerTap, double db = 80.0) {
    double d[16]; count = std::min(count, 16); for (int i = 0; i < count; ++i) d[i] = mult[i] * unitSec;
    return multi(d, count, gainPerTap, db);
}
}  // namespace sw::tail
