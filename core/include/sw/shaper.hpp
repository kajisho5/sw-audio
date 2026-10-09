// SW AUDIO core — biased tanh waveshaper at 1x / 2x / 4x oversampling (default 2x; the building block of DriveStage, tube / transformer / saturator models)
//   y = h s (tanh(g x / h + b) - tanh b) / g,  s = 1 / sech^2(b)     small-signal gain 1 for any g and b; saturates at about h / g
// b != 0 gives even harmonics; the DC shift this makes (output minus input) is high-passed at 5 Hz inside the oversampled loop (its coefficient follows the setting).
// setOversample(1 | 2 | 4): the common oversampling setting (spec 共通機能). BiasShaper4x is the same with 4x as its default (where the spec recommends 4x).
#pragma once
#include "sw/oversample.hpp"
#include <array>
#include <cmath>

namespace sw {

class BiasShaper {
public:
    explicit BiasShaper(int oversample = 2) { for (auto& o : os_) o.setFactor(oversample); }
    void prepare(double fs) { fs_ = fs; reset(); setOversample(os_[0].factor()); }
    void reset() { for (auto& o : os_) o.reset(); dc_ = {}; }
    void setOversample(int factor) {
        for (auto& o : os_) o.setFactor(factor);
        if (fs_ > 0.0) dcA_ = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / (os_[0].factor() * fs_));
    }
    int oversample() const { return os_[0].factor(); }
    // g: linear drive gain, b: bias, h: headroom (2.0 = +6 dBFS); channel 0 or 1
    double process(int ch, double x, double g, double b, double h = 2.0) {
        const size_t c = static_cast<size_t>(ch);
        const double tb = std::tanh(b), s = 1.0 / (1.0 - tb * tb);
        return os_[c].process(x, [&](double u) {
            const double y = h * s * (std::tanh(g * u / h + b) - tb) / g;
            dc_[c] = dcA_ * dc_[c] + (1.0 - dcA_) * (y - u);
            return u + (y - u) - dc_[c];
        });
    }

private:
    std::array<OsSwitch, 2> os_{};
    std::array<double, 2> dc_{};
    double fs_ = 0, dcA_ = 0;
};

class BiasShaper2x : public BiasShaper {
public:
    BiasShaper2x() : BiasShaper(2) {}
};
class BiasShaper4x : public BiasShaper {
public:
    BiasShaper4x() : BiasShaper(4) {}
};

}  // namespace sw
