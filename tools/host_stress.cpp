// sw-host-stress — the audio thread and the window's thread on one plug-in at the same time, to be built with ThreadSanitizer (tools/stress_tsan.sh).
//   The audio thread processes noise in blocks of 256 and now and then receives parameter events (the host's automation) and a reset() (the transport stopped or jumped); a second thread plays the window: it polls ("p"), moves parameters
//   ("s <i> <v>" with the gesture messages around them) and presses the buttons a screen can press ("c <name> <arg>": every call any product knows, with plausible arguments). The main thread
//   waits and stops them. What it finds is data races (TSan prints them and the exit code is not 0), crashes, and an audio thread that fails or sees NaN.
//   usage: sw-host-stress <plug-in.clap> [seconds = 6]
#include <clap/clap.h>
#include "sw_message.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr double kSr = 48000.0;
constexpr uint32_t kBlock = 256;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostNop(const clap_host_t*) {}
clap_host_t gHost = {CLAP_VERSION_INIT, nullptr, "sw-host-stress", "SEVENTHWELL", "", "1", hostGetExtension, hostNop, hostNop, hostNop};

struct Rng { uint64_t s; explicit Rng(uint64_t x) : s(x) {} double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<double>(s >> 11) / 9007199254740992.0; } };   // [0, 1)

