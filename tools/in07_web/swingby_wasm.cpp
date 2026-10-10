// SWINGBY (SW IN07) in the browser: the engine (products/in07) as WebAssembly for the web trial (tools/in07_web.py). One synth; plain C
// functions; the output is two float buffers the AudioWorklet copies out. Nothing here allocates after sw_init.
//   zig c++ --target=wasm32-wasi -mexec-model=reactor -msimd128 -O2 -fno-exceptions (tools/in07_web.py builds it)
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <vector>

#define SW_EXPORT(name) __attribute__((export_name(#name)))

namespace {
sw::in07::Processor* g = nullptr;
std::vector<float> L, R;
int cap = 0;
}  // namespace

extern "C" {
// the synth at a sample rate; buffers for up to maxBlock samples (the worklet asks for 128)
SW_EXPORT(sw_init) int sw_init(double fs, int maxBlock) {
    if (!g) g = new sw::in07::Processor();
    cap = std::max(1, maxBlock);
    L.assign(static_cast<size_t>(cap), 0.0f); R.assign(static_cast<size_t>(cap), 0.0f);
    (void)sw::in07::factoryPresets();
    (void)sw::in07::presetPlain(0);
    g->prepare(fs, cap);
    return sw::in07::kNumParams;
}
SW_EXPORT(sw_num_params) int sw_num_params() { return sw::in07::kNumParams; }
SW_EXPORT(sw_set) void sw_set(int id, double v) { if (g) g->setParam(id, v); }
SW_EXPORT(sw_get) double sw_get(int id) { return g ? g->param(id) : 0.0; }
// a factory preset (-1: Init) as one patch (the held notes fade and come back with it)
SW_EXPORT(sw_preset) void sw_preset(int index) {
    if (!g) return;
    g->beginPatch();
    if (index < 0) sw::in07::applyInit(*g); else sw::in07::applyPreset(*g, index);
    g->setParam(sw::in07::PresetSelect, index + 1);
    g->endPatch();
}
SW_EXPORT(sw_note_on) void sw_note_on(int key, double vel) { if (g && key >= 0 && key < 128) g->noteOn(key, std::clamp(vel, 0.0, 1.0), 16, -1); }
SW_EXPORT(sw_note_off) void sw_note_off(int key) { if (g && key >= 0 && key < 128) g->noteOff(key, 16); }
SW_EXPORT(sw_all_off) void sw_all_off() { if (g) g->allNotesOff(); }
SW_EXPORT(sw_wheel) void sw_wheel(double v) { if (g) g->modWheel(v); }
SW_EXPORT(sw_bend) void sw_bend(double v) { if (g) g->pitchBend(std::clamp(v, -1.0, 1.0)); }
SW_EXPORT(sw_tempo) void sw_tempo(double bpm) { if (g) g->setTempo(bpm); }
SW_EXPORT(sw_notes) int sw_notes() { return g ? g->notes() : 0; }
SW_EXPORT(sw_left) float* sw_left() { return L.data(); }
SW_EXPORT(sw_right) float* sw_right() { return R.data(); }
// n samples into the buffers (n <= maxBlock)
SW_EXPORT(sw_process) void sw_process(int n) {
    if (!g) return;
    n = std::clamp(n, 0, cap);
    float* c[2] = {L.data(), R.data()};
    g->process(c, 2, n);
}
}
