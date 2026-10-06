// SW ST05 Phones — loudspeakers in a room, heard on headphones (spec: 仕様書 v1.0「ST05 Phones」). The whole signal is processed (no Mix: monitoring only); reported delay 0.
//   Four impulse responses (left / right speaker -> left / right ear, up to 0.5 s) are convolved with the stereo input (sw::TieredConvolver: a 128-tap direct head, so no latency):
//     out_left = L * h_LL + R * h_RL, out_right = L * h_LR + R * h_RR.
//   No recorded impulse responses or HRTFs are used (the spec says they must be recorded in-house or licensed): every IR is built from a model, in designIr():
//     - the speakers sit at +-Angle from straight ahead at a distance D (Nearfield 1.2 m, Mains 3 m, Car 0.8 m), in a shoebox room (Room: Studio A / B / Living; Car has its own cabin) with the
//       listener at 0.45 of its length; the head is a sphere (Head size: radius 7.5 / 8.75 / 10 cm).
//     - direct sound and image sources up to the 2nd order: arrival = distance / 343 m/s, the interaural time difference by Woodworth (a / c)(theta + sin theta) split between the ears, 1 / distance,
//       wall reflectivity sqrt(1 - alpha) from the Sabine absorption (alpha = 0.161 V / (S RT60)); each arrival through the head-shadow filter of the Brown-Duda model
//       H(w) = (1 + alpha(phi) j w / 2 w0) / (1 + j w / 2 w0), w0 = c / a, alpha(phi) = 1.05 + 0.95 cos(1.2 phi) (phi: angle between the arrival and the ear's axis).
//     - a diffuse tail from 30 ms, independent noise per speaker and ear, exp(-6.9 t / RT60) with the low-pass moving down from 10 kHz, the energy fixed by the critical distance 0.057 sqrt(V / RT60).
//     - the speaker's own response: a 2nd-order high-pass (Nearfield 60 Hz, Mains 35 Hz, Car 80 Hz) and, in the Car, a +5 dB low shelf at 100 Hz (cabin gain).
//   Phones profile: generic curves for the kind of headphone (Closed / Open / Earbud), not for models: design values after the convolution. Off = flat.
//   Tracking (On): the head's yaw from setHeadYaw() (a head tracker is not connected yet; 0 until then) turns the head in the model; the IRs are rebuilt when it moves by 1.5 degrees.
//   A change of Speakers / Room / Angle / Head size (or the yaw) builds four new IRs step by step in the audio thread (one small job per call) and crossfades to them over 20 ms.
#pragma once
#include "sw/param.hpp"
#include "sw/svf.hpp"
#include "sw/tiered_convolver.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace sw::st05 {

enum ParamId { Speakers, Room, Angle, HeadSize, Tracking, PhonesProfile, kNumParams };
enum SpeakersId { Nearfield = 0, Mains = 1, Car = 2 };
enum RoomId { StudioA = 0, StudioB = 1, Living = 2 };
enum HeadId { Small = 0, Medium = 1, Large = 2 };
enum ProfileId { ProfOff = 0, Closed = 1, Open = 2, Earbud = 3 };

const std::vector<ParamSpec>& specs();

struct IrSpec { int speakers = 0, room = 0, head = 1; double angleDeg = 30.0, yawDeg = 0.0; };
double headRadius(int head);                       // m
double itdSeconds(double lateralDeg, int head);    // Woodworth: (a / c)(theta + sin theta)
double roomRt60(int speakers, int room);           // s (Car: its cabin)
double roomVolume(int speakers, int room);         // m^3
double speakerDistance(int speakers);              // m (before the room shortens it)
constexpr double kIrSeconds = 0.5;
// one impulse response: speaker 0 = left, 1 = right; ear 0 = left, 1 = right
void designIr(double fs, const IrSpec& s, int speaker, int ear, size_t length, std::vector<double>& out);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets();
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void setHeadYaw(double deg) { yawTarget_ = deg; }
    bool busy() const { return stage_ != Idle || differs(wanted()); }   // a new IR set is being built or waiting to be
    double builtAngle() const { return built_.angleDeg; }
    double builtYaw() const { return built_.yawDeg; }

private:
    enum Stage { Idle, Design, Load, Commit };
    bool differs(const IrSpec& w) const { return w.speakers != built_.speakers || w.room != built_.room || w.head != built_.head || w.angleDeg != built_.angleDeg || std::abs(w.yawDeg - built_.yawDeg) >= 1.5; }
    void startJob();
    void stepJob();
    IrSpec wanted() const;
    void setProfile();
    double fs_ = 48000.0, yawTarget_ = 0.0;
    size_t len_ = 24000;
    bool prepared_ = false, dirty_ = false, loaded_ = false, sync_ = false;
    Stage stage_ = Idle; int designIdx_ = 0, commitIdx_ = 0, loadIdx_ = 0;
    IrSpec built_{}, job_{};
    std::array<double, kNumParams> target_{};
    std::array<TieredConvolver, 4> conv_{};     // 0 LL, 1 LR, 2 RL, 3 RR (speaker, ear)
    std::array<std::vector<double>, 4> kernel_;
    std::array<std::vector<float>, 4> tmp_;
    std::array<std::array<Svf, 3>, 2> eq_{};
    int profile_ = -1;
    bool eqOn_ = false;
};

}  // namespace sw::st05
