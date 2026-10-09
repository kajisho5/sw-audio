// Host smoke test: load every built .clap the way a DAW does (dlopen -> clap_entry), push noise through it and check
// what the unit tests and the two validators cannot see: that the plug-in layer routes parameters to the right place.
//   - NaN / Inf / absurd peaks at the defaults                                   -> FAIL
//   - an "Output" gain parameter that does not give the gain it prints (+12 dB)  -> FAIL when the gain is too large
//     (the gain was applied twice, e.g. the Shell and the core both applied it), WARN when too small
//   - the host's Bypass parameter that does not return the input level            -> FAIL
//   - a Mix parameter at 0 % that is not the (latency-matched) input              -> FAIL
//   - a panel "In" switch that does not return the input level when Off           -> FAIL
//   - a plug-in that is a bit-exact pass-through at its defaults                  -> WARN (meters / analysers are expected)
//   - the plug-in window's page messages (sw_message.h, no window needed): the first poll is a well-formed update, presets save / load / delete in a temporary HOME,
//     and for UT03 / RV04 a file sent in base64 pieces the way the page does arrives in the core (length and load counters in the read-outs), EQ07's Auto thresh button call reaches the audio thread -> FAIL
//   - processing time per second of audio                                         -> printed only (CI runners are noisy)
// Usage: sw-host-smoke <dir-or-.clap> [...]      (Linux; exit code 1 when any product FAILs)
#include "sw_message.h"

#include <clap/clap.h>

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kSr = 48000.0;
constexpr uint32_t kBlock = 256;
constexpr int kPhaseBlocks = 563;   // ~3 s
constexpr int kMeasureBlocks = 188; // last ~1 s of a phase

// ---- minimal host
const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostNop(const clap_host_t*) {}
clap_host_t gHost = {CLAP_VERSION_INIT, nullptr, "sw-host-smoke", "SEVENTHWELL", "", "1", hostGetExtension, hostNop, hostNop, hostNop};

// ---- events
struct EventList {
    std::vector<clap_event_param_value_t> ev;
    clap_input_events_t in{};
    EventList() {
        in.ctx = this;
        in.size = [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<EventList*>(l->ctx)->ev.size()); };
        in.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
            auto* self = static_cast<EventList*>(l->ctx);
            return i < self->ev.size() ? &self->ev[i].header : nullptr;
        };
    }
    void set(clap_id id, double value) {
        clap_event_param_value_t e{};
        e.header.size = sizeof(e); e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
        e.param_id = id; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = value;
        ev.push_back(e);
    }
};
bool outTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    float next() {   // uniform in [-1, 1)
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<float>(static_cast<double>(s >> 11) / 4503599627370496.0 - 1.0);   // 2^52
    }
};

double rmsDb(const std::vector<float>& x, size_t from, size_t to) {
    double a = 0;
    for (size_t i = from; i < to; ++i) a += static_cast<double>(x[i]) * x[i];
    a /= static_cast<double>(std::max<size_t>(1, to - from));
    return 10.0 * std::log10(std::max(a, 1e-30));
}

struct Result {
    std::string code, name;
    bool fail = false;
    std::vector<std::string> notes;       // FAIL / WARN lines
    double latency = 0, cpuPct = 0, outDeltaWanted = 0, outDeltaGot = 0;
    bool hasOut = false, passThrough = false, hasBypass = false, hasMix = false, hasIn = false;
};

struct Run {
    const clap_plugin_t* p = nullptr;
    uint32_t inCh[2] = {2, 0};
    uint32_t nIn = 1;
    std::vector<float> in[2], out[2], sc[2];
    std::vector<float> inAll[2], outAll[2];
    bool bad = false;
    double peak = 0;
    double seconds = 0, audioSeconds = 0;

