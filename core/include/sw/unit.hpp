// SW AUDIO core — Unit A / B / C (spec common function, analog enclosures only): a fixed set of component tolerances, a different one on the left and on the right channel.
//   Gain +-0.3 dB, frequency +-3 %, saturation onset +-0.5 dB. A is the reference (no deviation); B and C are two fixed "units": the same numbers in every session on every machine
//   (an integer hash of unit, channel, slot and kind - no random state, nothing is stored). `slot` tells the controls of one product apart (the low shelf, the bell, the high shelf ... are
//   different parts and have different tolerances); a product numbers its own slots from 0.
//   Each value is uniform in +-limit (the limits are the spec's maxima).
#pragma once
#include <cmath>
#include <cstdint>

namespace sw {

struct Unit {
    static constexpr double kGainDb = 0.3, kFreq = 0.03, kSatDb = 0.5;
    enum Kind { Gain = 0, Freq = 1, Sat = 2 };
    static constexpr int kOutputSlot = 1000;   // the gain tolerance of the output stage that the Shell applies to every product's wet signal (the products' own slots are small numbers)

    // unit 0 = A, 1 = B, 2 = C (anything else is clamped); a number in [-1, 1], 0 for A
    static double value(int unit, int ch, int slot, int kind) {
        const uint64_t u = static_cast<uint64_t>(unit < 0 ? 0 : unit > 2 ? 2 : unit);
        if (u == 0) return 0.0;
        uint64_t z = (u << 48) ^ (static_cast<uint64_t>(ch & 1) << 40) ^ (static_cast<uint64_t>(slot & 0xFFFF) << 8) ^ static_cast<uint64_t>(kind & 0xFF);
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31;   // splitmix64
        return static_cast<double>(z >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }
    static double gainDb(int unit, int ch, int slot = 0) { return kGainDb * value(unit, ch, slot, Gain); }           // dB, +-0.3
    static double gainLin(int unit, int ch, int slot = 0) { return std::pow(10.0, gainDb(unit, ch, slot) / 20.0); }
    static double freqMul(int unit, int ch, int slot) { return 1.0 + kFreq * value(unit, ch, slot, Freq); }             // multiplier, 0.97 .. 1.03
    static double satDb(int unit, int ch, int slot = 0) { return kSatDb * value(unit, ch, slot, Sat); }              // dB on the drive: where the saturation sets in, +-0.5
};

}  // namespace sw
