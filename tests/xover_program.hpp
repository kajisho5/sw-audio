// shared by the tests of the crossover finder and of DY10 Auto: programs made of clusters of sines with a given share of the K-weighted energy
#pragma once
#include "sw/loudness.hpp"
#include "tu.hpp"
#include <complex>
#include <random>
#include <vector>
namespace xoverprog {
using namespace tu;
inline double kWeightPower(double f) {   // the K-weighting of BS.1770 as a power ratio, the same curve the finder uses (from the loudness meters' biquads)
    static sw::KWeighting k; static bool init = false; if (!init) { k.setup(kFs); init = true; }
    auto pw = [](const sw::Biquad& q, double w) { const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w); return std::norm((q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2)); };
    const double w = 2.0 * kPi * f / kFs; return pw(k.shelf(), w) * pw(k.highpass(), w);
}
struct Cluster { double lo, hi, energy; };
// clusters of equally strong sines (a grid of 1/12 octave, random phases) with the given share of the K-weighted energy, over a faint noise floor
inline std::vector<float> clusters(const std::vector<Cluster>& cl, double seconds, unsigned seed = 1) {
    const size_t n = static_cast<size_t>(seconds * kFs); std::vector<float> x = noise(-95.0, seconds, seed);
    std::mt19937 g(seed); std::uniform_real_distribution<double> ph(0.0, 2.0 * kPi);
    for (const auto& c : cl) {
        std::vector<double> fr; for (double f = c.lo; f <= c.hi; f *= std::pow(2.0, 1.0 / 12.0)) fr.push_back(f);
        double w = 0; for (double f : fr) w += kWeightPower(f);
        const double a = std::sqrt(2.0 * c.energy * 0.01 / w);   // total weighted power = energy * 0.01 (about -20 dBFS in all)
        for (double f : fr) { const double p = ph(g); for (size_t i = 0; i < n; ++i) x[i] += static_cast<float>(a * std::sin(2.0 * kPi * f * static_cast<double>(i) / kFs + p)); }
    }
    return x;
}
}  // namespace xoverprog
