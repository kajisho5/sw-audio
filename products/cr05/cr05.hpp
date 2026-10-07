// SW CR05 Tape Stop — a tape that slows to a stop, speeds up from standstill, or spins back (spec: 仕様書 v1.0「CR05 Tape Stop」). Reported delay 0 (at full speed the live input is read).
//   The input goes into a ring of 8 s; the output reads it with a speed s(t) (so the pitch follows the speed). Progress warp w(u), u = 0 .. 1 over the time: Lin w = u, Exp w = u^2 (the speed holds, then falls fast),
//   Log w = sqrt(u) (falls fast, then lingers). Stop: s = 1 - w, then silence until Trigger goes Off; Start: s = w from standstill, then a 30 ms cross-fade to the live input; Spin back: s = 1 - 3 w (it runs backwards at up
//   to -2 x), then silence. The level follows the speed (min(1, 4 |s|)). Filter On: a low-pass at max(300 Hz, 18 kHz x |s|^1.5). Times: 1/16, 1/8, 1/4, 1/2, 1, 2 bars (default Stop 1/2, Start 1/8).
//   Trigger Off -> On starts the action (automation); Off again ends a held stop with a 20 ms cross-fade to the live input. EVO (class A): with the host transport known, Stop and Spin back start at
//   (next bar line - time) (the following bar when that has gone by) so that they end exactly on the bar line; Start begins at once. Without a transport: at once. A 4/4 bar is assumed; 120 bpm without a tempo.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sw::cr05 {

enum ParamId { Action, StopTime, StartTime, Curve_, Filter, Trigger, kNumParams };
enum ActionId { Stop = 0, Start = 1, SpinBack = 2 };

const std::vector<ParamSpec>& specs();
double barFraction(int index);   // 0: 1/16 ... 5: 2 bars

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void setTempo(double bpm) { bpm_ = bpm; }
    void setTransport(bool playing, double beatsToNextBar);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double speed() const { return s_; }
    bool running() const { return state_ != Idle; }

private:
    enum State { Idle, Waiting, Running, Held, Resume };
    double warp(double u) const;
    double fs_ = 48000.0, bpm_ = 0.0, barBeat_ = 0.0, s_ = 1.0, rp_ = 0.0, live_ = 0.0, u_ = 0.0, durSamples_ = 1.0, waitLeft_ = 0.0;
    int64_t t_ = 0;
    int state_ = Idle, action_ = Stop;
    bool prepared_ = false, hostSeen_ = false, trig_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> ring_;
    std::array<Svf, 2> lp_{};
    size_t mask_ = 0;
};

}  // namespace sw::cr05