    // processes `blocks` blocks of noise (seeded), appending to inAll/outAll; events go into the first block
    void process(int blocks, uint64_t seed, EventList& events) {
        Rng rng(seed);
        for (int b = 0; b < blocks; ++b) {
            for (int c = 0; c < 2; ++c) {
                for (uint32_t i = 0; i < kBlock; ++i) {
                    const float x = rng.next() * 0.1732f;   // uniform noise, ~ -20 dBFS RMS
                    in[c][i] = x; sc[c][i] = x * 0.5f; out[c][i] = 0.f;
                }
            }
            float* inPtr[2] = {in[0].data(), in[1].data()};
            float* scPtr[2] = {sc[0].data(), sc[1].data()};
            float* outPtr[2] = {out[0].data(), out[1].data()};
            clap_audio_buffer_t ib[2]{};
            ib[0].channel_count = 2; ib[0].data32 = inPtr;
            ib[1].channel_count = inCh[1] ? inCh[1] : 2; ib[1].data32 = scPtr;
            clap_audio_buffer_t ob{};
            ob.channel_count = 2; ob.data32 = outPtr;
            clap_output_events_t oe{nullptr, outTryPush};
            clap_process_t pr{};
            pr.steady_time = -1; pr.frames_count = kBlock; pr.transport = nullptr;
            pr.audio_inputs = ib; pr.audio_inputs_count = nIn;
            pr.audio_outputs = &ob; pr.audio_outputs_count = 1;
            pr.in_events = &events.in; pr.out_events = &oe;
            const auto t0 = std::chrono::steady_clock::now();
            const clap_process_status st = p->process(p, &pr);
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            audioSeconds += kBlock / kSr;
            if (st == CLAP_PROCESS_ERROR) bad = true;
            events.ev.clear();
            for (int c = 0; c < 2; ++c) {
                for (uint32_t i = 0; i < kBlock; ++i) {
                    const float y = out[c][i];
                    if (!std::isfinite(y)) bad = true; else peak = std::max(peak, static_cast<double>(std::fabs(y)));
                    inAll[c].push_back(in[c][i]); outAll[c].push_back(y);
                }
            }
        }
    }
    void clearLogs() { for (int c = 0; c < 2; ++c) { inAll[c].clear(); outAll[c].clear(); } }
};

bool parsePlain(const char* s, double& v) { return std::sscanf(s, "%lf", &v) == 1; }

// finds the host parameter whose text for +12 dB (or the nearest smaller) parses; returns the host value and the dB it stands for
bool outputStep(const clap_plugin_params_t* pe, const clap_plugin_t* p, clap_id id, double defHost, double& hostValue, double& deltaDb) {
    char buf[64];
    if (!pe->value_to_text(p, id, defHost, buf, sizeof(buf))) return false;
    double defPlain = 0;
    if (!parsePlain(buf, defPlain)) return false;
    for (const char* t : {"12 dB", "10 dB", "6 dB", "3 dB"}) {
        double hv = 0;
        char txt[32]; std::snprintf(txt, sizeof(txt), "%+.1f dB", defPlain + std::atof(t));
        if (!pe->text_to_value(p, id, txt, &hv)) continue;
        if (!pe->value_to_text(p, id, hv, buf, sizeof(buf))) continue;
        double plain = 0;
        if (!parsePlain(buf, plain)) continue;
        if (plain - defPlain < 2.5) continue;
        hostValue = hv; deltaDb = plain - defPlain;
        return true;
    }
    return false;
}


