// SW RV07 Early — early reflections only: "where in the room" (spec: 仕様書 v1.0「RV07 Early」). No Mix (the spec assumes 100 %); the direct sound is NOT delayed.
//   A shoebox room (Room size, grown if the source would fall outside it), the listener at (0.3, 0.5) of its floor plan, 1.6 m up; the source Distance away, Angle to the right
//   (+) or left (-) of straight ahead, 1.5 m up. Image sources up to the 2nd order (25 with the direct sound): level = Wall's reflectivity^order x Use's gain / path length, arrival
//   = (path - direct path) / 343 m/s after the direct sound, panned by the azimuth of the image. Wall filters the reflections (low-pass, glass also high-pass); air absorption
//   (low-pass 18 kHz / (1 + distance / 6 m)) acts on the direct sound and on the reflections.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include <array>
#include <vector>

namespace sw::rv07 {

enum ParamId { Use, Distance, Angle, RoomSize, Wall, kNumParams };
enum UseId { Dialog = 0, Instrument = 1, Foley = 2 };
enum WallId { Wood = 0, Concrete = 1, Glass = 2, Curtain = 3 };

const std::vector<ParamSpec>& specs();

struct Tap { double delaySeconds, gain, pan; int order; };
struct RoomDims { double lx, ly, lz; };
RoomDims roomDims(int roomSize, double distance, double angleDeg);   // the room as used: grown when the source is farther than the room allows
std::vector<Tap> earlyTaps(int roomSize, double distance, double angleDeg);   // [0] is the direct sound (delay 0, order 0), then 24 images
double wallReflectivity(int wall);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }

private:
    void update();
    double fs_ = 48000.0, airD_[2] = {0, 0}, airE_[2] = {0, 0}, cD_ = 0.5, cE_ = 0.5, cW_ = 0.5, wallLp_[2] = {0, 0}, dirL_ = 0.7, dirR_ = 0.7, useGain_ = 1.0;
    size_t pos_ = 0;
    int nTaps_ = 0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<double, 40> delay_{}, cur_{}, gl_{}, gr_{};
    std::vector<float> buf_;
    std::array<Svf, 2> hp_{}, glassHp_{};
};

}  // namespace sw::rv07
