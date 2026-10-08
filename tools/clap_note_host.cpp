// A minimal CLAP host for instruments: loads a .clap, plays notes (CLAP dialect and MIDI dialect), and checks that sound comes out,
// stops after the release, and that every note reports its end. Also saves and loads the state. Linux / macOS (dlopen).
//   g++ -std=c++17 -O2 -Ibuild-cmake/_deps/clap-src/include tools/clap_note_host.cpp -ldl -o build/clap_note_host
//   build/clap_note_host "build-cmake/plugins/SW IN07 SWINGBY.clap"
#include <clap/clap.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct InEvents {
    std::vector<std::vector<uint8_t>> ev;
    static uint32_t size(const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<InEvents*>(l->ctx)->ev.size()); }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) { return reinterpret_cast<const clap_event_header_t*>(static_cast<InEvents*>(l->ctx)->ev[i].data()); }
    template <class E> void push(const E& e) { std::vector<uint8_t> b(sizeof(E)); std::memcpy(b.data(), &e, sizeof(E)); ev.push_back(b); }
};
struct OutEvents {
    int noteEnds = 0; std::vector<int> keys;
    static bool tryPush(const clap_output_events_t* l, const clap_event_header_t* h) {
        auto* o = static_cast<OutEvents*>(l->ctx);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_NOTE_END) { ++o->noteEnds; o->keys.push_back(reinterpret_cast<const clap_event_note_t*>(h)->key); }
        return true;
    }
};
const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNop(const clap_host_t*) {}
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "sw-note-host", "SEVENTHWELL", "", "0.1", hostExt, hostNop, hostNop, hostNop};

clap_event_note_t note(uint16_t type, uint32_t t, int key, double vel, int id) {
    clap_event_note_t e{}; e.header.size = sizeof(e); e.header.time = t; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = type;
    e.note_id = id; e.port_index = 0; e.channel = 0; e.key = static_cast<int16_t>(key); e.velocity = vel; return e;
}
clap_event_midi_t midi(uint32_t t, uint8_t a, uint8_t b, uint8_t c) {
    clap_event_midi_t e{}; e.header.size = sizeof(e); e.header.time = t; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0; e.data[0] = a; e.data[1] = b; e.data[2] = c; return e;
}
struct Mem { std::vector<uint8_t> d; size_t pos = 0; };
int64_t wr(const clap_ostream_t* s, const void* b, uint64_t n) { auto* m = static_cast<Mem*>(s->ctx); const auto* c = static_cast<const uint8_t*>(b); m->d.insert(m->d.end(), c, c + n); return static_cast<int64_t>(n); }
int64_t rd(const clap_istream_t* s, void* b, uint64_t n) { auto* m = static_cast<Mem*>(s->ctx); const uint64_t k = std::min<uint64_t>(n, m->d.size() - m->pos); std::memcpy(b, m->d.data() + m->pos, k); m->pos += k; return static_cast<int64_t>(k); }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin.clap>\n", argv[0]); return 2; }
    std::string path = argv[1];
#ifdef __APPLE__
    // a macOS bundle: Contents/MacOS/<name>
