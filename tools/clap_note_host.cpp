// A minimal CLAP host for instruments: loads a .clap, plays notes (CLAP dialect and MIDI dialect), and checks that sound comes out,
// stops after the release, and that every note reports its end. Also saves and loads the state (and refuses damaged states without
// changing anything), and, when the plug-in has them, browses and loads presets as a host's preset browser does (preset-discovery
// factory, preset-load: the factory presets and the user folder, a damaged preset file refused). Linux / macOS (dlopen).
// The user folder is redirected (XDG_DATA_HOME) to a temporary folder, so the user's own presets are not touched.
// The demo silence (no licence) is checked with SW_LICENSE_TEST_DEMO=1; a development build must play without it.
//   g++ -std=c++17 -O2 -Ibuild-cmake/_deps/clap-src/include tools/clap_note_host.cpp -ldl -o build/clap_note_host
//   build/clap_note_host "build-cmake/plugins/SW IN07 SWINGBY.clap"
#include <clap/clap.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
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
    std::vector<std::pair<clap_id, double>> values;   // parameter values the plug-in reports (a preset it loaded)
    static bool tryPush(const clap_output_events_t* l, const clap_event_header_t* h) {
        auto* o = static_cast<OutEvents*>(l->ctx);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_NOTE_END) { ++o->noteEnds; o->keys.push_back(reinterpret_cast<const clap_event_note_t*>(h)->key); }
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) { const auto* v = reinterpret_cast<const clap_event_param_value_t*>(h); o->values.push_back({v->param_id, v->value}); }
        return true;
    }
};
struct PresetLoadLog { int loaded = 0, errors = 0; std::string lastError; } gPresetLog;
void onPresetError(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char* msg) { ++gPresetLog.errors; gPresetLog.lastError = msg ? msg : ""; }
void onPresetLoaded(const clap_host_t*, uint32_t, const char*, const char*) { ++gPresetLog.loaded; }
const clap_host_preset_load_t kHostPresetLoad = {onPresetError, onPresetLoaded};
const void* hostExt(const clap_host_t*, const char* id) { return !std::strcmp(id, CLAP_EXT_PRESET_LOAD) ? &kHostPresetLoad : nullptr; }

