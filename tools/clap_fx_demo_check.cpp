// Checks the demo silence in an effect plug-in (the shared effect adapter): with SW_LICENSE_TEST_DEMO=1 the output is silent from
// 30 s (+ the 10 ms fade) to 33 s; without it (a development build) it is not. Linux / macOS (dlopen).
//   g++ -std=c++17 -O2 -Ibuild-cmake/_deps/clap-src/include tools/clap_fx_demo_check.cpp -ldl -o build/clap_fx_demo_check
//   build/clap_fx_demo_check "build-cmake/plugins/SW UT01 Gain.clap"
#include <clap/clap.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNop(const clap_host_t*) {}
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "sw-fx-demo-check", "SEVENTHWELL", "", "0.1", hostExt, hostNop, hostNop, hostNop};
uint32_t noEvents(const clap_input_events_t*) { return 0; }
const clap_event_header_t* noEvent(const clap_input_events_t*, uint32_t) { return nullptr; }
bool dropEvent(const clap_output_events_t*, const clap_event_header_t*) { return true; }

// RMS (dBFS) of [a, b) seconds of a 0.25 constant-ish input (a slow sine) through the plug-in
double run(const clap_plugin_t* p, double a, double b) {
    const double fs = 48000; const uint32_t B = 256;
    p->activate(p, fs, 1, B); p->start_processing(p);
    std::vector<float> il(B), ir(B), ol(B), orr(B), sl(B, 0.0f), sr(B, 0.0f);
    float* in[2] = {il.data(), ir.data()}; float* out[2] = {ol.data(), orr.data()}; float* sc[2] = {sl.data(), sr.data()};
    clap_audio_buffer_t ib[2]{}, ob{};
    ib[0].data32 = in; ib[0].channel_count = 2; ib[1].data32 = sc; ib[1].channel_count = 2;
    ob.data32 = out; ob.channel_count = 2;
    clap_input_events_t ie{nullptr, noEvents, noEvent};
    clap_output_events_t oe{nullptr, dropEvent};
    double sum = 0; size_t cnt = 0;
    const size_t n = static_cast<size_t>(34.0 * fs);
    for (size_t off = 0; off < n; off += B) {
        for (uint32_t i = 0; i < B; ++i) il[i] = ir[i] = static_cast<float>(0.25 * std::sin(2 * 3.14159265358979 * 220.0 * (off + i) / fs));
        clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = B; pr.audio_inputs = ib; pr.audio_inputs_count = 1; pr.audio_outputs = &ob; pr.audio_outputs_count = 1;
        pr.in_events = &ie; pr.out_events = &oe;
        p->process(p, &pr);
        for (uint32_t i = 0; i < B; ++i) { const double t = (off + i) / fs; if (t >= a && t < b) { sum += ol[i] * ol[i] + orr[i] * orr[i]; cnt += 2; } }
    }
    p->stop_processing(p); p->deactivate(p);
    return 10 * std::log10(sum / std::max<size_t>(cnt, 1) + 1e-30);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <effect.clap>\n", argv[0]); return 2; }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry || !entry->init(argv[1])) return 1;
    const auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* p = fac->create_plugin(fac, &kHost, fac->get_plugin_descriptor(fac, 0)->id);
    if (!p || !p->init(p)) return 1;
    setenv("SW_LICENSE_TEST_DEMO", "1", 1);
    const double before = run(p, 25.0, 29.0), gap = run(p, 30.5, 32.5);
    unsetenv("SW_LICENSE_TEST_DEMO");
    const double clean = run(p, 30.5, 32.5);
    p->destroy(p); entry->deinit();
    std::printf("demo: 25..29 s %.1f dBFS, 30.5..32.5 s %.1f dBFS; without the switch 30.5..32.5 s %.1f dBFS\n", before, gap, clean);
    const bool ok = before > -40.0 && gap < -200.0 && clean > -40.0;
    std::printf(ok ? "ok    the demo silence in an effect\n" : "FAIL  the demo silence in an effect\n");
    return ok ? 0 : 1;
}
