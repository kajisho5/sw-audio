// SW IN07 SWINGBY — CLAP instrument traits (VST3 / AUv2 through clap-wrapper)
#include "instrument_adapter.hpp"
#include "in07/in07.hpp"

namespace {
struct In07 {
    using Core = sw::in07::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::in07::specs(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.in07", "SW IN07 SWINGBY", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Lightweight preset synth: four layers, orbits of sound", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_INSTRUMENT_ENTRY(in07, In07)
