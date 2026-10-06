// SW DL01 Echo — echo with three characters (spec: 仕様書 v1.0「DL01 Echo」). Wet signal only (Mix is the shared frame's); reported delay 0.
//   Per channel: record = filters(limit(in + Feedback * echo)) -> delay line -> echo (4-point Hermite read, time glides). The loop limiter is L * tanh(x / L): gain 1 for small
//   signals, so Feedback up to 110 % rings up and settles instead of running away (L = 8 Digital, 1.6 Analog, 1.0 Tape: Tape and Analog also colour what they record).
//   Loop filters HPF (20 Hz = Off) and LPF (20 kHz = Off), second order. Mode: Tape adds a 9 kHz low-pass (0.12 s time glide, wow 0.3 x Depth x 10 ms and flutter);
//   Analog (BBD) a 5 kHz second-order low-pass (0.06 s glide, 2.5 ms sine); Digital stays flat (0.02 s glide, 1.0 ms sine).
//   Depth (0..100 %) x the mode's peak deviation, Rate (0.1..10 Hz). Ping-pong: the left line takes the input, each line feeds the other (first echo left, second right).
//   Sync: Time (the knob position) picks a note from sw/notes.hpp at the host tempo (without tempo it is Time in ms). Duck: wet reduced by up to Duck dB while the input is loud.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::dl01 {

enum ParamId { Mode, Time, Feedback, Hpf, Lpf, Depth, Rate, Duck, Mix, Sync, PingPong, kNumParams };
enum ModeId { Tape = 0, Analog = 1, Digital = 2 };

const std::vector<ParamSpec>& specs();
constexpr double kMaxSeconds = 4.0;   // longest delay (2 bars at 120 bpm; slower tempos are clamped)

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void setTempo(double bpm) { bpm_ = bpm; }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    double timeSeconds() const;   // the delay in use (a note length when Sync is on and the tempo is known)

private:
    double read(int c, double delay) const;
    double fs_ = 48000.0, bpm_ = 0.0, delay_ = 0.0, env_ = 0.0, duckGain_ = 1.0, ph_ = 0.0, phFl_ = 0.0;
    size_t pos_ = 0, mask_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<std::vector<float>, 2> buf_;
    struct Chan { Svf hp, lp, tone, tone2; double ping = 0.0; };
    std::array<Chan, 2> ch_{};
    std::array<double, 2> echo_{};
    int filtMode_ = -1; double filtHpf_ = 0, filtLpf_ = 0;
    void setFilters();
};

}  // namespace sw::dl01
