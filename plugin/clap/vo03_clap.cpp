// SW VO03 Harmony — CLAP plugin traits
#include "clap_adapter.hpp"
#include "vo03/vo03.hpp"

namespace {
struct Vo03 {
    using Core = sw::vo03::Processor;
    static const std::vector<sw::ParamSpec>& specs() { return sw::vo03::specs(); }
    static constexpr int kOutputParam = -1;
    static constexpr int kInParam = -1;
    static constexpr int kMixParam = -1;
    static constexpr int kReadouts = 7;   // voiced (1 / 0), the singer's pitch, the four voices' pitches (MIDI note numbers), the chord held on the MIDI track (bit k = pitch class k)
    static void readouts(const Core& c, double* o) { o[0] = c.voiced() ? 1.0 : 0.0; o[1] = c.leadSemitones(); for (int v = 0; v < sw::vo03::kVoices; ++v) o[2 + v] = c.voiceSemitones(v); o[6] = c.chordMask(); }
    // MIDI: the notes held on the MIDI track are the chord (Source MIDI); CC123 / CC120 (all notes off) let go of all of them
    static void midi(Core& c, int kind, int, int d1, int) { if (kind == 1) c.noteOn(d1); else if (kind == 0) c.noteOff(d1); else if (kind == 2 && (d1 == 123 || d1 == 120)) c.allNotesOff(); }
    static const clap_plugin_descriptor_t* descriptor() {
        static const char* const f[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_PITCH_SHIFTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
        static const clap_plugin_descriptor_t d = {CLAP_VERSION_INIT, "com.seventh-well.sw-audio.vo03", "SW VO03 Harmony", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "0.13.0", "Harmony voices from one voice", f};
        return &d;
    }
};
}  // namespace

SW_CLAP_ENTRY(vo03, Vo03)
