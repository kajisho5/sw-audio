// SW AUDIO core — morph between two parameter sets (spec 共通章 5「モーフ」)
// Continuous parameters interpolate in the normalized domain, so frequencies move geometrically and
// dB values linearly. Stepped parameters switch at 0.5 (the product's own step crossfade smooths it).
#pragma once
#include "sw/param.hpp"
#include <algorithm>

namespace sw {

inline double morphValue(const ParamSpec& s, double plainA, double plainB, double m) {
    m = std::clamp(m, 0.0, 1.0);
    if (s.curve == Curve::Step) return m < 0.5 ? plainA : plainB;
    if (m <= 0.0) return plainA;
    if (m >= 1.0) return plainB;
    const double a = s.toNorm(plainA), b = s.toNorm(plainB);
    return s.toValue(a + (b - a) * m);
}

}  // namespace sw
