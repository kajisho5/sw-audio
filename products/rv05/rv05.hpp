// SW RV05 Chamber — echo chamber (spec: 仕様書 v1.0「RV05 Chamber」). Wet signal only (Mix is the shared frame's); no reported delay.
//   A loudspeaker at one end of a shoebox room, a microphone at Mic distance from it. Early reflections: the image sources of the shoebox up to the 2nd order
//   (25 of them, from the room's size and the two positions: delay = path length / 343 m/s, level 1/path x 0.85^order, panned by the side the image is on),
//   including the direct sound (so moving the microphone changes the direct-to-reverberant ratio, the first reflections and the loss of highs together).
//   Late: diffusion all-passes -> 16-line FDN (sw::Fdn) scaled to the room, level set from the room (critical distance). Speaker tilt shapes the signal sent to the speaker.
#pragma once
#include "sw/fdn.hpp"
#include "sw/param.hpp"
#include "sw/smooth.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv05 {

enum ParamId { Room, Decay, MicDistance, Tilt, Tone, Mix, Unit, kNumParams };

const std::vector<ParamSpec>& specs();

struct Tap { double delaySeconds, gain, pan; };
struct RoomDims { double lx, ly, lz; };
RoomDims roomDims(int room);                                  // Small / Medium / Large, metres
double micDistanceMeters(int room, double micPct);            // 0 % = 0.5 m .. 100 % = 0.7 x the room's length
double decaySeconds(double knob);                             // 0..10 -> 0.4 .. 4 s (logarithmic)
std::vector<Tap> earlyReflections(int room, double micPct);   // the direct sound first, then the images up to the 2nd order
void earlyReflections(int room, double micPct, std::vector<Tap>& out);   // the same into a vector the caller keeps (no allocation when it has room for 25)

class Processor {
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
    void updateRoom();
    void updateFilters();
    double fs_ = 48000.0, airLp_[2] = {0, 0}, airC_ = 0.5, lateGain_ = 1.0, lateTrim_ = 1.0, preLateLen_ = 0.0;
    size_t pos_ = 0, latePos_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::vector<Tap> taps_;
    std::array<double, 40> tapDelay_{}, curDelay_{}, tapL_{}, tapR_{};
    int nTaps_ = 0;
    Fdn fdn_;
    std::vector<float> buf_, late_;
    std::array<Ap, 4> ap_{};
    std::array<Svf, 2> shelfLo_{}, shelfHi_{};
};

}  // namespace sw::rv05
