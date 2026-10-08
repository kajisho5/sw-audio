// SW IN07 SWINGBY — CLAP instrument traits (VST3 / AUv2 through clap-wrapper)
#include "instrument_adapter.hpp"
#include "in07/in07.hpp"
#include "in07/presets.hpp"

namespace {
struct In07 {
    using Core = sw::in07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::in07::specs(); }
    // the preset selector: step 0 = Init, then the factory presets
    static constexpr int kProgramParam = sw::in07::PresetSelect;
    static void loadProgram(Core& c, int step) { if (step <= 0) sw::in07::applyInit(c); else sw::in07::applyPreset(c, step - 1); }
    static void warmUp() { (void)sw::in07::factoryPresets(); }
    // preset files: the host's preset browser lists the factory presets and the user's (CLAP preset-discovery), and loads them (preset-load)
    static constexpr const char* kPresetVendor = "SEVENTHWELL";
    static constexpr const char* kPresetProduct = "SWINGBY";
    static int factoryPresetCount() { return static_cast<int>(sw::in07::factoryPresets().size()); }
    static const std::string& factoryPresetName(int i) { return sw::in07::factoryPresets()[static_cast<size_t>(i)].name; }
    static const std::string& factoryPresetCategory(int i) { return sw::in07::factoryPresets()[static_cast<size_t>(i)].category; }
    static void factoryPresetValues(int i, std::vector<double>& plain) { sw::in07::presetValues(i, plain); }
    static bool userPresetValues(std::string_view text, std::vector<double>& plain, sw::presetfile::Meta& meta, std::string& error) {
        return sw::in07::userPresetValues(text, plain, meta, error);
    }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.in07", "SW IN07 SWINGBY", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Lightweight preset synth: four layers, orbits of sound", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_INSTRUMENT_ENTRY(in07, In07)
