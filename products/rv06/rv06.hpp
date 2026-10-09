// SW RV06 Shimmer — a reverb whose tail is shifted up in pitch, again and again (spec: 仕様書 v1.0「RV06 Shimmer」). Wet signal only (Mix is the shared frame's).
//   mid -> 4 diffusion all-passes -> 16-line FDN (sw::Fdn). INSIDE its feedback, 8 of the 16 lines (the even ones) are passed through a pitch shifter and mixed with
//   the unshifted line: s' = s + a (shift(s) - s), a = 0.7 x Shimmer. The shifter is two overlapping grains (delay-line taps 25 ms apart in a 50 ms window, crossfaded with
//   sin^2) reading at 2x (octave) or 1.5x (fifth) speed; Both gives the lines alternately one and the other. Mixing a gain-1 shifter with the line can never raise
//   the amplitude, so the loop cannot run away; every trip round it moves some of the tail up once more. Freeze: the loop gain of the FDN goes to 0.99995 with no damping, the input is closed and the
//   shimmer feedback is stopped (a held pad does not climb). Duck lowers the tail by 6 dB while the source is loud.
#pragma once
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include <array>
#include <vector>

namespace sw::rv06 {

enum ParamId { Decay, Shimmer, Interval, Mix, Freeze, Duck, kNumParams };
enum IntervalId { Octave = 0, Fifth = 1, Both = 2 };

const std::vector<ParamSpec>& specs();

class Processor : public Fdn::LineHook {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    double tailSeconds() const;   // how long it goes on after the input stops (sw/tail.hpp)
    int latencySamples() const { return 0; }

private:
    struct Ap { std::vector<float> buf; size_t pos = 0; double process(double x, double g) { const double d = buf[pos]; const double y = -g * x + d; buf[pos] = static_cast<float>(x + g * y); if (++pos >= buf.size()) pos = 0; return y; } };
    struct Shifter {
        std::vector<float> buf; size_t pos = 0; double d = 0.0, win = 1.0;
        void prepare(double fs) { win = std::round(0.05 * fs); buf.assign(static_cast<size_t>(win) + 8, 0.0f); pos = 0; d = 0.0; }
        double process(double x, double ratio);
    };
    void updateLines();
    double process(int line, double s) override;   // Fdn::LineHook: lines 0, 2, 4 ... 14 are partly pitch shifted
    double fs_ = 48000.0, env_ = 0.0, duckGain_ = 1.0, lateTrim_ = 1.0, shimAmount_ = 0.0, shimTarget_ = 0.0;
    int interval_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    Fdn fdn_;
    std::array<Ap, 4> ap_{};
    std::array<Shifter, 8> shifter_{};
};

}  // namespace sw::rv06
