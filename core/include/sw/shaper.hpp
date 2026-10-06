// SW AUDIO core — biased tanh waveshaper at 2x oversampling (the building block of DriveStage, tube / transformer / saturator models)
//   y = h s (tanh(g x / h + b) - tanh b) / g,  s = 1 / sech^2(b)     small-signal gain 1 for any g and b; saturates at about h / g
// b != 0 gives even harmonics; the DC shift this makes (output minus input) is high-passed at 5 Hz inside the 2x loop.
#pragma once
#include "sw/oversample.hpp"
#include <array>
#include <cmath>

namespace sw {

class BiasShaper2x {
public:
    void prepare(double fs) { os_ = {}; dc_ = {}; dcA_ = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / (2.0 * fs)); }
    void reset() { os_ = {}; dc_ = {}; }
    // g: linear drive gain, b: bias, h: headroom (2.0 = +6 dBFS); channel 0 or 1
    double process(int ch, double x, double g, double b, double h = 2.0) {
        const size_t c = static_cast<size_t>(ch);
        double up[2];
        os_[c].up(x, up);
        const double tb = std::tanh(b), s = 1.0 / (1.0 - tb * tb);
        for (double& u : up) {
            const double y = h * s * (std::tanh(g * u / h + b) - tb) / g;
            dc_[c] = dcA_ * dc_[c] + (1.0 - dcA_) * (y - u);
            u = u + (y - u) - dc_[c];
        }
        return os_[c].down(up);
    }

private:
    std::array<Oversampler2x, 2> os_{};
    std::array<double, 2> dc_{};
    double dcA_ = 0;
};

}  // namespace sw