// ---- the page's messages (sw_message.h)
// the arrays of an update script SWHOST.update([values],lat,cpu,[meters],[spectrum],[readouts],[stereo]): each top-level [...] as numbers
std::vector<std::vector<double>> updateArrays(const std::string& s) {
    std::vector<std::vector<double>> out;
    size_t i = 0;
    while ((i = s.find('[', i)) != std::string::npos) {
        const size_t e = s.find(']', i); if (e == std::string::npos) break;
        std::vector<double> v; const std::string body = s.substr(i + 1, e - i - 1); size_t k = 0;
        while (k < body.size()) { char* end = nullptr; const double x = std::strtod(body.c_str() + k, &end); if (end == body.c_str() + k) break; v.push_back(x); k = static_cast<size_t>(end - body.c_str()); if (k < body.size() && body[k] == ',') ++k; }
        out.push_back(v); i = e + 1;
    }
    return out;
}
std::string b64(const std::vector<uint8_t>& v, size_t a, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o;
    for (size_t i = a; i < a + n; i += 3) { const uint32_t x = (uint32_t(v[i]) << 16) | (i + 1 < a + n ? uint32_t(v[i + 1]) << 8 : 0) | (i + 2 < a + n ? v[i + 2] : 0); o += T[x >> 18]; o += T[(x >> 12) & 63]; o += i + 1 < a + n ? T[(x >> 6) & 63] : '='; o += i + 2 < a + n ? T[x & 63] : '='; }
    return o;
}
// what the page sends of a file: pieces of 3 x 65536 bytes
void sendPieces(const sw_plugin_message_t* m, const clap_plugin_t* p, const char* name, const std::vector<uint8_t>& bytes) {
    const size_t piece = 3 * 65536;
    for (size_t off = 0; off < bytes.size(); off += piece) m->send(p, (std::string("c ") + name + " " + b64(bytes, off, std::min(piece, bytes.size() - off))).c_str());
}
std::vector<uint8_t> wav16Noise(double seconds, double rate) {   // 16 bit stereo WAV of a 440 Hz tone
    const size_t n = static_cast<size_t>(seconds * rate); std::vector<uint8_t> f;
    auto p32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(x >> (8 * i))); }; auto p16 = [&](uint16_t x) { f.push_back(static_cast<uint8_t>(x)); f.push_back(static_cast<uint8_t>(x >> 8)); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(t[i])); };
    tag("RIFF"); p32(static_cast<uint32_t>(36 + n * 4)); tag("WAVE"); tag("fmt "); p32(16); p16(1); p16(2); p32(static_cast<uint32_t>(rate)); p32(static_cast<uint32_t>(rate * 4)); p16(4); p16(16); tag("data"); p32(static_cast<uint32_t>(n * 4));
    for (size_t i = 0; i < n; ++i) { const int16_t v = static_cast<int16_t>(8000.0 * std::sin(6.283185307 * 440.0 * static_cast<double>(i) / rate)); p16(static_cast<uint16_t>(v)); p16(static_cast<uint16_t>(v)); }
    return f;
}
std::string codeOfId(const char* id) { std::string s = id; s = s.substr(s.rfind('.') + 1); for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
void messageChecks(const clap_plugin_t* p, const std::string& code, Run& run, EventList& ev, Result& r) {
    const auto* m = static_cast<const sw_plugin_message_t*>(p->get_extension(p, SW_EXT_MESSAGE));
    auto fail = [&](const std::string& t) { r.notes.push_back("FAIL page messages: " + t); r.fail = true; };
    if (!m) { fail("the plug-in has no message hook"); return; }
    // the first poll: a well-formed update with the right number of values
    const std::string up = m->send(p, "p");
    if (up.rfind("SWHOST.update([", 0) != 0 || up.size() < 4 || up.substr(up.size() - 2) != ");") fail("the poll is not an update script");
    if (up.find("nan") != std::string::npos || up.find("inf") != std::string::npos) fail("the update script has nan / inf");
    if (std::string(m->send(p, "garbage")).size() || std::string(m->send(p, "c")).size()) fail("a malformed message got a reply");
    // presets: save, list, load, delete in a temporary HOME (the same folder rules as the window)
    const std::string pre = std::string("c presetsave smoke%20test a.b=1;c_d=-2.5e-3;e=0");
    const std::string sv = m->send(p, pre.c_str());
    if (sv != "SWHOST.presets([\"smoke test\"],\"smoke test\");") fail("presetsave replied: " + sv);
    const std::string ld = m->send(p, "c presetload smoke%20test");
    if (ld != "SWHOST.presetLoaded(\"smoke test\",\"a.b=1;c_d=-2.5e-3;e=0\");") fail("presetload replied: " + ld);
    if (std::string(m->send(p, "c presetlist")) != "SWHOST.presets([\"smoke test\"],\"\");") fail("presetlist");
    if (std::string(m->send(p, "c presetsave bad a=<b>")).rfind("SWHOST.presetError(", 0) != 0) fail("a bad preset body was accepted");
    if (std::string(m->send(p, "c presetdelete smoke%20test")) != "SWHOST.presets([],\"\");") fail("presetdelete");
    // UT03: a reference file in pieces; RV04: an IR in pieces
    auto readouts = [&](int blocks) { run.process(blocks, 99, ev); const auto a = updateArrays(m->send(p, "p")); return a.size() > 3 ? a[3] : std::vector<double>{}; };
    if (code == "UT03") {
        const auto base = readouts(2);
        if (base.size() < 12) { fail("UT03 read-outs missing"); return; }
        const auto wav = wav16Noise(2.0, 48000.0);
        m->send(p, "c refbegin 1"); sendPieces(m, p, "refdata", wav); m->send(p, "c refend");
        const auto a = readouts(2);
        if (a.size() < 12 || std::fabs(a[4] - 2.0) > 0.01 || a[10] != base[10] + 1 || a[11] != base[11]) fail("UT03: the reference sent in pieces did not arrive (length " + std::to_string(a.size() > 4 ? a[4] : -1) + ")");
        m->send(p, "c looprange 0.25 1.25");
        m->send(p, "c refbegin 2"); m->send(p, "c refdata AAAA"); m->send(p, "c refend");   // not a file
        const auto b = readouts(2);
        if (b.size() < 12 || b[11] != a[11] + 1 || b[7] != 0.0) fail("UT03: a file that is not audio was not refused");
        m->send(p, "c refclear 1");
        const auto c = readouts(2);
        if (c.size() < 12 || c[4] != 0.0) fail("UT03: refclear left the reference");
    } else if (code == "EQ07") {   // a button call that runs on the audio thread (the queue): Auto thresh starts listening
        const auto base = readouts(2);
        if (base.size() < 2 || base[0] != 0.0) { fail("EQ07 read-outs missing or already listening"); return; }
        m->send(p, "c learn");
        const auto a = readouts(2);
        if (a.size() < 2 || a[0] != 1.0 || a[1] <= 0.0) fail("EQ07: Auto thresh did not start listening after the screen's button call");
    } else if (code == "EQ02") {   // Assist: the toggle reaches the audio thread and shows in the read-outs (the marks themselves are checked in the unit tests)
        const auto base = readouts(2);
        if (base.size() < 13 || base[0] != 0.0) { fail("EQ02 read-outs missing or Assist already on"); return; }
        m->send(p, "c assist 1");
        const auto a = readouts(2);
        if (a.size() < 13 || a[0] != 1.0) fail("EQ02: Assist did not turn on after the screen's button call");
        m->send(p, "c assist 0");
        const auto b = readouts(2);
        if (b.size() < 13 || b[0] != 0.0) fail("EQ02: Assist did not turn off");
    } else if (code == "RV04") {
        const auto base = readouts(2);
        if (base.size() < 3) { fail("RV04 read-outs missing"); return; }
        std::vector<float> ir(4800, 0.0f); ir[10] = 1.0f; ir[2400] = 0.4f;
        std::vector<uint8_t> by(ir.size() * 4); std::memcpy(by.data(), ir.data(), by.size());
        m->send(p, "c irbegin 1 48000"); sendPieces(m, p, "irdata", by); m->send(p, "c irend");
        const auto a = readouts(3);
        if (a.size() < 3 || a[0] != base[0] + 1 || a[1] != base[1] || a[2] != 1.0) fail("RV04: the IR sent in pieces did not arrive");
        m->send(p, "c irbegin 1 48000"); m->send(p, "c irdata AAAA"); m->send(p, "c irend");   // 3 bytes: not whole samples
        const auto b = readouts(1);
        if (b.size() < 3 || b[1] != a[1] + 1) fail("RV04: damaged IR data was not refused");
    }
}

Result testOne(const fs::path& path) {
    Result r;
    r.code = path.stem().string();
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { r.fail = true; r.notes.push_back(std::string("FAIL dlopen: ") + dlerror()); return r; }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry || !entry->init(path.c_str())) { r.fail = true; r.notes.push_back("FAIL no clap_entry / init"); return r; }
    auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!fac || fac->get_plugin_count(fac) < 1) { r.fail = true; r.notes.push_back("FAIL no plug-in in factory"); return r; }
    const clap_plugin_descriptor_t* d = fac->get_plugin_descriptor(fac, 0);
    r.name = d->name;
    const clap_plugin_t* p = fac->create_plugin(fac, &gHost, d->id);
    if (!p || !p->init(p) || !p->activate(p, kSr, 16, kBlock) || !p->start_processing(p)) {
        r.fail = true; r.notes.push_back("FAIL create/init/activate"); return r;
    }
    const auto* pe = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS));
    const auto* lat = static_cast<const clap_plugin_latency_t*>(p->get_extension(p, CLAP_EXT_LATENCY));
    r.latency = lat ? lat->get(p) : 0;

    Run run;
    run.p = p;
    run.nIn = ports ? std::min<uint32_t>(2, ports->count(p, true)) : 1;
    if (run.nIn > 1) {
        clap_audio_port_info_t pi{};
        if (ports->get(p, true, 1, &pi)) run.inCh[1] = std::min<uint32_t>(2, pi.channel_count);
    }
    for (int c = 0; c < 2; ++c) { run.in[c].resize(kBlock); run.out[c].resize(kBlock); run.sc[c].resize(kBlock); }

    // parameters worth knowing about
    clap_id outId = CLAP_INVALID_ID, bypassId = CLAP_INVALID_ID, mixId = CLAP_INVALID_ID, inId = CLAP_INVALID_ID;
    double outDef = 0, bypassDef = 0, mixDef = 0, inDef = 0;
    if (pe) {
        const uint32_t n = pe->count(p);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t pi{};
            if (!pe->get_info(p, i, &pi)) continue;
            const std::string nm = pi.name, mod = pi.module;
            if (mod == "common.bypass") { bypassId = pi.id; bypassDef = pi.default_value; }
            if (nm == "Mix" && std::count(mod.begin(), mod.end(), '.') == 1 && mixId == CLAP_INVALID_ID)   // <code>.mix only (CS01 comp.mix is the compressor's parallel mix)
                { mixId = pi.id; mixDef = pi.default_value; }
            if (nm == "In" && inId == CLAP_INVALID_ID) { inId = pi.id; inDef = pi.default_value; }
            const bool outName = nm == "Output" || nm == "Out" || nm == "EQ Output";
            // a plain dB gain called Output (Master of the amp products is a knob 0..10, not a gain in dB)
            if (outName && outId == CLAP_INVALID_ID) {
                char t[64];
                if (pe->value_to_text(p, pi.id, pi.default_value, t, sizeof(t)) && std::strstr(t, "dB")) { outId = pi.id; outDef = pi.default_value; }
            }
        }
    }

    EventList ev;
    // ---- phase A: defaults
    run.process(kPhaseBlocks, 1234, ev);
    const size_t nA = run.outAll[0].size();
    const size_t fromA = nA - static_cast<size_t>(kMeasureBlocks) * kBlock;
    const double inDb = rmsDb(run.inAll[0], fromA, nA);
    const double outDbA = rmsDb(run.outAll[0], fromA, nA);
    r.cpuPct = 100.0 * run.seconds / std::max(1e-9, run.audioSeconds);

    // bit-exact pass-through? (input delayed by the reported latency)
    {
        const size_t lt = static_cast<size_t>(std::max(0.0, r.latency));
        double maxDiff = 0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = fromA; i < nA; ++i)
                maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(run.outAll[c][i] - (i >= lt ? run.inAll[c][i - lt] : 0.f))));
        r.passThrough = maxDiff < 1e-6;
    }
    const double peakA = run.peak;

    // ---- phase B: Output +12 dB
    if (outId != CLAP_INVALID_ID) {
        double hv = 0, delta = 0;
        if (outputStep(pe, p, outId, outDef, hv, delta)) {
            run.clearLogs();
            ev.set(outId, hv);
            run.process(kPhaseBlocks, 1234, ev);
            const size_t nB = run.outAll[0].size();
            const double outDbB = rmsDb(run.outAll[0], nB - static_cast<size_t>(kMeasureBlocks) * kBlock, nB);
            r.hasOut = true; r.outDeltaWanted = delta; r.outDeltaGot = outDbB - outDbA;
            const double err = r.outDeltaGot - delta;
            char b[160];
            if (err > 3.0) {
                std::snprintf(b, sizeof(b), "FAIL Output +%.0f dB gave %+.1f dB (the gain is applied more than once?)", delta, r.outDeltaGot);
                r.notes.push_back(b); r.fail = true;
            } else if (err < -3.0) {
                std::snprintf(b, sizeof(b), "WARN Output +%.0f dB gave only %+.1f dB", delta, r.outDeltaGot);
                r.notes.push_back(b);
            }
            ev.set(outId, outDef);   // back to default
            run.process(kPhaseBlocks / 2, 1234, ev);
        }
    }

    // ---- phase C: host Bypass returns the input level
    if (bypassId != CLAP_INVALID_ID) {
        r.hasBypass = true;
        run.clearLogs();
        ev.set(bypassId, 1.0);
        run.process(kPhaseBlocks, 1234, ev);
        const size_t nC = run.outAll[0].size();
        const size_t fromC = nC - static_cast<size_t>(kMeasureBlocks) * kBlock;
        const double d = rmsDb(run.outAll[0], fromC, nC) - rmsDb(run.inAll[0], fromC, nC);
        if (std::fabs(d) > 0.5) {
            char b[160]; std::snprintf(b, sizeof(b), "FAIL Bypass changes the level by %+.2f dB", d);
            r.notes.push_back(b); r.fail = true;
        }
        ev.set(bypassId, bypassDef);
        run.process(8, 1234, ev);
    }

    // ---- phase D: Mix 0 % is the input, delayed by the reported latency
    if (mixId != CLAP_INVALID_ID) {
        double hv = 0;
        if (pe->text_to_value(p, mixId, "0", &hv)) {
            r.hasMix = true;
            run.clearLogs();
            ev.set(mixId, hv);
            run.process(kPhaseBlocks, 1234, ev);
            const size_t nD = run.outAll[0].size(), lt = static_cast<size_t>(std::max(0.0, r.latency));
            double maxDiff = 0;
            for (int c = 0; c < 2; ++c)
                for (size_t i = nD - static_cast<size_t>(kMeasureBlocks) * kBlock; i < nD; ++i)
                    maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(run.outAll[c][i] - run.inAll[c][i - lt])));
            if (maxDiff > 1e-3) {
                char b[160]; std::snprintf(b, sizeof(b), "FAIL Mix 0 %% is not the input (max difference %.4f, latency %.0f)", maxDiff, r.latency);
                r.notes.push_back(b); r.fail = true;
            }
            ev.set(mixId, mixDef);
            run.process(8, 1234, ev);
        }
    }

    // ---- phase E: the panel's In switch Off returns the input level
    if (inId != CLAP_INVALID_ID) {
        r.hasIn = true;
        run.clearLogs();
        ev.set(inId, 0.0);
        run.process(kPhaseBlocks, 1234, ev);
        const size_t nE = run.outAll[0].size();
        const size_t fromE = nE - static_cast<size_t>(kMeasureBlocks) * kBlock;
        const double d = rmsDb(run.outAll[0], fromE, nE) - rmsDb(run.inAll[0], fromE, nE);
        if (std::fabs(d) > 0.5) {
            char b[160]; std::snprintf(b, sizeof(b), "FAIL In Off changes the level by %+.2f dB", d);
            r.notes.push_back(b); r.fail = true;
        }
        ev.set(inId, inDef);
        run.process(8, 1234, ev);
    }

    if (run.bad) { r.notes.push_back("FAIL NaN / Inf in the output (or process() returned an error)"); r.fail = true; }
    if (peakA > 8.0) {
        char b[96]; std::snprintf(b, sizeof(b), "FAIL peak %.1f at defaults for a -20 dBFS input", peakA);
        r.notes.push_back(b); r.fail = true;
    }
    if (r.passThrough) r.notes.push_back("NOTE bit-exact pass-through at the defaults");
    (void)inDb;

    messageChecks(p, codeOfId(d->id), run, ev, r);

    p->stop_processing(p); p->deactivate(p); p->destroy(p);
    entry->deinit();
    dlclose(lib);
    return r;
}

}   // namespace

