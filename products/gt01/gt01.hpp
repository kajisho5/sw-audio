// SW GT01 Amp — guitar amplifier head, no cabinet (spec: 仕様書 v1.0「GT01 Amp」)
//   in -> [Volume match: fixed gain measured over the first 5 s of playing] -> Bright (treble shelf that fades as Gain rises)
//      -> 4x OS: triode 1 -> coupling high-pass -> triode 2 (Crunch / Lead; Clean uses a unity second stage) -> down
//      -> passive three-knob tone stack (analog transfer function from the circuit, bilinear transform) + recovery gain
//      -> 4x OS: power stage with supply sag -> Presence shelf -> output transformer low/high roll-off -> out.
// Each triode is a biased tanh (even harmonics); the level into it decides how much it distorts, so turning the guitar down cleans the sound up.
#pragma once
#include "sw/oversample.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::gt01 {

enum ParamId { Channel, Gain, Bass, Middle, Treble, Presence, Master, Bright, VolumeMatch, kNumParams };
enum ChannelId { Clean = 0, Crunch = 1, Lead = 2 };

const std::vector<ParamSpec>& specs();

// the passive tone stack: B(s) = b1 s + b2 s^2 + b3 s^3, A(s) = 1 + a1 s + a2 s^2 + a3 s^3 (t, m, l = pot positions 0..1)
struct ToneStackPoly { double b[4], a[4]; };
ToneStackPoly toneStackAnalog(double t, double m, double l);
double toneStackAnalogDb(double f, double t, double m, double l);   // |H(j 2 pi f)| in dB
double bassPot(double knob10);                                      // audio-taper bass pot position for a 0..10 knob

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double toneResponseDb(double f) const;   // digital response of the tone stack as currently set
    double matchGainDb() const { return matchDb_; }   // Volume match: gain found so far (0 until measured)
    bool matchDone() const { return matchDone_; }

private:
    struct Os4 {
        Oversampler2x a, b;
        void up(double x, double out[4]) { double u[2]; a.up(x, u); b.up(u[0], out); b.up(u[1], out + 2); }
        double down(const double in[4]) { const double u[2] = {b.down(in), b.down(in + 2)}; return a.down(u); }
    };
    struct Ch {
        Os4 pre, pow;
        double hp1 = 0, lp1 = 0, hp2 = 0, lp2 = 0, dc1 = 0, dc2 = 0, sag = 0, pdc = 0;
        double tz[3] = {0, 0, 0};              // tone stack, transposed direct form II
        Svf bright, presence, tHp, tLp, tLp2;  // bright shelf, presence shelf, output transformer
    };
    void updateControl();
    void updateTone();
    void updateFixed();
    double fs_ = 48000.0, matchDb_ = 0.0, accSq_ = 0.0, accT_ = 0.0, inGain_ = 1.0, recovery_ = 1.0;
    bool matchDone_ = false, prepared_ = false;
    int ctl_ = 0, blockN_ = 0;
    double blockSq_ = 0;
    std::array<double, kNumParams> target_{};
    LinearSmoother gain_, bass_, mid_, tre_, master_, match_;
    double tb_[4] = {0, 0, 0, 0}, ta_[4] = {1, 0, 0, 0};   // digital tone stack coefficients (a0 = 1)
    std::array<Ch, 2> c_{};
};

}  // namespace sw::gt01