struct Events {
    std::vector<clap_event_param_value_t> ev; clap_input_events_t in{};
    Events() { in.ctx = this; in.size = [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<Events*>(l->ctx)->ev.size()); };
               in.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { auto* e = static_cast<Events*>(l->ctx); return i < e->ev.size() ? &e->ev[i].header : nullptr; }; }
    void set(clap_id id, double v, uint32_t t) { clap_event_param_value_t e{}; e.header.size = sizeof e; e.header.time = t; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE; e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v; ev.push_back(e); }
};
bool outTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plug-in.clap> [seconds]\n", argv[0]); return 2; }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 6.0;
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry")); if (!entry || !entry->init(argv[1])) return 2;
    auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID)); const clap_plugin_descriptor_t* d = fac->get_plugin_descriptor(fac, 0);
    const clap_plugin_t* p = fac->create_plugin(fac, &gHost, d->id);
    if (!p || !p->init(p) || !p->activate(p, kSr, 16, kBlock) || !p->start_processing(p)) { std::fprintf(stderr, "create failed\n"); return 2; }
    const auto* pe = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS));
    const auto* msg = static_cast<const sw_plugin_message_t*>(p->get_extension(p, SW_EXT_MESSAGE));
    if (!pe || !msg) { std::fprintf(stderr, "no params / message extension\n"); return 2; }
    const uint32_t np = pe->count(p); std::vector<clap_param_info_t> info(np); for (uint32_t i = 0; i < np; ++i) pe->get_info(p, i, &info[i]);
    const uint32_t nIn = ports ? std::min<uint32_t>(2, ports->count(p, true)) : 1;
    clap_audio_port_info_t pi{}; uint32_t scCh = 2; if (nIn > 1 && ports->get(p, true, 1, &pi)) scCh = std::min<uint32_t>(2, pi.channel_count);

    std::atomic<bool> stop{false}; std::atomic<long> blocks{0}, messages{0}; std::atomic<bool> bad{false};
    std::thread audio([&] {
        Rng rng(1); std::vector<float> in[2], sc[2], out[2]; for (int c = 0; c < 2; ++c) { in[c].resize(kBlock); sc[c].resize(kBlock); out[c].resize(kBlock); }
        while (!stop.load()) {
            Events ev;
            if (rng.next() < 0.3) for (int k = 0, n = 1 + static_cast<int>(rng.next() * 4); k < n; ++k) { const uint32_t i = static_cast<uint32_t>(rng.next() * np); if (i >= np || (info[i].flags & CLAP_PARAM_IS_READONLY)) continue; double v = info[i].min_value + rng.next() * (info[i].max_value - info[i].min_value); if (info[i].flags & CLAP_PARAM_IS_STEPPED) v = std::round(v); ev.set(info[i].id, v, static_cast<uint32_t>(rng.next() * kBlock)); }
            std::sort(ev.ev.begin(), ev.ev.end(), [](const auto& a, const auto& b) { return a.header.time < b.header.time; });
            for (int c = 0; c < 2; ++c) for (uint32_t i = 0; i < kBlock; ++i) { const float x = static_cast<float>((rng.next() - 0.5) * 0.35); in[c][i] = x; sc[c][i] = 0.5f * x; out[c][i] = 0.f; }
            float* ip[2] = {in[0].data(), in[1].data()}; float* sp[2] = {sc[0].data(), sc[1].data()}; float* op[2] = {out[0].data(), out[1].data()};
            clap_audio_buffer_t ib[2]{}; ib[0].channel_count = 2; ib[0].data32 = ip; ib[1].channel_count = scCh; ib[1].data32 = sp;
            clap_audio_buffer_t ob{}; ob.channel_count = 2; ob.data32 = op; clap_output_events_t oe{nullptr, outTryPush};
            clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = kBlock; pr.audio_inputs = ib; pr.audio_inputs_count = nIn; pr.audio_outputs = &ob; pr.audio_outputs_count = 1; pr.in_events = &ev.in; pr.out_events = &oe;
            if (rng.next() < 0.004) p->reset(p);   // the host stopped or jumped (audio thread, between two blocks)
            if (p->process(p, &pr) == CLAP_PROCESS_ERROR) bad = true;
            for (int c = 0; c < 2; ++c) for (uint32_t i = 0; i < kBlock; ++i) if (!std::isfinite(out[c][i])) bad = true;
            ++blocks; std::this_thread::sleep_for(std::chrono::microseconds(300));   // faster than real time, but leave the other thread room
        }
    });
    std::thread window([&] {
        Rng rng(7);
        static const char* calls[] = {"tap", "learn", "check", "forget", "assist 1", "assist 0", "linkwatch 1", "linkwatch 0", "randomize", "clear", "remember", "reset", "measure", "autoalign", "ringout 1", "ringout 0", "lock", "clearlive", "flat",
                                      "refclear 1", "refclear 2", "looprange 0.2 0.6", "resetcounts", "resetmeters", "arm 1", "arm 0", "output 1", "output 0", "record 1", "record 0", "mark", "clap 150", "lockall 1", "lockall 0", "export", "presetlist", "irabort", "refabort"};
        while (!stop.load()) {
            const double r = rng.next();
            if (r < 0.45) msg->send(p, "p");
            else if (r < 0.8 && np) { const uint32_t i = static_cast<uint32_t>(rng.next() * np) % np; double v = info[i].min_value + rng.next() * (info[i].max_value - info[i].min_value); if (info[i].flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
                msg->send(p, ("b " + std::to_string(i)).c_str()); char b[64]; std::snprintf(b, sizeof b, "s %u %.9g", i, v); msg->send(p, b); msg->send(p, ("e " + std::to_string(i)).c_str()); }
            else { const char* c = calls[static_cast<size_t>(rng.next() * (sizeof calls / sizeof *calls)) % (sizeof calls / sizeof *calls)]; msg->send(p, (std::string("c ") + c).c_str()); }
            ++messages; std::this_thread::sleep_for(std::chrono::microseconds(150));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(seconds * 1000)));
    stop = true; audio.join(); window.join();
    p->stop_processing(p); p->deactivate(p); p->destroy(p); entry->deinit(); dlclose(lib);
    std::printf("%-28s %6ld blocks, %7ld window messages: %s\n", d->name, blocks.load(), messages.load(), bad.load() ? "FAIL (process error or NaN)" : "ok");
    return bad.load() ? 1 : 0;
}
