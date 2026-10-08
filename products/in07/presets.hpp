// SWINGBY (SW IN07) — factory presets. A preset names the parameters it changes (by id string, so the table survives a reordering of the host
//   numbers); applying one sets every parameter to its default first, then the preset's values, then its levels. The levels are measured,
//   not set by ear: tools/in07_presets.cpp plays each preset's audition phrase (by category), measures BS.1770 integrated loudness and writes
//   preset_levels.inc, in two stages, as a mixing desk is set up:
//     staging: a trim on every layer level that is on puts the voices' sum, before the effects, at kPresetStageLufs (so a Drive, a delay's
//              feedback or the limiter works on the level the preset was designed for; trimming after the design instead left a Drive 20 dB
//              short of its input and doing nothing);
//     output:  after the effects, to kPresetTargetLufs: down with the master Level (it sits after the limiter, so it may only turn down), or,
//              when it has to come up, up with the limiter's own gain (the limiter keeps the peaks under its ceiling).
//   tests/test_in07_presets.cpp checks the result (and the categories' conventions) on every build.
#pragma once
#include "in07/in07.hpp"
#include "sw/preset_file.hpp"
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sw::in07 {

constexpr double kPresetTargetLufs = -16.0;   // every factory preset, measured on its category's audition phrase
constexpr double kPresetStageLufs = -18.0;    // the voices' sum before the effects, on the same phrase

struct Preset {
    std::string name, category;                  // categories: LEAD, PAD, SEQ, BASS, PLUCK, KEYS, FX
    std::vector<std::pair<int, double>> values;  // host ids and plain values; everything else stays at its default
    double trim = 0.0;                           // staging: dB added to every layer level that is on
    double level = 0.0;                          // output: in07.level (dB, 0 or less)
    double boost = 0.0;                          // output: dB added to the limiter's gain (0 or more; used only when the level is 0)
};
struct PresetLevels { double trim = 0.0, level = 0.0, boost = 0.0; };
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
// lv = null: the preset's own levels. preFx: every effect off and Level 0 dB (the voices' sum, for the staging)
PresetMeasure measurePreset(int index, double fs = 48000.0, const PresetLevels* lv = nullptr, bool preFx = false);
void applyPreset(Processor& p, int index, const PresetLevels& lv);                 // with other levels (the levelling tool)

// the same as plain values for every parameter (index = host id; the selector at 0): what applyPreset sets, for the plug-in layer,
// which loads a preset on the main thread through its host values. An index out of range gives Init.
void presetValues(int index, std::vector<double>& plain);
void presetValues(int index, const PresetLevels& lv, std::vector<double>& plain);

// user presets (sw/preset_file.hpp, product "in07"): every parameter but the selector; the category is one of the factory categories or
// empty. Reading starts from Init and applies the file's values; a file that is not an IN07 preset changes nothing.
constexpr const char* kPresetProduct = "in07";
bool isPresetCategory(std::string_view c);
std::string userPresetText(const std::vector<double>& plain, const presetfile::Meta& meta);
std::string userPresetText(const Processor& p, const presetfile::Meta& meta);
bool userPresetValues(std::string_view text, std::vector<double>& plain, presetfile::Meta& meta, std::string& error);
bool loadUserPreset(Processor& p, std::string_view text, presetfile::Meta& meta, std::string& error);

}  // namespace sw::in07
