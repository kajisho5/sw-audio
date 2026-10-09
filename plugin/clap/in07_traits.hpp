// SW IN07 SWINGBY — the instrument's traits for the CLAP adapter (plugin/clap/instrument_adapter.hpp): the synth, its parameter table, the
// preset selector, the preset files and the plug-in window (ui/in07, embedded by tools/embed_in07_ui.py). In a header of its own so the
// unit tests drive the same plug-in the hosts load (tests/test_in07_window.cpp).
#pragma once
#include "instrument_adapter.hpp"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include "in07_ui_assets.hpp"
#include <cctype>
#include <string>

namespace sw::in07clap {
struct In07 {
    using Core = sw::in07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::in07::specs(); }
    // the preset selector: step 0 = Init, then the factory presets
    static constexpr int kProgramParam = sw::in07::PresetSelect;
    static void loadProgram(Core& c, int step) { if (step <= 0) sw::in07::applyInit(c); else sw::in07::applyPreset(c, step - 1); }   // no allocation (after warmUp)
    static void warmUp() { (void)sw::in07::factoryPresets(); (void)sw::in07::presetPlain(0); }
    // preset files: the host's preset browser lists the factory presets and the user's (CLAP preset-discovery), and loads them (preset-load)
    static constexpr const char* kPresetVendor = "SEVENTHWELL";
    static constexpr const char* kPresetProduct = "SWINGBY";
    static int factoryPresetCount() { return static_cast<int>(sw::in07::factoryPresets().size()); }
    static const std::string& factoryPresetName(int i) { return sw::in07::factoryPresets()[static_cast<size_t>(i)].name; }
    static const std::string& factoryPresetCategory(int i) { return sw::in07::factoryPresets()[static_cast<size_t>(i)].category; }
    static void factoryPresetValues(int i, std::vector<double>& plain) { sw::in07::presetValues(i, plain); }   // out of range: Init
    static bool userPresetValues(std::string_view text, std::vector<double>& plain, sw::presetfile::Meta& meta, std::string& error) {
        return sw::in07::userPresetValues(text, plain, meta, error);
    }
    static std::string userPresetText(const std::vector<double>& plain, const sw::presetfile::Meta& meta) { return sw::in07::userPresetText(plain, meta); }
    // MIDI learn: the eight macros
    static constexpr int kMacroCount = 8;
    static int macroParam(int m) { return sw::in07::Macro1 + m; }
    // where a parameter sits in a host's generic list: "Layer 1/Filter", "Effects/Delay", "Mod matrix/Slot 3" ...
    static std::string paramModule(int id) {
        const std::string s = specs()[static_cast<size_t>(id)].id;   // in07.<group>.<...>
        auto part = [&](int k) { size_t a = 0; for (int i = 0; i < k; ++i) { a = s.find('.', a); if (a == std::string::npos) return std::string(); ++a; } const size_t b = s.find('.', a); return s.substr(a, b == std::string::npos ? std::string::npos : b - a); };
        const std::string g = part(1), h = part(2);
        auto cap = [](std::string t) { if (!t.empty()) t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0]))); return t; };
        if (g.size() == 2 && g[0] == 'l' && g[1] >= '1' && g[1] <= '4') {
            const std::string layer = std::string("Layer ") + g[1];
            if (h == "osc" || h == "wt" || h == "fm" || h == "smp") return layer + "/Oscillator";
            if (h == "flt") return layer + "/Filter";
            if (h == "amp") return layer + "/Amp envelope";
            if (h == "fenv") return layer + "/Filter envelope";
            return layer;
        }
        if (g == "fx") return h.rfind("slot", 0) == 0 ? "Effects/Order" : "Effects/" + (h == "eq" ? std::string("EQ") : cap(h));
        if (g.rfind("lfo", 0) == 0) return "LFO " + g.substr(3);
        if (g.rfind("macro", 0) == 0) return "Macros";
        if (g.rfind("mod", 0) == 0 && g.size() > 3 && std::isdigit(static_cast<unsigned char>(g[3]))) return "Mod matrix/Slot " + g.substr(3);
        if (g == "flyby") return "Flyby";
        if (g == "arp") return "Arpeggiator";
        if (g == "gate") return "Trance gate";
        if (g == "preset") return "Preset";
        return "Voice";
    }
    // the plug-in window: 1280 x 860 at 100 % (the design, docs/design/in07)
    static const sw::instgui::Ui& ui() {
        static const sw::instgui::Ui u{sw::in07ui::kCss, sw::in07ui::kCssSize, sw::in07ui::kJs, sw::in07ui::kJsSize, sw::in07ui::kAssets, sw::in07ui::kAssetCount, 1280, 860};
        return u;
    }
    // the licence server's address for activation by key from the window (server/license; the owner deploys it). Empty: the window offers
    // the licence file only (the key is turned into a file on the server's web page).
    static constexpr const char* kActivationServer = "";
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.in07", "SW IN07 SWINGBY", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.14.1", "Lightweight preset synth: four layers, orbits of sound", f};
        return &d;
    }
};
}  // namespace sw::in07clap
