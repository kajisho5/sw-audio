// SW UT01 Gain — a gain and channel utility (spec: 仕様書 v1.0「UT01 Gain」). Reported delay 0; no Auto gain (it would cancel the Gain: the spec says to drop it), no Delta.
//   Order per sample: polarity (Ø L / Ø R) -> Swap -> Width (mid/side: side x Width/100) -> Mono ((L + R) / 2 in both) -> Balance (linear: the quieter side is cut, 0 dB in the middle) -> Gain.
//   Channel (design): Both / L only / R only says which channel gets the Gain (the other passes unchanged). Gain, Balance and Width are smoothed over 10 ms; at the defaults the signal is bit-identical.
//   EVO (class A, the spec): a Gain per kind of track. setTrackName() classifies the name the host gives (Vocal, Drums, Bass, Guitar, Keys, Bus, Other: keywords in English and Japanese);
//   rememberGain() keeps the Gain for that kind (saved with the project and shared by the state of the instance); suggestedGainDb() is what was kept last for the kind of the current track. Receiving the track name
//   from the host (CLAP track-info, VST3 track information) is the plug-in layer's job: not yet.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::ut01 {

enum ParamId { Gain, Balance, Width, PhaseL, PhaseR, Swap, Mono, Channel, kNumParams };
enum ChannelId { Both = 0, LeftOnly = 1, RightOnly = 2 };
enum TrackKind { Vocal, Drums, Bass, Guitar, Keys, Bus, Other, kKinds };

const std::vector<ParamSpec>& specs();
int classifyTrack(const std::string& name);

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() { if (prepared_) { g_ = target_[Gain]; b_ = target_[Balance]; w_ = target_[Width]; } }
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void setTrackName(const std::string& name) { kind_ = classifyTrack(name); }
    int trackKind() const { return kind_; }
    void rememberGain() { kept_[static_cast<size_t>(kind_)] = target_[Gain]; has_[static_cast<size_t>(kind_)] = true; }
    bool suggestedGainDb(double& db) const { if (!has_[static_cast<size_t>(kind_)]) return false; db = kept_[static_cast<size_t>(kind_)]; return true; }
    void saveExtra(std::vector<uint8_t>& out) const;
    void loadExtra(const uint8_t* data, size_t size);

private:
    double fs_ = 48000.0, g_ = 0, b_ = 0, w_ = 100;
    int kind_ = Other;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::array<double, kKinds> kept_{};
    std::array<bool, kKinds> has_{};
};

}  // namespace sw::ut01
