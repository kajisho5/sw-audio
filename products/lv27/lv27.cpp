#include "lv27/lv27.hpp"
#include <algorithm>

namespace sw::lv27 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv27.follow", "Follow scenes", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv27.fade",   "Fade between",  0, 1000, 300, Curve::Lin, 1, {}, "ms"},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }
void Processor::prepare(double sampleRate, int) { fs_ = sampleRate; fade_ = 1.0; prepared_ = true; }
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

int Processor::lookup(const std::string& scene) const { for (const auto& m : map_) if (m.scene == scene) return m.preset; return 0; }

void Processor::onSceneChanged(const std::string& scene) {
    scene_ = scene;
    if (target_[FollowScenes] < 0.5) return;
    const int p = lookup(scene);
    if (p > 0 && p != active_) { previous_ = active_; active_ = p; fade_ = target_[FadeBetween] <= 0.0 ? 1.0 : 0.0; }
}
bool Processor::setMapping(const std::string& scene, int preset) {
    if (scene.empty() || scene.size() > 200 || preset < 1 || preset > kMaxPreset) return false;
    for (auto& m : map_) if (m.scene == scene) { m.preset = preset; return true; }
    if (static_cast<int>(map_.size()) >= kMaxMappings) return false;
    map_.push_back({scene, preset}); return true;
}
bool Processor::learnCurrent(int preset) { return setMapping(scene_, preset); }
bool Processor::removeMapping(const std::string& scene) { const auto it = std::remove_if(map_.begin(), map_.end(), [&](const Mapping& m) { return m.scene == scene; }); const bool had = it != map_.end(); map_.erase(it, map_.end()); return had; }

// layout: count (1 byte), then per mapping: preset (1 byte), length (1 byte), the scene name's bytes
void Processor::saveExtra(std::vector<uint8_t>& out) const {
    out.push_back(static_cast<uint8_t>(map_.size()));
    for (const auto& m : map_) { out.push_back(static_cast<uint8_t>(m.preset)); const size_t len = std::min<size_t>(200, m.scene.size()); out.push_back(static_cast<uint8_t>(len)); out.insert(out.end(), m.scene.begin(), m.scene.begin() + static_cast<long>(len)); }
}
void Processor::loadExtra(const uint8_t* d, size_t size) {
    if (size < 1) return; const size_t count = std::min<size_t>(d[0], kMaxMappings); size_t pos = 1; std::vector<Mapping> v;
    for (size_t i = 0; i < count; ++i) { if (pos + 2 > size) break; const int preset = d[pos]; const size_t len = d[pos + 1]; pos += 2; if (pos + len > size) break; std::string s(reinterpret_cast<const char*>(d + pos), len); pos += len; if (preset >= 1 && preset <= kMaxPreset && !s.empty()) v.push_back({s, preset}); }
    map_ = v;
}

}  // namespace sw::lv27
