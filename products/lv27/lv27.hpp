// SW LV27 Scene Sync — OBS scenes and presets (spec: 仕様書 v1.0「LV27 Scene Sync」). The sound passes untouched (bit for bit, delay 0, no Auto gain, no Delta).
//   What exists: the table (OBS scene name -> preset slot 1..32 of the SW AUDIO instances) with Learn current, the scene-change handler and the Fade between curve. onSceneChanged(name) (the engine calls it with the program output's scene: from OBS directly
//   in SW AUDIO for OBS, over obs-websocket in the VST3 version) looks the scene up; with Follow scenes On and a hit, activePreset() becomes that slot and fadeProgress() runs 0 -> 1 over Fade between ms (a block-by-block clock in process()).
//   learnCurrent(preset) writes "the current scene -> this preset" into the table (the scene is the last one reported). The table is part of the saved state.
//   **Not here: sending the preset change to the other instances (that is SW Link, which does not exist) and the obs-websocket client with its saved password (the spec's open point).** The result is read by whoever has the link: activePreset(), previousPreset(), fadeProgress().
#pragma once
#include "sw/param.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::lv27 {

enum ParamId { FollowScenes, FadeBetween, kNumParams };
constexpr int kMaxMappings = 64, kMaxPreset = 32;

const std::vector<ParamSpec>& specs();

struct Mapping { std::string scene; int preset; };

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float**, int, int n) { if (prepared_ && fade_ < 1.0) { const double ms = std::max(1.0, target_[FadeBetween]); fade_ = std::min(1.0, fade_ + 1000.0 * n / (fs_ * ms)); } }
    int latencySamples() const { return 0; }
    void onSceneChanged(const std::string& scene);
    bool learnCurrent(int preset);
    bool setMapping(const std::string& scene, int preset);
    bool removeMapping(const std::string& scene);
    int lookup(const std::string& scene) const;   // 0 = none
    const std::vector<Mapping>& mappings() const { return map_; }
    const std::string& currentScene() const { return scene_; }
    int activePreset() const { return active_; }
    int previousPreset() const { return previous_; }
    double fadeProgress() const { return fade_; }
    void saveExtra(std::vector<uint8_t>& out) const;
    void loadExtra(const uint8_t* data, size_t size);

private:
    double fs_ = 48000.0, fade_ = 1.0;
    bool prepared_ = false;
    std::array<double, kNumParams> target_{};
    std::vector<Mapping> map_;
    std::string scene_;
    int active_ = 0, previous_ = 0;
};

}  // namespace sw::lv27