// a host's preset index: what the provider declares and what it says about each preset
struct Index {
    std::vector<std::string> fileExts;
    struct Loc { uint32_t flags, kind; std::string name, location; };
    std::vector<Loc> locs;
    static bool declareFiletype(const clap_preset_discovery_indexer_t* ix, const clap_preset_discovery_filetype_t* f) { static_cast<Index*>(ix->indexer_data)->fileExts.push_back(f->file_extension ? f->file_extension : ""); return true; }
    static bool declareLocation(const clap_preset_discovery_indexer_t* ix, const clap_preset_discovery_location_t* l) {
        static_cast<Index*>(ix->indexer_data)->locs.push_back({l->flags, l->kind, l->name ? l->name : "", l->location ? l->location : ""}); return true;
    }
    static bool declareSoundpack(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
    static const void* ext(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }
};
struct Receiver {
    struct Item { std::string name, key; std::vector<std::string> features, creators; int ids = 0; };
    std::vector<Item> items; int errors = 0;
    static Receiver* me(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Receiver*>(r->receiver_data); }
    static void onError(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char*) { ++me(r)->errors; }
    static bool begin(const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* key) { me(r)->items.push_back({name ? name : "", key ? key : "", {}, {}, 0}); return true; }
    static void addId(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t*) { if (!me(r)->items.empty()) ++me(r)->items.back().ids; }
    static void soundpack(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
    static void flags(const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
    static void creator(const clap_preset_discovery_metadata_receiver_t* r, const char* c) { if (!me(r)->items.empty()) me(r)->items.back().creators.push_back(c); }
    static void description(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
    static void timestamps(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
    static void feature(const clap_preset_discovery_metadata_receiver_t* r, const char* f) { if (!me(r)->items.empty()) me(r)->items.back().features.push_back(f); }
    static void extra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}
    clap_preset_discovery_metadata_receiver_t clap() { return {this, onError, begin, addId, soundpack, flags, creator, description, timestamps, feature, extra}; }
};
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
            // presets: the selector (by its id string) loads a preset; the plug-in reports the values it changed
            clap_id sel = CLAP_INVALID_ID, mode = CLAP_INVALID_ID;
            int polar = -1;
            for (uint32_t i = 0; i < n; ++i) {
                clap_param_info_t inf; params->get_info(p, i, &inf);
                if (!std::strcmp(inf.module, "in07.preset")) {
                    sel = inf.id;
                    check(!(inf.flags & CLAP_PARAM_IS_AUTOMATABLE) && (inf.flags & CLAP_PARAM_IS_STEPPED), "the preset selector is stepped and not automatable");
                    for (int k = 0; k <= static_cast<int>(inf.max_value); ++k) { char t[64]; params->value_to_text(p, sel, k, t, sizeof(t)); if (!std::strcmp(t, "Polar Bass")) polar = k; }
                }
                if (!std::strcmp(inf.module, "in07.mode")) mode = inf.id;
            }
            check(sel != CLAP_INVALID_ID && polar > 0, "the preset selector lists the factory presets by name");
            if (sel != CLAP_INVALID_ID && polar > 0) {
                pv.param_id = sel; pv.value = polar;
                OutEvents po; run(0.01, {{0.0, bytes(pv)}}, 0, 0, po);
                double c = 0, m2 = 0; params->get_value(p, cutoff, &c); params->get_value(p, mode, &m2);
                char ct[64]; params->value_to_text(p, cutoff, c, ct, sizeof(ct));
                bool reported = false; for (auto& x : po.values) reported = reported || (x.first == cutoff && std::abs(x.second - c) < 1e-12);
                std::printf("      Polar Bass: L1 cutoff %s, mode step %.0f, %zu values reported to the host\n", ct, m2, po.values.size());
                check(!std::strcmp(ct, "300 Hz") && m2 == 1.0, "selecting a preset loads it (L1 cutoff 300 Hz, Mono)");
                check(reported && po.values.size() > 20, "the plug-in reports the changed values to the host");
                // a session saved after a tweak comes back with the tweak (the selector is restored, not applied again)
                pv.param_id = cutoff; pv.value = 0.6; run(0.01, {{0.0, bytes(pv)}}, 0, 0, po);
                Mem m3; clap_ostream_t os3{&m3, wr}; state->save(p, &os3);
                pv.param_id = sel; pv.value = 0; run(0.01, {{0.0, bytes(pv)}}, 0, 0, po);   // Init
                clap_istream_t is3{&m3, rd}; state->load(p, &is3);
                double c3 = 0, s3 = 0; params->get_value(p, cutoff, &c3); params->get_value(p, sel, &s3);
                check(std::abs(c3 - 0.6) < 1e-9 && s3 == polar, "a saved session keeps its tweaks and its preset name");
                // through flush (no audio running): Init puts the defaults back
                InEvents ie; pv.value = 0; pv.param_id = sel; ie.push(pv);
                clap_input_events_t in{&ie, InEvents::size, InEvents::get};
                OutEvents fo; clap_output_events_t o{&fo, OutEvents::tryPush};
                params->flush(p, &in, &o);
                double c4 = 0; params->get_value(p, cutoff, &c4); char t4[64]; params->value_to_text(p, cutoff, c4, t4, sizeof(t4));
                std::printf("      Init through flush: L1 cutoff %s, %zu values reported\n", t4, fo.values.size());
                check(!std::strcmp(t4, "2.4 kHz") && !fo.values.empty(), "Init through flush restores the defaults and reports them");
            }
        }
    }
    {   // damaged states: refused, nothing changes (a cut state, an impossible count); values that are not numbers read as defaults
        const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        const auto* state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
        Mem good; clap_ostream_t os{&good, wr}; state->save(p, &os);
        const uint32_t n = params->count(p);
        std::vector<double> before(n); for (uint32_t i = 0; i < n; ++i) params->get_value(p, i, &before[i]);
        Mem cut = good; cut.d.resize(cut.d.size() / 2);
        clap_istream_t isc{&cut, rd};
        const bool cutOk = state->load(p, &isc);
        Mem huge; huge.d = {'S', 'W', 'A', '1', 0xFF, 0xFF, 0xFF, 0xFF}; huge.d.resize(64, 0);
        clap_istream_t ish{&huge, rd};
        const bool hugeOk = state->load(p, &ish);
        bool same = true; for (uint32_t i = 0; i < n; ++i) { double v = 0; params->get_value(p, i, &v); same = same && v == before[i]; }
        check(!cutOk && !hugeOk && same, "a cut state and an impossible count are refused and change nothing");
        Mem nan = good; const double q = std::nan(""), big = 1e300;
        for (uint32_t i = 0; i < n; ++i) std::memcpy(nan.d.data() + 8 + 8 * i, (i % 2) ? &q : &big, 8);
        clap_istream_t isn{&nan, rd};
        const bool nanOk = state->load(p, &isn);
        bool finite = true; for (uint32_t i = 0; i < n; ++i) { double v = 0; params->get_value(p, i, &v); finite = finite && std::isfinite(v); }
        OutEvents oe;
        const double lvl = run(0.6, {{0.01, bytes(note(CLAP_EVENT_NOTE_ON, 0, 60, 0.9, 7))}, {0.5, bytes(note(CLAP_EVENT_NOTE_OFF, 0, 60, 0, 7))}}, 0.0, 0.6, oe);
        std::printf("      a state of NaN and 1e300 values: loaded %d, values finite %d, a note then plays at %.1f dBFS RMS\n", nanOk, finite, lvl);
        check(nanOk && finite && std::isfinite(lvl), "values that are not numbers or out of range become defaults or limits (no NaN reaches the sound)");
        clap_istream_t isg{&good, rd}; state->load(p, &isg);
    }
    const auto* pdf = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    const auto* pload = static_cast<const clap_plugin_preset_load_t*>(p->get_extension(p, CLAP_EXT_PRESET_LOAD));
    if (pdf && pload) {   // a host's preset browser: index the provider's locations, then load a factory preset and the user's files
        namespace fs = std::filesystem;
        const fs::path tmp = fs::temp_directory_path() / ("sw_note_host_" + std::to_string(static_cast<long>(std::rand())));
        setenv("XDG_DATA_HOME", tmp.c_str(), 1);   // the provider's user folder lands in the temporary folder
        Index ix;
        clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "sw-note-host", "SEVENTHWELL", "", "0.1", &ix, Index::declareFiletype, Index::declareLocation, Index::declareSoundpack, Index::ext};
        check(pdf->count(pdf) == 1, "one preset provider");
        const clap_preset_discovery_provider_descriptor_t* pd = pdf->get_descriptor(pdf, 0);
        const clap_preset_discovery_provider_t* prov = pd ? pdf->create(pdf, &indexer, pd->id) : nullptr;
        check(prov && prov->init(prov), "the provider initialises");
        if (prov) {
            std::string userDir; bool factoryLoc = false;
            for (const auto& l : ix.locs) {
                if (l.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && (l.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT)) factoryLoc = true;
                if (l.kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && (l.flags & CLAP_PRESET_DISCOVERY_IS_USER_CONTENT)) userDir = l.location;
            }
            check(ix.fileExts.size() == 1 && ix.fileExts[0] == "swpreset" && factoryLoc && userDir.rfind(tmp.string(), 0) == 0,
                  "it declares the .swpreset type, the factory presets and the user folder (under XDG_DATA_HOME)");
            Receiver fr; auto frc = fr.clap();
            const bool fok = prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &frc);
            bool named = !fr.items.empty();
            for (const auto& it : fr.items) named = named && !it.name.empty() && it.key == it.name && it.ids == 1 && it.features.size() == 2;
            std::printf("      factory location: %zu presets (first \"%s\", features %s / %s)\n", fr.items.size(), fr.items.empty() ? "" : fr.items[0].name.c_str(),
                        fr.items.empty() ? "" : fr.items[0].features[0].c_str(), fr.items.empty() || fr.items[0].features.size() < 2 ? "" : fr.items[0].features[1].c_str());
            check(fok && fr.items.size() > 100 && named, "the factory presets are listed by name, with the plug-in id and a category feature");
            // the user folder: one good file, one from another product, one damaged
            const fs::path ud = fs::u8path(userDir);
            { std::ofstream f(ud / "Soft Pad.swpreset"); f << "SW-PRESET 1\nproduct=in07\nname=Soft Pad\ncategory=PAD\nauthor=tester\nin07.l1.flt.cutoff=600\nin07.mode=Mono\n"; }
            { std::ofstream f(ud / "Other.swpreset"); f << "SW-PRESET 1\nproduct=dy01\nname=Other\n"; }
            { std::ofstream f(ud / "Broken.swpreset", std::ios::binary); f << std::string("\x00\x01garbage", 9); }
            int ok = 0, bad = 0; Receiver ur; auto urc = ur.clap();
            for (const char* f : {"Soft Pad.swpreset", "Other.swpreset", "Broken.swpreset"}) (prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, (ud / f).c_str(), &urc) ? ok : bad)++;
            check(ok == 1 && bad == 2 && ur.errors == 2 && ur.items.size() == 1 && ur.items[0].name == "Soft Pad" && ur.items[0].creators.size() == 1,
                  "the user folder: the good file is listed (name, author), the other product's and the damaged one are refused with an error");
            // loading: a factory preset by its key, then the user file, then the damaged file (refused, nothing changes)
            const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
            clap_id cutoff = CLAP_INVALID_ID, mode = CLAP_INVALID_ID, sel = CLAP_INVALID_ID;
            for (uint32_t i = 0; i < params->count(p); ++i) {
                clap_param_info_t inf; params->get_info(p, i, &inf);
                if (!std::strcmp(inf.module, "in07.l1.flt.cutoff")) cutoff = inf.id;
                if (!std::strcmp(inf.module, "in07.mode")) mode = inf.id;
                if (!std::strcmp(inf.module, "in07.preset")) sel = inf.id;
            }
            auto text = [&](clap_id id) { double v = 0; params->get_value(p, id, &v); static char t[64]; params->value_to_text(p, id, v, t, sizeof(t)); return std::string(t); };
            gPresetLog = {};
            const bool l1 = pload->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "Polar Bass");
            OutEvents oe; run(0.02, {}, 0, 0, oe);
            std::printf("      loaded \"Polar Bass\" from the browser: L1 cutoff %s, mode %s, selector %s\n", text(cutoff).c_str(), text(mode).c_str(), text(sel).c_str());
            check(l1 && text(cutoff) == "300 Hz" && text(mode) == "Mono" && text(sel) == "Polar Bass" && gPresetLog.loaded == 1, "a factory preset loads from the browser (and moves the selector)");
            const bool l2 = pload->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, (ud / "Soft Pad.swpreset").c_str(), nullptr);
            const double lvl = run(0.6, {{0.01, bytes(note(CLAP_EVENT_NOTE_ON, 0, 60, 0.9, 9))}, {0.5, bytes(note(CLAP_EVENT_NOTE_OFF, 0, 60, 0, 9))}}, 0.1, 0.5, oe);
            std::printf("      loaded the user file: L1 cutoff %s, mode %s, a note at %.1f dBFS RMS\n", text(cutoff).c_str(), text(mode).c_str(), lvl);
            check(l2 && text(cutoff) == "600 Hz" && text(mode) == "Mono" && lvl > -60.0 && gPresetLog.loaded == 2, "a user preset file loads from the browser and plays");
            const std::string c0 = text(cutoff);
            const bool l3 = pload->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, (ud / "Broken.swpreset").c_str(), nullptr);
            const bool l4 = pload->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "No Such Preset");
            check(!l3 && !l4 && gPresetLog.errors == 2 && text(cutoff) == c0, "a damaged file and an unknown key are refused (the host is told why) and change nothing");
            prov->destroy(prov);
        }
        fs::remove_all(tmp);
    }
    {   // without a licence (forced here with SW_LICENSE_TEST_DEMO=1): 3 s of silence every 60 s from 30 s; a development build plays clean
        auto heldRms = [&](const clap_plugin_t* q, double a, double b) {
            q->activate(q, fs, 1, B); q->start_processing(q);
            const size_t n = static_cast<size_t>(34.0 * fs);
            double sum = 0; size_t cnt = 0; bool sounding = true;
            for (size_t off = 0; off < n; off += B) {
                InEvents ie;
                if (off == 0) ie.push(note(CLAP_EVENT_NOTE_ON, 0, 60, 0.9, 11));
                clap_input_events_t in{&ie, InEvents::size, InEvents::get};
                OutEvents oe; clap_output_events_t o{&oe, OutEvents::tryPush};
                clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &in; pr.out_events = &o;
                q->process(q, &pr);
                for (uint32_t i = 0; i < B; ++i) {
                    const double t = (off + i) / fs;
                    if (t >= a && t < b) { sum += L[i] * L[i] + R[i] * R[i]; cnt += 2; }
                    if (t >= 25.0 && t < 29.0 && L[i] == 0.0f && R[i] == 0.0f && i > 0 && L[i - 1] == 0.0f) sounding = false;   // two zero samples in a row
                }
            }
            q->stop_processing(q); q->deactivate(q);
            return std::make_pair(10 * std::log10(sum / std::max<size_t>(cnt, 1) + 1e-30), sounding);
        };
        setenv("SW_LICENSE_TEST_DEMO", "1", 1);
        const clap_plugin_t* q = fac->create_plugin(fac, &kHost, d->id);
        q->init(q);
        const auto gap = heldRms(q, 30.5, 32.5), before = heldRms(q, 25.0, 29.0);
        unsetenv("SW_LICENSE_TEST_DEMO");
        const auto clean = heldRms(q, 30.5, 32.5);
        q->destroy(q);
        std::printf("      demo: a held note 25..29 s %.1f dBFS, 30.5..32.5 s %.1f dBFS; without the switch 30.5..32.5 s %.1f dBFS\n", before.first, gap.first, clean.first);
        check(before.first > -60.0 && before.second && gap.first < -200.0, "without a licence the sound stops for 3 s at 30 s (and plays before it)");
        check(clean.first > -60.0, "a development build plays without the silence");
    }
    p->stop_processing(p); p->deactivate(p); p->destroy(p); entry->deinit();
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
