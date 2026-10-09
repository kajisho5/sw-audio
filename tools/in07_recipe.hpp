// SWINGBY (SW IN07) tools: a recipe in the factory tables' language (products/in07/preset_dsl.hpp) as plain values, as the factory table
// is built (products/in07/presets.cpp), and the factory presets' levels put on them. Shared by tools/in07_pack.cpp and tools/in07_song.cpp.
#pragma once
#include "in07/presets.hpp"
#include "in07/preset_dsl.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace sw::in07::recipe {
using dsl::Raw;
using dsl::V;

// ---- a recipe as plain values (as the factory table is built: products/in07/presets.cpp)
inline bool plainOf(const Raw& raw, std::vector<double>& plain) {
    const auto& s = specs();
    std::map<std::string, int> ids;
    for (int i = 0; i < kNumParams; ++i) ids[s[static_cast<size_t>(i)].id] = i;
    plain.assign(static_cast<size_t>(kNumParams), 0.0);
    for (int i = 0; i < kNumParams; ++i) plain[static_cast<size_t>(i)] = s[static_cast<size_t>(i)].def;
    bool ok = true;
    std::vector<int> seen;
    for (const V& e : raw.b.v) {
        const std::string id = e.layer > 0 ? "in07.l" + std::to_string(e.layer) + "." + e.id : std::string("in07.") + e.id;
        const auto it = ids.find(id);
        if (it == ids.end()) { std::fprintf(stderr, "%s: no parameter %s\n", raw.name, id.c_str()); ok = false; continue; }
        const sw::ParamSpec& sp = s[static_cast<size_t>(it->second)];
        double v = e.v;
        if (!e.label.empty()) {
            const auto f = std::find(sp.labels.begin(), sp.labels.end(), std::string(e.label));
            if (f == sp.labels.end()) { std::fprintf(stderr, "%s: %s has no step \"%s\"\n", raw.name, id.c_str(), std::string(e.label).c_str()); ok = false; continue; }
            v = sp.steps[static_cast<size_t>(f - sp.labels.begin())];
        } else if (sp.curve == sw::Curve::Step) {
            if (std::find(sp.steps.begin(), sp.steps.end(), v) == sp.steps.end()) { std::fprintf(stderr, "%s: %s has no step %g\n", raw.name, id.c_str(), v); ok = false; continue; }
        } else if (v < sp.min || v > sp.max) { std::fprintf(stderr, "%s: %s = %g out of range\n", raw.name, id.c_str(), v); ok = false; continue; }
        if (std::find(seen.begin(), seen.end(), it->second) != seen.end()) { std::fprintf(stderr, "%s: %s twice\n", raw.name, id.c_str()); ok = false; continue; }
        seen.push_back(it->second);
        plain[static_cast<size_t>(it->second)] = v;
    }
    plain[static_cast<size_t>(PresetSelect)] = s[static_cast<size_t>(PresetSelect)].def;
    return ok;
}
inline void apply(Processor& p, const std::vector<double>& plain) { for (int i = 0; i < kNumParams; ++i) if (i != PresetSelect) p.setParam(i, plain[static_cast<size_t>(i)]); }
// the levels on the plain values (as presetValues() puts a factory preset's trim, level and boost)
inline std::vector<double> levelled(std::vector<double> v, const PresetLevels& lv) {
    v[static_cast<size_t>(Level)] = std::clamp(lv.level, -40.0, 0.0);
    v[static_cast<size_t>(FxLimitGain)] = std::clamp(v[static_cast<size_t>(FxLimitGain)] + std::max(0.0, lv.boost), 0.0, 12.0);
    for (int l = 0; l < kLayers; ++l)
        if (v[static_cast<size_t>(lp(l, On))] > 0.5) v[static_cast<size_t>(lp(l, LayerLevel))] = std::clamp(v[static_cast<size_t>(lp(l, LayerLevel))] + lv.trim, -60.0, 6.0);
    return v;
}

}  // namespace sw::in07::recipe
