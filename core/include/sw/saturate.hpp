// SW AUDIO core — symmetric soft clipper with small-signal unity gain (spec: Drive の音量補正つき)
#pragma once
#include <cmath>

namespace sw {

class Saturator {
public:
    void setDriveDb(double db) { g_ = std::pow(10.0, db / 20.0); }
    // h tanh(g x / h)/g : unity gain for small signals, saturates at h/g (h = headroom, default 1 = 0 dBFS)
    void setHeadroom(double h) { h_ = h; }
    double process(double x) const { return h_ * std::tanh(g_ * x / h_) / g_; }
    // the same with the drive gain scaled by k (Unit A / B / C: where the saturation sets in, per channel; the small-signal gain stays 1)
    double process(double x, double k) const { const double g = g_ * k; return h_ * std::tanh(g * x / h_) / g; }

private:
    double g_ = 1.0, h_ = 1.0;
};

}  // namespace sw
