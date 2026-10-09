// shared by the tests of the bleed learner and of the products that use it
#pragma once
#include "sw/svf.hpp"
#include "tu.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
namespace drumtest {
using tu::kFs;
// a mono "drum mic": the wanted hits (a snare: noise around 2.5 kHz, 40 ms decay) every 0.6 s at `hitDb` peak, and bleed (a hi-hat: noise over 8 kHz, 15 ms decay) every 0.3 s at `bleedDb` peak, in between
inline std::vector<float> drums(double seconds, double hitDb, double bleedDb, double hitHz = 2500.0, double bleedHz = 8000.0, bool highBleed = true, unsigned seed = 1) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> x(n, 0.0f); std::mt19937 g(seed); std::uniform_real_distribution<double> u(-1.0, 1.0);
    auto burst = [&](double t0, double peakDb, double fc, bool highpass, double tau) {
        sw::Svf f; f.setup(highpass ? sw::Svf::Mode::HighPass : sw::Svf::Mode::BandPass, fc, kFs, highpass ? 0.7071 : 1.0, 0.0);
        const size_t s0 = static_cast<size_t>(t0 * kFs), len = static_cast<size_t>(6.0 * tau * kFs);
        std::vector<double> y(len); double pk = 1e-9;
        for (size_t i = 0; i < len && s0 + i < n; ++i) { y[i] = f.process(u(g)) * std::exp(-static_cast<double>(i) / (tau * kFs)); pk = std::max(pk, std::abs(y[i])); }
        const double a = std::pow(10.0, peakDb / 20.0) / pk;
        for (size_t i = 0; i < len && s0 + i < n; ++i) x[s0 + i] += static_cast<float>(a * y[i]);
    };
    for (double t = 0.5; t < seconds - 0.3; t += 0.6) burst(t, hitDb, hitHz, false, 0.04);
    for (double t = 0.2; t < seconds - 0.3; t += 0.6) burst(t, bleedDb, bleedHz, highBleed, 0.015);
    for (double t = 0.35; t < seconds - 0.3; t += 0.6) burst(t, bleedDb - 3.0, bleedHz, highBleed, 0.015);
    return x;
}
}  // namespace drumtest
