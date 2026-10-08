// SWINGBY (SW IN07) — factory presets. A preset names the parameters it changes (by id string, so the table survives a reordering of the host
//   numbers); applying one sets every parameter to its default first, then the preset's values, then its trim. The loudness is measured, not
//   set by ear: tools/in07_presets.cpp plays each preset's audition phrase (by category), measures BS.1770 integrated loudness and writes
//   preset_trims.inc so that every preset plays at kPresetTargetLufs. The trim moves every layer level together (before the effects), and
//   the master Level stays at kPresetLevel: the Level sits after the limiter, so levelling with it would push the peaks past the ceiling.
//   tests/test_in07_presets.cpp checks the result (and the categories' conventions) on every build.
#pragma once
#include "in07/in07.hpp"
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sw::in07 {

constexpr double kPresetTargetLufs = -16.0;   // every factory preset, measured on its category's audition phrase
constexpr double kPresetLevel = 0.0;          // in07.level in every factory preset (dB)

struct Preset {
    std::string name, category;                  // categories: LEAD, PAD, SEQ, BASS, PLUCK, KEYS, FX
    std::vector<std::pair<int, double>> values;  // host ids and plain values; everything else stays at its default
    double trim = 0.0;                           // dB added to every layer level that is on (from preset_trims.inc)
};
const std::vector<Preset>& factoryPresets();      // the PLAY screen's ten first, in its order
const std::vector<std::string>& presetNames();    // their names (for the selector's labels; does not need the parameter table)
const std::vector<std::string>& presetErrors();   // ids, labels or values in the table that did not resolve (empty when it is right)
void applyPreset(Processor& p, int index);       // every parameter but the selector: defaults, then the preset's values, then its trim
void applyInit(Processor& p);                     // every parameter but the selector back to its default

// the audition phrase of a category: (start s, length s, key); velocity 0.8, tempo 120
struct AuditionNote { double start, length; int key; };
std::vector<AuditionNote> audition(const std::string& category, double& total);
struct PresetMeasure {
    double lufs = -200.0, monoLufs = -200.0;     // integrated loudness, stereo and the mono fold-down (L+R)/2 on both channels
    double peakDb = -200.0, rawPeakDb = -200.0;  // sample peak out, and into the limiter
};
PresetMeasure measurePreset(int index, double fs = 48000.0, double trim = NAN);   // NAN = the preset's own trim
void applyPreset(Processor& p, int index, double trim);                           // with another trim (the levelling tool)

}  // namespace sw::in07