#endif
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry || !entry->init(path.c_str())) { std::fprintf(stderr, "no entry\n"); return 1; }
    const auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* d = fac->get_plugin_descriptor(fac, 0);
    const clap_plugin_t* p = fac->create_plugin(fac, &kHost, d->id);
    if (!p || !p->init(p)) { std::fprintf(stderr, "create failed\n"); return 1; }
    const double fs = 48000; const uint32_t B = 256;
    p->activate(p, fs, 1, B); p->start_processing(p);
    std::vector<float> L(B), R(B); float* ch[2] = {L.data(), R.data()};
    clap_audio_buffer_t out{}; out.data32 = ch; out.channel_count = 2;
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    // play: blocks of 256; events at their sample inside the block; returns the RMS (dB) of [a, b) seconds and counts note ends
    auto run = [&](double seconds, std::vector<std::pair<double, std::vector<uint8_t>>> events, double a, double b, OutEvents& oe) {
        const size_t n = static_cast<size_t>(seconds * fs); double sum = 0; size_t cnt = 0; size_t e = 0;
        for (size_t off = 0; off < n; off += B) {
            InEvents ie;
            while (e < events.size() && static_cast<size_t>(events[e].first * fs) < off + B) {
                auto bytes = events[e].second; auto* h = reinterpret_cast<clap_event_header_t*>(bytes.data());
                h->time = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(events[e].first * fs) - static_cast<int64_t>(off)));
                ie.ev.push_back(bytes); ++e;
            }
            clap_input_events_t in{&ie, InEvents::size, InEvents::get};
            clap_output_events_t o{&oe, OutEvents::tryPush};
            clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &in; pr.out_events = &o;
            if (p->process(p, &pr) == CLAP_PROCESS_ERROR) { std::printf("process error\n"); ++fails; return -999.0; }
            for (uint32_t i = 0; i < B; ++i) {
                const double t = (off + i) / fs;
                if (t >= a && t < b) { sum += L[i] * L[i] + R[i] * R[i]; cnt += 2; }
            }
        }
        return 10 * std::log10(sum / std::max<size_t>(cnt, 1) + 1e-30);
    };
    auto bytes = [](const auto& ev) { std::vector<uint8_t> b(sizeof(ev)); std::memcpy(b.data(), &ev, sizeof(ev)); return b; };

    {   // CLAP dialect: a chord held 1 s, then the tail (reverb and delay are on in the default patch), then silence
        OutEvents oe;
        std::vector<std::pair<double, std::vector<uint8_t>>> ev = {
            {0.01, bytes(note(CLAP_EVENT_NOTE_ON, 0, 57, 0.9, 1))}, {0.01, bytes(note(CLAP_EVENT_NOTE_ON, 0, 60, 0.9, 2))}, {0.01, bytes(note(CLAP_EVENT_NOTE_ON, 0, 64, 0.9, 3))},
            {1.0, bytes(note(CLAP_EVENT_NOTE_OFF, 0, 57, 0, 1))}, {1.0, bytes(note(CLAP_EVENT_NOTE_OFF, 0, 60, 0, 2))}, {1.0, bytes(note(CLAP_EVENT_NOTE_OFF, 0, 64, 0, 3))}};
        const double held = run(1.0, ev, 0.2, 0.9, oe);
        ev.clear();
        const double after = run(2.0, ev, 1.0, 2.0, oe);
        const double late = run(12.0, ev, 11.0, 12.0, oe);
        std::printf("      chord held %.1f dBFS RMS, 1..2 s after the release %.1f dBFS, 12 s after %.1f dBFS, note ends %d\n", held, after, late, oe.noteEnds);
        check(held > -40.0 && held < 0.0, "a CLAP chord sounds");
        check(after < held - 25.0, "the tail dies away");
        check(late < -200.0, "then silence (the effects sleep)");
        check(oe.noteEnds == 3, "three note ends");
    }
    {   // MIDI dialect: note on, pitch bend, sustain pedal holds after note off, pedal up releases
        OutEvents oe;
        std::vector<std::pair<double, std::vector<uint8_t>>> ev = {
            {0.01, bytes(midi(0, 0xB0, 64, 127))}, {0.01, bytes(midi(0, 0x90, 69, 100))}, {0.2, bytes(midi(0, 0xE0, 0x00, 0x60))},
            {0.5, bytes(midi(0, 0x80, 69, 0))}, {1.5, bytes(midi(0, 0xB0, 64, 0))}};
        const double held = run(1.5, ev, 0.6, 1.4, oe);
        ev.clear();
        const double after = run(2.0, ev, 1.0, 2.0, oe);
        std::printf("      MIDI note on the pedal %.1f dBFS RMS, after pedal up %.1f dBFS, note ends %d\n", held, after, oe.noteEnds);
        check(held > -40.0, "a MIDI note held by the sustain pedal sounds");
        check(after < held - 25.0, "pedal up releases it");
        check(oe.noteEnds == 1, "one note end");
    }
    {   // state: set Cutoff (id from the params ext) through a param event, save, change, load, read back
        const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        const auto* state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
        check(params && state, "params and state extensions");
        if (params && state) {
            const uint32_t n = params->count(p);
            clap_id cutoff = CLAP_INVALID_ID;
            for (uint32_t i = 0; i < n; ++i) { clap_param_info_t inf; params->get_info(p, i, &inf); if (!std::strcmp(inf.module, "in07.l1.flt.cutoff")) cutoff = inf.id; }
            check(cutoff != CLAP_INVALID_ID, "L1 cutoff is found by its id string");
            clap_event_param_value_t pv{}; pv.header.size = sizeof(pv); pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID; pv.header.type = CLAP_EVENT_PARAM_VALUE;
            pv.param_id = cutoff; pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1; pv.value = 0.25;
            OutEvents oe; run(0.01, {{0.0, bytes(pv)}}, 0, 0, oe);
            Mem m; clap_ostream_t os{&m, wr}; state->save(p, &os);
            pv.value = 0.75; run(0.01, {{0.0, bytes(pv)}}, 0, 0, oe);
            clap_istream_t is{&m, rd}; state->load(p, &is);
            double v = 0; params->get_value(p, cutoff, &v);
            char txt[64]; params->value_to_text(p, cutoff, v, txt, sizeof(txt));
            std::printf("      cutoff after load: %.3f (%s), state %zu bytes, %u parameters\n", v, txt, m.d.size(), n);
            check(std::abs(v - 0.25) < 1e-9, "state round trip");
        }
    }
    p->stop_processing(p); p->deactivate(p); p->destroy(p); entry->deinit();
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