int main(int argc, char** argv) {
    // the preset checks write into the person's home folder: a temporary one
    const fs::path tmpHome = fs::temp_directory_path() / ("sw-host-smoke-home-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpHome); setenv("HOME", tmpHome.c_str(), 1);
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        const fs::path a = argv[i];
        if (fs::is_directory(a) && a.extension() != ".clap") {
            for (auto& e : fs::directory_iterator(a))
                if (e.path().extension() == ".clap" && fs::is_regular_file(e.path())) files.push_back(e.path());
        } else {
            files.push_back(a);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::fprintf(stderr, "usage: %s <dir|file.clap> ...\n", argv[0]); return 2; }

    int fails = 0, warns = 0, pass = 0, outTested = 0, bypassTested = 0, mixTested = 0, inTested = 0;
    std::vector<std::string> passNames;
    double worstCpu = 0; std::string worstCpuName;
    for (const auto& f : files) {
        Result r = testOne(f);
        for (const auto& n : r.notes) if (n.rfind("WARN", 0) == 0) { ++warns; break; }
        if (r.fail) ++fails;
        if (r.hasOut) ++outTested;
        if (r.hasBypass) ++bypassTested;
        if (r.hasMix) ++mixTested;
        if (r.hasIn) ++inTested;
        if (r.passThrough) passNames.push_back(r.name);
        if (r.cpuPct > worstCpu) { worstCpu = r.cpuPct; worstCpuName = r.name; }
        std::printf("%-5s %-28s lat %6.0f  cpu %5.1f%%  out %s%s\n", r.fail ? "FAIL" : "ok", r.name.c_str(), r.latency, r.cpuPct,
                    r.hasOut ? ("+" + std::to_string(static_cast<int>(r.outDeltaWanted)) + " dB -> " + std::to_string(r.outDeltaGot).substr(0, 5)).c_str() : "-",
                    r.hasBypass ? "  bypass ok" : "");
        for (const auto& n : r.notes) std::printf("        %s\n", n.c_str());
        if (!r.fail) ++pass;
    }
    std::printf("\n%zu plug-ins: %d ok, %d FAIL, %d with warnings; Output gain checked on %d, Bypass on %d, Mix 0 %% on %d, In Off on %d; slowest %s (%.1f%% of one core, noisy)\n",
                files.size(), pass, fails, warns, outTested, bypassTested, mixTested, inTested, worstCpuName.c_str(), worstCpu);
    std::printf("bit-exact pass-through at the defaults (%zu):", passNames.size());
    for (const auto& n : passNames) std::printf(" [%s]", n.c_str());
    std::printf("\n");
    std::error_code ec; fs::remove_all(tmpHome, ec);
    return fails ? 1 : 0;
}
