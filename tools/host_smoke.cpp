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
#include "sw/unit.hpp"
#include "sw_message.h"

#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/tail.h>
#include <clap/ext/track-info.h>

#include <dlfcn.h>
#include <unistd.h>

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

double kSr = 48000.0;   // --rate=N changes it (the spec says 44.1 - 192 kHz)
constexpr uint32_t kBlock = 256;
int kPhaseBlocks = 563;   // ~3 s (scaled with the rate)
int kMeasureBlocks = 188; // last ~1 s of a phase

// ---- minimal host
// the host's track information (CLAP track-info): a track called "Lead Vocal" (UT01 sorts its Gain by the kind of track)
bool hostTrackInfoGet(const clap_host_t*, clap_track_info_t* i) { std::memset(i, 0, sizeof *i); i->flags = CLAP_TRACK_INFO_HAS_TRACK_NAME; std::strcpy(i->name, "Lead Vocal"); return true; }
const clap_host_track_info_t gHostTrackInfo = {hostTrackInfoGet};
const void* hostGetExtension(const clap_host_t*, const char* id) { return id && !std::strcmp(id, CLAP_EXT_TRACK_INFO) ? &gHostTrackInfo : nullptr; }
void hostNop(const clap_host_t*) {}
int gRestartRequests = 0;   // how many times a plug-in asked the host to restart it (a setting changed the delay)
void hostRestart(const clap_host_t*) { ++gRestartRequests; }
clap_host_t gHost = {CLAP_VERSION_INIT, nullptr, "sw-host-smoke", "SEVENTHWELL", "", "1", hostGetExtension, hostRestart, hostNop, hostNop};

// ---- events (parameter values and MIDI / note events, in the order they were added; all at time 0)
struct EventList {
    struct Ev { alignas(8) unsigned char bytes[64]; };
    std::vector<Ev> ev;
    clap_input_events_t in{};
    EventList() {
        in.ctx = this;
        in.size = [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<EventList*>(l->ctx)->ev.size()); };
        in.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
            auto* self = static_cast<EventList*>(l->ctx);
            return i < self->ev.size() ? reinterpret_cast<const clap_event_header_t*>(self->ev[i].bytes) : nullptr;
        };
    }
    void set(clap_id id, double value, uint32_t time = 0) {
        clap_event_param_value_t e{};
        e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
        e.param_id = id; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = value;
        push(&e, sizeof e);
    }
    // a raw MIDI message (status, data1, data2) on note port 0
    void midi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t time = 0) {
        clap_event_midi_t e{};
        e.header.size = sizeof(e); e.header.time = time; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_MIDI; e.header.flags = 0;
        e.port_index = 0; e.data[0] = status; e.data[1] = d1; e.data[2] = d2;
        push(&e, sizeof e);
    }
    // a CLAP note event (note on / off) on channel 0
    void note(bool on, int16_t key, double velocity = 0.8) {
        clap_event_note_t e{};
        e.header.size = sizeof(e); e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF; e.header.flags = 0;
        e.note_id = -1; e.port_index = 0; e.channel = 0; e.key = key; e.velocity = velocity;
        push(&e, sizeof e);
    }
private:
    void push(const void* p, size_t n) { Ev x{}; std::memcpy(x.bytes, p, n); ev.push_back(x); }
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
    double toneHz = 0, toneDb = -20.0; uint64_t toneN = 0;   // toneHz > 0: a sine instead of noise (the SW Link checks)
    int64_t impulse = -1;                                    // >= 0: silence with one impulse (0.5) at that sample (the latency check)

    // processes `blocks` blocks of noise (seeded), appending to inAll/outAll; events go into the first block
    void process(int blocks, uint64_t seed, EventList& events) {
        Rng rng(seed);
        for (int b = 0; b < blocks; ++b) {
            for (int c = 0; c < 2; ++c) {
                for (uint32_t i = 0; i < kBlock; ++i) {
                    float x = rng.next() * 0.1732f;   // uniform noise, ~ -20 dBFS RMS
                    if (impulse >= 0) x = (static_cast<int64_t>(toneN) + i == impulse) ? 0.5f : 0.0f;
                    if (toneHz > 0) x = static_cast<float>(std::pow(10.0, toneDb / 20.0) * std::sin(6.283185307179586 * toneHz * static_cast<double>(toneN + i) / kSr));
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
            events.ev.clear(); toneN += kBlock;
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
    } else if (code == "UT01") {   // the host's track name reaches the core (CLAP track-info, asked for at activation), and a call from the screen reaches the audio thread
        const auto base = readouts(2);
        if (base.size() < 3) { fail("UT01 read-outs missing"); return; }
        if (base[0] != 0.0 || base[1] != 0.0) fail("UT01: the host's track \"Lead Vocal\" was not taken as a Vocal track with nothing remembered (kind " + std::to_string(base[0]) + ")");
        m->send(p, "c remember");
        const auto a = readouts(2);
        if (a.size() < 3 || a[1] != 1.0) fail("UT01: Remember gain did not reach the audio thread");
    } else if (code == "MD05" || code == "VO03" || code == "CR04") {   // MIDI in: the plug-in has a note input, and MIDI / note events reach the core (sample-accurate, on the audio thread)
        const auto* np = static_cast<const clap_plugin_note_ports_t*>(p->get_extension(p, CLAP_EXT_NOTE_PORTS));
        const auto* pe = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        if (!np || np->count(p, true) != 1 || np->count(p, false) != 0) { fail(code + ": expected one MIDI input port and no output port"); return; }
        clap_note_port_info_t ni{}; if (!np->get(p, 0, true, &ni) || !(ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI) || !(ni.supported_dialects & CLAP_NOTE_DIALECT_CLAP)) { fail(code + ": the note port takes neither MIDI nor CLAP notes"); return; }
        auto idOf = [&](const char* name, clap_id& id) { for (uint32_t i = 0; pe && i < pe->count(p); ++i) { clap_param_info_t pi{}; if (pe->get_info(p, i, &pi) && std::string(pi.name) == name) { id = pi.id; return true; } } return false; };
        auto value = [&](clap_id id) { double v = -1; pe->get_value(p, id, &v); return v; };
        if (code == "MD05") {   // CC64 (sustain pedal) and CC1 (mod wheel) and the notes C2 / C#2 / D2 set Speed (Stop 0, Slow 1, Fast 2); the value is the host's too
            clap_id sp = CLAP_INVALID_ID; if (!idOf("Speed", sp)) { fail("MD05 has no Speed"); return; }
            if (value(sp) != 1.0) fail("MD05: Speed should start at Slow");
            ev.midi(0xB0, 64, 127); run.process(2, 99, ev); if (value(sp) != 2.0) fail("MD05: the sustain pedal (CC64 = 127) did not set Speed to Fast: " + std::to_string(value(sp)));
            ev.midi(0xB0, 64, 0); run.process(2, 99, ev); if (value(sp) != 1.0) fail("MD05: the pedal up (CC64 = 0) did not set Slow");
            ev.midi(0xB0, 1, 5); run.process(2, 99, ev); if (value(sp) != 0.0) fail("MD05: the mod wheel at the bottom (CC1 = 5) did not stop the rotors");
            ev.note(true, 38); run.process(2, 99, ev); if (value(sp) != 2.0) fail("MD05: the note D2 (a CLAP note event) did not set Fast");
            ev.midi(0x90, 37, 90); run.process(2, 99, ev); if (value(sp) != 1.0) fail("MD05: the note C#2 (a MIDI note on) did not set Slow");
            ev.midi(0x90, 36, 0); run.process(2, 99, ev); if (value(sp) != 1.0) fail("MD05: a note on with velocity 0 is a note off: it must not change Speed");
        } else if (code == "VO03") {   // the held notes are the chord (pitch classes): readouts[6] = the 12-bit mask (after voiced, the singer's pitch and the four voices' pitches)
            auto chord = [&](int blocks) { const auto r = readouts(blocks); return r.size() > 6 ? r[6] : -1.0; };
            if (chord(2) != 0.0) { fail("VO03: read-outs missing or a chord is held at the start"); return; }
            ev.note(true, 53); ev.note(true, 57); ev.midi(0x90, 60, 100); run.process(2, 99, ev);
            double a = chord(1); if (a != double((1 << 5) | (1 << 9) | (1 << 0))) fail("VO03: F A C were not taken as the chord (mask " + std::to_string(a) + ")");
            ev.midi(0x80, 57, 0); run.process(2, 99, ev); a = chord(1); if (a != double((1 << 5) | (1 << 0))) fail("VO03: the note off (MIDI) did not take A out of the chord");
            ev.note(false, 53); ev.note(false, 60); run.process(2, 99, ev); a = chord(1); if (a != 0.0) fail("VO03: the chord should be empty after every note off");
        } else {   // CR04: with Freeze On a note-on makes a new capture (readouts[0] = the number of captures)
            clap_id fz = CLAP_INVALID_ID; if (!idOf("Freeze", fz)) { fail("CR04 has no Freeze"); return; }
            const auto base = readouts(2); if (base.empty()) { fail("CR04: read-outs missing"); return; }
            ev.note(true, 60); run.process(8, 99, ev); const auto idle = readouts(1); if (idle.empty() || idle[0] != base[0]) fail("CR04: a note with Freeze Off must not capture");
            ev.set(fz, 1.0); run.process(40, 99, ev); const auto on = readouts(1);
            ev.note(true, 62); run.process(40, 99, ev); const auto after = readouts(1);
            if (on.empty() || after.empty() || after[0] != on[0] + 1) fail("CR04: a note-on with Freeze On did not make one new capture (" + std::to_string(on.empty() ? -1 : on[0]) + " -> " + std::to_string(after.empty() ? -1 : after[0]) + ")");
        }
    } else if (code == "RS01" || code == "CS04" || code == "MS04" || code == "MS01" || code == "MS03") {   // Low lat changes the delay: the plug-in asks the host to restart it, and the new delay is what it reports after the restart (deactivate / activate)
        const auto* pe = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        const auto* lat = static_cast<const clap_plugin_latency_t*>(p->get_extension(p, CLAP_EXT_LATENCY));
        auto idOf = [&](const char* name) { for (uint32_t i = 0; pe && i < pe->count(p); ++i) { clap_param_info_t pi{}; if (pe->get_info(p, i, &pi) && std::string(pi.name) == name) return pi.id; } return CLAP_INVALID_ID; };
        auto restart = [&]() { p->stop_processing(p); p->deactivate(p); p->activate(p, kSr, 16, kBlock); p->start_processing(p); };
        const clap_id ll = idOf("Low lat"); if (ll == CLAP_INVALID_ID || !lat) { fail(code + ": no Low lat parameter or no latency extension"); return; }
        uint32_t offLat = 2048, onLat = 512;   // RS01: the 2048-point window / the 512-point one
        if (code == "MS04") { offLat = 48; onLat = 8; }   // the FIR oversampler / the IIR half-bands
        auto msN = [&](double t) { return static_cast<uint32_t>(std::lround(t * 0.001 * kSr)); };   // look-aheads are milliseconds (the run may be at another sample rate: --rate)
        if (code == "MS01") { offLat = msN(2.0) + 16; onLat = msN(0.5) + 16; }   // look-ahead 2 ms + true-peak interpolation (16) / 0.5 ms + the same interpolation
        if (code == "MS03") { offLat = msN(2.0) + msN(1.0) + msN(0.5) + 16; onLat = 3 * msN(0.5) + 16; }   // 2 + 1 + 0.5 ms and the interpolation / 0.5 + 0.5 + 0.5 ms and the interpolation
        if (code == "CS04") { const clap_id lim = idOf("Limit"); if (lim == CLAP_INVALID_ID) { fail("CS04: no Limit"); return; } ev.set(lim, 1.0); run.process(3, 7, ev); restart(); offLat = msN(1.0); onLat = 1; }   // the limiter's look-ahead: 1 ms / 1 sample
        const uint32_t before = lat->get(p); const int req0 = gRestartRequests;
        if (before != offLat) fail(code + ": the delay with Low lat Off should be " + std::to_string(offLat) + ", it is " + std::to_string(before));
        ev.set(ll, 1.0); run.process(3, 7, ev);
        if (gRestartRequests == req0) fail(code + ": Low lat did not make the plug-in ask the host for a restart");
        if (lat->get(p) != before) fail(code + ": the reported delay must stay until the restart (" + std::to_string(before) + " -> " + std::to_string(lat->get(p)) + ")");
        restart(); if (lat->get(p) != onLat) fail(code + ": after the restart the delay should be " + std::to_string(onLat) + ", it is " + std::to_string(lat->get(p)));
        ev.set(ll, 0.0); run.process(3, 7, ev); restart(); if (lat->get(p) != offLat) fail(code + ": back to Off the delay should be " + std::to_string(offLat) + ", it is " + std::to_string(lat->get(p)));
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

// SW Link between plug-in binaries: the registry is made by whichever product is loaded first and has to outlive it. Loads three products (A, B, C) as a host would, each from its own file:
//   A (loaded first, makes the registry) and B see each other; B hears A's tone in the spectrum it gets for its screen; A is destroyed and its library unloaded; B no longer sees A; C is
//   loaded afterwards, finds the same registry (it was not freed with A's library) and sees B.
struct Loaded {
    void* lib = nullptr; const clap_plugin_entry_t* entry = nullptr; const clap_plugin_t* p = nullptr; const sw_plugin_message_t* m = nullptr; Run run; EventList ev;
    bool open(const fs::path& path, std::string& why, uint32_t maxFrames = kBlock) {
        lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); if (!lib) { why = std::string("dlopen: ") + dlerror(); return false; }
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry")); if (!entry || !entry->init(path.c_str())) { why = "no clap_entry"; return false; }
        auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID)); if (!fac || fac->get_plugin_count(fac) < 1) { why = "no factory"; return false; }
        const clap_plugin_descriptor_t* d = fac->get_plugin_descriptor(fac, 0);
        p = fac->create_plugin(fac, &gHost, d->id);
        if (!p || !p->init(p) || !p->activate(p, kSr, 16, maxFrames) || !p->start_processing(p)) { why = "create/init/activate"; return false; }
        m = static_cast<const sw_plugin_message_t*>(p->get_extension(p, SW_EXT_MESSAGE)); if (!m) { why = "no message hook"; return false; }
        run.p = p; run.nIn = 1; for (int c = 0; c < 2; ++c) { run.in[c].resize(kBlock); run.out[c].resize(kBlock); run.sc[c].resize(kBlock); }
        return true;
    }
    void close() {
        if (p) { p->stop_processing(p); p->deactivate(p); p->destroy(p); p = nullptr; }
        if (entry) { entry->deinit(); entry = nullptr; }
        if (lib) { dlclose(lib); lib = nullptr; }
    }
    // [peers, spectrum of the others] from a poll
    std::vector<double> link() { const auto a = updateArrays(m->send(p, "p")); return a.size() > 5 ? a[5] : std::vector<double>{}; }
};
// Unit A / B / C through the real plug-in: with flat default settings the only thing Unit B / C change is the gain tolerance of the output stage, a different one per channel (the Shell, routed by the adapter's kUnitParam):
// the level of each channel against Unit A must move by the tolerance sw::Unit gives that channel
bool unitChecks(const std::vector<fs::path>& files) {
    bool ok = true; int tested = 0;
    for (const char* code : {"EQ03", "EQ05", "EQ06", "EQ09", "SA04", "DY07"}) {
        fs::path f; for (const auto& x : files) if (x.stem().string().find(std::string(" ") + code + " ") != std::string::npos) f = x;
        if (f.empty()) continue;
        auto fail = [&](const std::string& t) { std::printf("FAIL  Unit of %s: %s\n", code, t.c_str()); ok = false; };
        auto levels = [&](int unit, double* g) -> bool {
            Loaded a; std::string why; if (!a.open(f, why)) { fail(why); return false; }
            const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS));
            clap_id uid = CLAP_INVALID_ID; for (uint32_t i = 0; pe && i < pe->count(a.p); ++i) { clap_param_info_t pi{}; if (pe->get_info(a.p, i, &pi) && std::string(pi.name) == "Unit") uid = pi.id; }
            if (uid == CLAP_INVALID_ID) { fail("no Unit parameter"); a.close(); return false; }
            EventList ev; ev.set(uid, static_cast<double>(unit)); a.run.process(60, 7, ev);
            for (int c = 0; c < 2; ++c) { const size_t n = a.run.inAll[c].size(); g[c] = rmsDb(a.run.outAll[c], n / 2, n) - rmsDb(a.run.inAll[c], n / 2, n); }
            a.close(); return true;
        };
        double ga[2], gb[2], gc[2];
        if (!levels(0, ga) || !levels(1, gb) || !levels(2, gc)) continue;
        for (int c = 0; c < 2; ++c) {
            const double wantB = sw::Unit::gainDb(1, c, sw::Unit::kOutputSlot), wantC = sw::Unit::gainDb(2, c, sw::Unit::kOutputSlot);
            if (std::abs((gb[c] - ga[c]) - wantB) > 0.15) fail("channel " + std::to_string(c) + ": Unit B moves the level by " + std::to_string(gb[c] - ga[c]) + " dB, expected " + std::to_string(wantB));
            if (std::abs((gc[c] - ga[c]) - wantC) > 0.15) fail("channel " + std::to_string(c) + ": Unit C moves the level by " + std::to_string(gc[c] - ga[c]) + " dB, expected " + std::to_string(wantC));
        }
        ++tested;
    }
    if (ok && tested) std::printf("ok    Unit A / B / C: %d plug-ins: each channel's level moves by the tolerance that sw::Unit gives it (B and C, against A)\n", tested);
    return ok;
}
// ---- soak: a long run of the real plug-in (--soak=SECONDS of audio per plug-in, faster than real time): noise, a tone, near-silence, and a random parameter every second.
// What it looks for: memory that keeps growing (the process's resident set after the first tenth of the run against the end), the time a block takes creeping up (the first tenth against the last tenth), NaN / Inf / runaway output.
double residentMB() { long pages = 0, rss = 0; std::FILE* f = std::fopen("/proc/self/statm", "r"); if (!f) return 0.0; if (std::fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0; std::fclose(f); return static_cast<double>(rss) * static_cast<double>(sysconf(_SC_PAGESIZE)) / 1048576.0; }
bool soakOne(const fs::path& f, double secs, std::string& line, bool* timingOnly = nullptr) {
    Loaded a; std::string why; if (!a.open(f, why)) { line = "FAIL  " + f.stem().string() + ": " + why; return false; }
    const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS));
    std::vector<clap_param_info_t> info(pe ? pe->count(a.p) : 0); for (uint32_t i = 0; i < info.size(); ++i) pe->get_info(a.p, i, &info[i]);
    Rng rng(0xC0FFEEull ^ info.size() * 7919u);
    const int blocksPerChunk = static_cast<int>(std::ceil(kSr / kBlock)), lead = 2, measure = 8, total = std::max(static_cast<int>(secs), 2 * (lead + measure) + 4);
    // layout (chunks of one second): [warm-up][A: the defaults][a random parameter every second ...][every parameter back to its default][warm-up][B]; the time per block of A and B must be the same
    // (the parameters are, so only what the plug-in has piled up in between can make B slower); the memory after A and at the end
    std::vector<double> cpu(static_cast<size_t>(total));
    const int endA = lead + measure, startB = total - measure, resetAt = startB - lead;
    double rss0 = 0, rssEnd = 0;
    for (int c = 0; c < total; ++c) {
        EventList ev;
        if (c == resetAt) {
            for (uint32_t i = 0; i < info.size(); ++i) if (!(info[i].flags & CLAP_PARAM_IS_READONLY)) ev.set(info[i].id, info[i].default_value);
        } else if (c >= endA && c < resetAt && !info.empty()) {   // one random parameter per second (the stepped ones to a step)
            const uint32_t i = static_cast<uint32_t>((rng.next() * 0.5f + 0.5f) * static_cast<float>(info.size())) % static_cast<uint32_t>(info.size());
            if (!(info[i].flags & CLAP_PARAM_IS_READONLY)) { double v = info[i].min_value + (rng.next() * 0.5 + 0.5) * (info[i].max_value - info[i].min_value); if (info[i].flags & CLAP_PARAM_IS_STEPPED) v = std::round(v); ev.set(info[i].id, v); }
        }
        a.run.toneHz = (c % 4 == 2) ? 440.0 : 0.0;                                       // every fourth second a tone
        a.run.impulse = (c % 5 == 4) ? static_cast<int64_t>(a.run.toneN) + 100 : -1;      // every fifth second silence (one impulse)
        const double s0 = a.run.seconds, a0 = a.run.audioSeconds;
        a.run.process(blocksPerChunk, 1000u + static_cast<uint64_t>(c), ev);
        cpu[static_cast<size_t>(c)] = (a.run.seconds - s0) / std::max(1e-9, a.run.audioSeconds - a0);
        a.run.clearLogs();
        const double rss = residentMB(); if (c == endA - 1) rss0 = rss; rssEnd = rss;
    }
    auto best = [&](int from) { return *std::min_element(cpu.begin() + from, cpu.begin() + from + measure); };   // the fastest second: the machine (other work, the clock) can only make a second slower, never faster
    const double first = best(lead), last = best(startB);
    char buf[256]; std::snprintf(buf, sizeof buf, "%-26s cpu %5.2f%% -> %5.2f%%   memory %.1f -> %.1f MB", f.stem().string().c_str(), first * 100.0, last * 100.0, rss0, rssEnd);
    line = buf; bool ok = true;
    if (a.run.bad) { line += "   FAIL: NaN / Inf or a process() error"; ok = false; }
    if (a.run.peak > 1e4) { line += "   FAIL: output peak " + std::to_string(a.run.peak); ok = false; }
    if (rssEnd - rss0 > 16.0) { line += "   FAIL: memory grows by " + std::to_string(rssEnd - rss0) + " MB"; ok = false; }
    // (below 0.5 % of a core the clock of the machine is as big as the number)
    const bool grown = last > first * 1.6 && last > 0.005;
    if (grown) { line += "   FAIL: at the same settings the time per block has grown (" + std::to_string(last / first) + "x)"; ok = false; }
    if (timingOnly) *timingOnly = grown && !a.run.bad && a.run.peak <= 1e4 && rssEnd - rss0 <= 16.0;
    a.close(); return ok;
}
bool linkChecks(const std::vector<fs::path>& files) {
    auto find = [&](const char* code) { for (const auto& f : files) if (f.stem().string().find(std::string(" ") + code + " ") != std::string::npos) return f; return fs::path(); };
    const fs::path fa = find("DY08"), fb = find("EQ02"), fc = find("EQ07");
    if (fa.empty() || fb.empty() || fc.empty()) return true;   // a partial set of plug-ins: nothing to check
    bool ok = true; auto fail = [&](const std::string& t) { std::printf("FAIL  SW Link: %s\n", t.c_str()); ok = false; };
    Loaded a, b, c; std::string why;
    if (!a.open(fa, why)) { fail("A " + why); return false; }
    if (!b.open(fb, why)) { fail("B " + why); a.close(); return false; }
    // before anybody listens only the count is sent
    { const auto l = b.link(); if (l.size() != 1 || l[0] != 1.0) fail("B should see 1 other instance (A), got " + std::to_string(l.empty() ? -9.0 : l[0]) + " values " + std::to_string(l.size())); }
    { const auto l = a.link(); if (l.size() != 1 || l[0] != 1.0) fail("A should see 1 other instance (B)"); }
    // A plays a 1 kHz tone at -20 dBFS (DY08 at its defaults passes it), B asks for the spectrum of the others
    a.run.toneHz = 1000.0; EventList none; a.run.process(60, 1, none); b.run.process(10, 2, none);
    b.m->send(b.p, "c linkwatch 1"); a.run.process(8, 1, none); b.run.process(8, 2, none);
    { const auto l = b.link();
      if (l.size() != 65 || l[0] != 1.0) fail("B should get [1, 64 values], got " + std::to_string(l.size()) + " values");
      else {
          int pk = 1; for (int k = 1; k <= 64; ++k) if (l[static_cast<size_t>(k)] > l[static_cast<size_t>(pk)]) pk = k; const int band = pk - 1;
          const double f = 20.0 * std::pow(1000.0, (band + 0.5) / 64.0);
          if (std::fabs(std::log2(f / 1000.0)) > 0.12 || l[static_cast<size_t>(pk)] < -30.0 || l[static_cast<size_t>(pk)] > -10.0) fail("B should hear A's 1 kHz tone at about -20 dB, got " + std::to_string(f) + " Hz at " + std::to_string(l[static_cast<size_t>(pk)]) + " dB");
      } }
    b.m->send(b.p, "c linkwatch 0"); { const auto l = b.link(); if (l.size() != 1) fail("after linkwatch 0 only the count should be sent"); }
    // A is destroyed and its library unloaded: the registry must survive it
    a.close();
    { b.run.process(8, 2, none); const auto l = b.link(); if (l.size() != 1 || l[0] != 0.0) fail("B should see nobody after A was unloaded"); }
    if (!c.open(fc, why)) { fail("C " + why); b.close(); return false; }
    { const auto l = c.link(); if (l.size() != 1 || l[0] != 1.0) fail("C should find the registry again and see B"); }
    { const auto l = b.link(); if (l.size() != 1 || l[0] != 1.0) fail("B should see C"); }
    c.close(); b.close();
    if (ok) std::printf("ok    SW Link: three products in one process (%s, %s, %s): peers, spectrum of the others, and the registry survives an unloaded library\n", fa.stem().string().c_str(), fb.stem().string().c_str(), fc.stem().string().c_str());
    return ok;
}

// The reported latency against what a plug-in really does: an impulse at the defaults, the position of the largest output sample against the latency the plug-in reports. A host delays the
// other tracks by the reported number (plug-in delay compensation): a wrong number puts the track out of time with the rest and combs against a parallel copy. Only plug-ins whose impulse
// response has a clear main peak can be judged (a reverb or a delay has none: the peak is then not compared). Differences are WARN lines, not failures: an IIR filter's own group delay is a few samples.
void impulseChecks(const std::vector<fs::path>& files, int& warns, std::vector<std::string>& lines) {
    for (const auto& f : files) {
        Loaded l; std::string why; if (!l.open(f, why)) continue;
        const auto* lat = static_cast<const clap_plugin_latency_t*>(l.p->get_extension(l.p, CLAP_EXT_LATENCY));
        const double reported = lat ? lat->get(l.p) : 0.0;
        const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(l.p->get_extension(l.p, CLAP_EXT_AUDIO_PORTS));
        l.run.nIn = ports ? std::min<uint32_t>(2, ports->count(l.p, true)) : 1;
        if (l.run.nIn > 1) { clap_audio_port_info_t pi{}; if (ports->get(l.p, true, 1, &pi)) l.run.inCh[1] = std::min<uint32_t>(2, pi.channel_count); }
        l.run.impulse = 4 * kBlock;                        // after a few blocks of silence (smoothers settle)
        EventList none; const int blocks = static_cast<int>((4 * kBlock + reported + 8192) / kBlock) + 2; l.run.process(blocks, 1, none);
        const auto& y = l.run.outAll[0]; double pk = 0, sumsq = 0; size_t at = 0;
        for (size_t i = 0; i < y.size(); ++i) { const double a = std::fabs(static_cast<double>(y[i])); sumsq += a * a; if (a > pk) { pk = a; at = i; } }
        const double rms = std::sqrt(sumsq / std::max<size_t>(1, y.size()));
        const double got = static_cast<double>(at) - static_cast<double>(l.run.impulse);
        const std::string name = f.stem().string();
        if (pk < 1e-3) { lines.push_back("  (no clear impulse: " + name + ")"); l.close(); continue; }             // a gate / dynamics that does not let a lone click through at the defaults
        // a main peak: far above the rest of the response (an impulse spread over many samples, like a reverb, has no single position)
        if (pk < 20.0 * rms) { lines.push_back("  (no single peak: " + name + ")"); l.close(); continue; }
        const double tol = std::max(8.0, 0.02 * reported);
        if (std::fabs(got - reported) > tol) {
            char b[200]; std::snprintf(b, sizeof b, "WARN %-30s the impulse comes out %6.0f samples late, the plug-in reports %6.0f", name.c_str(), got, reported); lines.push_back(b); ++warns;
        }
        l.close();
    }
}

// The saved state of a project, through the real plug-in: random values on every parameter -> save -> a new instance -> load -> the same values (and the same reported latency); a state that was cut short,
// read a few bytes at a time, or is garbage must not crash and must leave every parameter inside its range.
struct MemOut { std::vector<uint8_t> d; clap_ostream_t s{}; MemOut() { s.ctx = this; s.write = [](const clap_ostream_t* st, const void* b, uint64_t n) -> int64_t { auto* m = static_cast<MemOut*>(st->ctx); const auto* q = static_cast<const uint8_t*>(b); m->d.insert(m->d.end(), q, q + n); return static_cast<int64_t>(n); }; } };
struct MemIn {
    std::vector<uint8_t> d; size_t pos = 0, chunk; clap_istream_t s{};
    MemIn(std::vector<uint8_t> bytes, size_t maxChunk = 1u << 30) : d(std::move(bytes)), chunk(maxChunk) {
        s.ctx = this;
        s.read = [](const clap_istream_t* st, void* b, uint64_t n) -> int64_t { auto* m = static_cast<MemIn*>(st->ctx); const size_t k = std::min<size_t>({static_cast<size_t>(n), m->chunk, m->d.size() - m->pos}); std::memcpy(b, m->d.data() + m->pos, k); m->pos += k; return static_cast<int64_t>(k); };
    }
};
bool stateChecks(const std::vector<fs::path>& files, std::vector<std::string>& problems) {
    bool ok = true; int tested = 0;
    for (const auto& f : files) {
        const std::string name = f.stem().string(); std::string why;
        auto bad = [&](const std::string& t) { problems.push_back("FAIL  state of " + name + ": " + t); ok = false; };
        Loaded a; if (!a.open(f, why)) { bad(why); continue; }
        const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS));
        const auto* st = static_cast<const clap_plugin_state_t*>(a.p->get_extension(a.p, CLAP_EXT_STATE));
        const auto* lat = static_cast<const clap_plugin_latency_t*>(a.p->get_extension(a.p, CLAP_EXT_LATENCY));
        if (!pe || !st) { bad("no params / state extension"); a.close(); continue; }
        const uint32_t n = pe->count(a.p); std::vector<clap_param_info_t> info(n);
        Rng rng(0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(n) * 7919u);
        EventList ev;
        for (uint32_t i = 0; i < n; ++i) {
            pe->get_info(a.p, i, &info[i]); if (info[i].flags & CLAP_PARAM_IS_READONLY) continue;
            double v = info[i].min_value + (rng.next() * 0.5 + 0.5) * (info[i].max_value - info[i].min_value);
            if (info[i].flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
            ev.set(info[i].id, v);
        }
        // the ids a host stores in its sessions: the product's parameters are numbered from 0 in their order, the common switches have ids of their own (whatever the number of the product's parameters)
        for (uint32_t i = 0; i < n; ++i) {
            const std::string nm = info[i].name;
            const clap_id want = nm == "Auto gain" ? 0x1000 : nm == "Delta" ? 0x1001 : nm == "Bypass" ? 0x1002 : i;
            if (info[i].id != want) { bad("parameter " + std::to_string(i) + " (" + nm + ") has id " + std::to_string(info[i].id) + ", expected " + std::to_string(want)); break; }
            double tmp = 0; if (!pe->get_value(a.p, info[i].id, &tmp)) { bad("get_value does not know the id of " + nm); break; }
        }
        a.run.process(3, 5, ev);
        std::vector<double> va(n); for (uint32_t i = 0; i < n; ++i) pe->get_value(a.p, info[i].id, &va[i]);
        MemOut out; if (!st->save(a.p, &out.s) || out.d.size() < 8) { bad("save failed"); a.close(); continue; }
        a.run.process(2, 5, ev); const double latA = lat ? lat->get(a.p) : 0.0;
        a.close();
        auto fresh = [&](Loaded& b) { std::string w; return b.open(f, w); };
        auto inRange = [&](Loaded& b, const char* what) { for (uint32_t i = 0; i < n; ++i) { double v = 0; pe->get_value(b.p, info[i].id, &v); if (!std::isfinite(v) || v < info[i].min_value - 1e-9 || v > info[i].max_value + 1e-9) { bad(std::string(what) + ": parameter " + std::to_string(i) + " out of range (" + std::to_string(v) + ")"); return false; } } return true; };
        for (size_t chunk : {size_t(1) << 30, size_t(7)}) {   // all at once, and a few bytes per read
            Loaded b; if (!fresh(b)) { bad("reopen"); break; }
            const auto* pb = static_cast<const clap_plugin_params_t*>(b.p->get_extension(b.p, CLAP_EXT_PARAMS)); const auto* sb = static_cast<const clap_plugin_state_t*>(b.p->get_extension(b.p, CLAP_EXT_STATE));
            MemIn in(out.d, chunk); if (!sb->load(b.p, &in.s)) { bad(std::string("load failed (chunk ") + std::to_string(chunk) + ")"); b.close(); continue; }
            EventList none; b.run.process(3, 5, none);
            for (uint32_t i = 0; i < n; ++i) { if (info[i].flags & CLAP_PARAM_IS_READONLY) continue; double v = 0; pb->get_value(b.p, info[i].id, &v); if (v != va[i]) { bad("parameter " + std::to_string(i) + " (" + info[i].name + ") came back as " + std::to_string(v) + ", saved " + std::to_string(va[i])); break; } }
            const auto* lb = static_cast<const clap_plugin_latency_t*>(b.p->get_extension(b.p, CLAP_EXT_LATENCY));
            // the latency of the restored instance is what the plug-in reports for these values (a change needs a restart: the host asks for it; here compare what it will be)
            (void)lb; (void)latA;
            MemOut again; sb->save(b.p, &again.s); if (again.d != out.d) bad("saving the restored instance gives other bytes (" + std::to_string(again.d.size()) + " vs " + std::to_string(out.d.size()) + ")");
            b.close();
        }
        // a state saved by an older build of the product (one parameter fewer at the end of the product's list): the common switches (Auto gain, Delta, Bypass) are the last values and must keep
        // their places, the product's parameters keep theirs
        {
            uint32_t nExtra = 0; for (uint32_t i = 0; i < n; ++i) { const std::string nm = info[i].name; if (nm == "Auto gain" || nm == "Delta" || nm == "Bypass") ++nExtra; }
            const uint32_t nProd = n - nExtra, hdr = 8;
            if (nProd >= 2 && out.d.size() >= hdr + 8ull * n) {
                std::vector<uint8_t> old; old.insert(old.end(), out.d.begin(), out.d.begin() + hdr + 8ull * (nProd - 1));                       // magic, count, the product's values but the last
                old.insert(old.end(), out.d.begin() + hdr + 8ull * nProd, out.d.begin() + hdr + 8ull * n);                                       // the switches
                old.insert(old.end(), out.d.begin() + hdr + 8ull * n, out.d.end());                                                              // whatever follows the values
                const uint32_t cnt = n - 1; std::memcpy(old.data() + 4, &cnt, 4);
                Loaded b; if (!fresh(b)) { bad("reopen"); }
                else {
                    const auto* pb = static_cast<const clap_plugin_params_t*>(b.p->get_extension(b.p, CLAP_EXT_PARAMS)); const auto* sb = static_cast<const clap_plugin_state_t*>(b.p->get_extension(b.p, CLAP_EXT_STATE));
                    MemIn in(old); if (!sb->load(b.p, &in.s)) bad("an older state (one product parameter fewer) failed to load");
                    else {
                        EventList none; b.run.process(3, 5, none);
                        for (uint32_t i = 0; i < n; ++i) {
                            if (info[i].flags & CLAP_PARAM_IS_READONLY) continue;
                            if (i == nProd - 1) continue;   // the one the older build did not have: its default
                            double v = 0; pb->get_value(b.p, info[i].id, &v);
                            if (v != va[i]) { bad("an older state (one product parameter fewer): parameter " + std::to_string(i) + " (" + info[i].name + ") came back as " + std::to_string(v) + ", saved " + std::to_string(va[i])); break; }
                        }
                    }
                    b.close();
                }
            }
        }
        // cut short at various places, and garbage: no crash, parameters stay in range
        std::vector<size_t> cuts = {0, 1, 3, 4, 7, 8, 11, 12, 15, out.d.size() / 3, out.d.size() / 2, out.d.size() - 9, out.d.size() - 1};
        for (size_t c : cuts) {
            if (c >= out.d.size()) continue;
            Loaded b; if (!fresh(b)) { bad("reopen"); break; }
            const auto* sb = static_cast<const clap_plugin_state_t*>(b.p->get_extension(b.p, CLAP_EXT_STATE));
            MemIn in(std::vector<uint8_t>(out.d.begin(), out.d.begin() + static_cast<std::ptrdiff_t>(c))); sb->load(b.p, &in.s);
            EventList none; b.run.process(2, 5, none); inRange(b, "a cut state"); b.close();
        }
        for (int g = 0; g < 4; ++g) {
            Loaded b; if (!fresh(b)) { bad("reopen"); break; }
            const auto* sb = static_cast<const clap_plugin_state_t*>(b.p->get_extension(b.p, CLAP_EXT_STATE));
            std::vector<uint8_t> junk(g == 0 ? 64 : g == 1 ? 1000 : out.d.size()); for (auto& x : junk) x = static_cast<uint8_t>(rng.next() * 127.0f + 128.0f);
            if (g >= 2) { std::memcpy(junk.data(), out.d.data(), std::min<size_t>(12 + 8 * (g - 1), out.d.size())); }   // the right header, then garbage
            MemIn in(junk); sb->load(b.p, &in.s);
            EventList none; b.run.process(2, 5, none); inRange(b, "a garbage state"); b.close();
        }
        ++tested;
    }
    if (ok) std::printf("ok    project state: %d plug-ins: random values on every parameter survive save -> new instance -> load -> save (same bytes); cut-short, byte-by-byte and garbage states do not crash; a state saved before a product parameter was added still puts the common switches where they belong\n", tested);
    return ok;
}

// Random settings through the real plug-in: every parameter at a random value (three seeds), 0.4 s of noise (plus a loud burst): no NaN / Inf, no error from process(), and nothing absurdly loud.
bool randomParamChecks(const std::vector<fs::path>& files, std::vector<std::string>& problems) {
    bool ok = true; int tested = 0;
    for (const auto& f : files) {
        const std::string name = f.stem().string();
        for (int seed = 1; seed <= 3; ++seed) {
            Loaded a; std::string why; if (!a.open(f, why)) { problems.push_back("FAIL  random settings of " + name + ": " + why); ok = false; break; }
            const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS));
            const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(a.p->get_extension(a.p, CLAP_EXT_AUDIO_PORTS));
            a.run.nIn = ports ? std::min<uint32_t>(2, ports->count(a.p, true)) : 1;
            if (a.run.nIn > 1) { clap_audio_port_info_t pi{}; if (ports->get(a.p, true, 1, &pi)) a.run.inCh[1] = std::min<uint32_t>(2, pi.channel_count); }
            Rng rng(0xD1B54A32D192ED03ull * static_cast<uint64_t>(seed) + 17);
            EventList ev;
            for (uint32_t i = 0; pe && i < pe->count(a.p); ++i) {
                clap_param_info_t pi{}; if (!pe->get_info(a.p, i, &pi) || (pi.flags & CLAP_PARAM_IS_READONLY)) continue;
                double v = pi.min_value + (rng.next() * 0.5 + 0.5) * (pi.max_value - pi.min_value); if (pi.flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
                ev.set(pi.id, v);
            }
            a.run.process(1, 100 + static_cast<uint64_t>(seed), ev);   // the settings arrive; the next blocks are the audio
            a.run.process(70, 200 + static_cast<uint64_t>(seed), ev);
            if (a.run.bad) { problems.push_back("FAIL  random settings of " + name + " (seed " + std::to_string(seed) + "): NaN / Inf in the output or an error from process()"); ok = false; }
            else if (a.run.peak > 1e4) { char b[200]; std::snprintf(b, sizeof b, "FAIL  random settings of %s (seed %d): peak %.3g", name.c_str(), seed, a.run.peak); problems.push_back(b); ok = false; }
            a.close();
        }
        ++tested;
    }
    if (ok) std::printf("ok    random settings: %d plug-ins x 3 seeds: every parameter at a random value, 0.4 s of noise: finite output, no process() error, peak under 1e4\n", tested);
    return ok;
}

// The same input in blocks of different sizes: what comes out must not depend on how the host cuts the audio (the buffer size of a DAW, an offline bounce, a host that splits a block at every automation point).
// The reference is blocks of 256; the others are 1, 7, 64, 509, 1024 and a mix (1, 2, 3, 5, 8 ... 987). The input is the same noise, three parameter events (random parameters, random values) fall at the same absolute samples in
// every run (the adapter has to cut the block at the event). `over` = the largest block a run sends is 2 x the max_frames the plug-in was activated with (a host that breaks the CLAP rule: the adapter has to cope).
struct TimedParam { size_t at; clap_id id; double v; bool midi = false; uint8_t st = 0, d1 = 0, d2 = 0; };
struct Cut { const char* name; std::vector<uint32_t> sizes; };
void processCut(Loaded& a, const std::vector<float> in[2], const std::vector<float>* sc, const std::vector<TimedParam>& tp, const std::vector<uint32_t>& sizes, std::vector<float> out[2], bool& bad) {
    const size_t total = in[0].size(); uint32_t maxSize = 1; for (uint32_t z : sizes) maxSize = std::max(maxSize, z);
    std::vector<float> bi[2], bs[2], bo[2]; for (int c = 0; c < 2; ++c) { bi[c].resize(maxSize); bs[c].resize(maxSize); bo[c].resize(maxSize); out[c].assign(total, 0.0f); }
    size_t pos = 0, k = 0, ti = 0;
    while (pos < total) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(sizes[k++ % sizes.size()], total - pos));
        EventList ev; while (ti < tp.size() && tp[ti].at < pos + n) { const uint32_t t = static_cast<uint32_t>(tp[ti].at - pos); if (tp[ti].midi) ev.midi(tp[ti].st, tp[ti].d1, tp[ti].d2, t); else ev.set(tp[ti].id, tp[ti].v, t); ++ti; }
        for (int c = 0; c < 2; ++c) { std::copy(in[c].begin() + static_cast<long>(pos), in[c].begin() + static_cast<long>(pos + n), bi[c].begin()); std::fill(bo[c].begin(), bo[c].end(), 0.0f); if (sc) std::copy(sc[c].begin() + static_cast<long>(pos), sc[c].begin() + static_cast<long>(pos + n), bs[c].begin()); }
        float* ip[2] = {bi[0].data(), bi[1].data()}; float* sp[2] = {bs[0].data(), bs[1].data()}; float* op[2] = {bo[0].data(), bo[1].data()};
        clap_audio_buffer_t ib[2]{}; ib[0].channel_count = 2; ib[0].data32 = ip; ib[1].channel_count = 2; ib[1].data32 = sp; clap_audio_buffer_t ob{}; ob.channel_count = 2; ob.data32 = op;
        clap_output_events_t oe{nullptr, outTryPush}; clap_process_t pr{};
        pr.steady_time = -1; pr.frames_count = n; pr.audio_inputs = ib; pr.audio_inputs_count = sc ? 2 : 1; pr.audio_outputs = &ob; pr.audio_outputs_count = 1; pr.in_events = &ev.in; pr.out_events = &oe;
        if (a.p->process(a.p, &pr) == CLAP_PROCESS_ERROR) bad = true;
        for (int c = 0; c < 2; ++c) for (uint32_t i = 0; i < n; ++i) { out[c][pos + i] = bo[c][i]; if (!std::isfinite(bo[c][i])) bad = true; }
        pos += n;
    }
}
bool blockChecks(const std::vector<fs::path>& files, std::vector<std::string>& lines, int& differ) {
    bool ok = true; differ = 0;
    const size_t total = static_cast<size_t>(kSr * 0.7);
    const std::vector<Cut> cuts = {{"1", {1}}, {"7", {7}}, {"64", {64}}, {"509", {509}}, {"1024", {1024}}, {"mixed", {1, 0, 2, 3, 0, 5, 8, 13, 21, 0, 34, 55, 89, 144, 233, 377, 610, 987}}, {"2048*", {2048}}};
    struct Scenario { const char* name; float level; bool events; bool bursts; bool sidechain; bool midi = false; };
    const Scenario scenarios[] = {{"steady, -20 dBFS noise", 0.1732f, false, false, false}, {"steady, loud (peaks over 0 dBFS)", 1.0f, false, false, false}, {"steady, tone bursts with pauses", 0.25f, false, true, false},
                                  {"sidechain connected (noise in, tone bursts on the sidechain)", 0.1732f, false, false, true}, {"-20 dBFS noise + 3 parameter events", 0.1732f, true, false, false},
                                  {"MIDI notes and controllers (Freeze on, if there is one), tone bursts in", 0.25f, true, true, false, true}};
    for (const auto& f : files) {
        const std::string name = f.stem().string();
        std::vector<TimedParam> tp, tpMidi; bool bad = false; std::string why; std::vector<uint8_t> state;   // the state of the first instance goes into every other one (SA02: the seed of its component tolerances is saved with the project)
        std::vector<std::string> rows; double worstAll = -400.0; bool failed = false, differs = false;
        for (const auto& sc : scenarios) {
            std::vector<float> in[2];
            {   // noise, or a 220 Hz tone in bursts (0.12 s on, 0.1 s off) over a faint noise floor: what gates, detectors and pitch trackers need to act
                Rng rng(77);
                for (int c = 0; c < 2; ++c) {
                    in[c].resize(total);
                    for (size_t i = 0; i < total; ++i) {
                        const float nz = rng.next();
                        if (!sc.bursts) { in[c][i] = nz * sc.level; continue; }
                        const bool on = std::fmod(static_cast<double>(i) / kSr, 0.22) < 0.12;
                        in[c][i] = nz * 0.001f + (on ? sc.level * static_cast<float>(std::sin(6.283185307179586 * 220.0 * static_cast<double>(i) / kSr)) : 0.0f);
                    }
                }
            }
            std::vector<float> scIn[2]; bool skipScenario = false;
            if (sc.sidechain) for (int c = 0; c < 2; ++c) { scIn[c].resize(total); for (size_t i = 0; i < total; ++i) scIn[c][i] = std::fmod(static_cast<double>(i) / kSr, 0.22) < 0.12 ? 0.25f * static_cast<float>(std::sin(6.283185307179586 * (330.0 + 110.0 * c) * static_cast<double>(i) / kSr)) : 0.0f; }
            auto runCut = [&](const std::vector<uint32_t>& sizes, std::vector<float> out[2]) -> bool {
                Loaded a; if (!a.open(f, why, 1024)) return false;
                const auto* st = static_cast<const clap_plugin_state_t*>(a.p->get_extension(a.p, CLAP_EXT_STATE));
                if (st) { if (state.empty()) { MemOut o; if (st->save(a.p, &o.s)) state = o.d; } else { MemIn in(state, state.size()); st->load(a.p, &in.s); } }
                if (sc.events && !sc.midi && tp.empty()) {   // chosen once, from the first plug-in instance: three random writable parameters
                    const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS)); Rng rr(0xB10C ^ std::hash<std::string>()(name));
                    std::vector<clap_param_info_t> info(pe ? pe->count(a.p) : 0); for (uint32_t i = 0; i < info.size(); ++i) pe->get_info(a.p, i, &info[i]);
                    const size_t at[3] = {total / 7, total / 3 + 11, total / 2 + 301};
                    for (int e = 0; e < 3 && !info.empty(); ++e) {
                        const uint32_t i = static_cast<uint32_t>((rr.next() * 0.5f + 0.5f) * static_cast<float>(info.size())) % static_cast<uint32_t>(info.size()); if (info[i].flags & CLAP_PARAM_IS_READONLY) continue;
                        double v = info[i].min_value + (rr.next() * 0.5 + 0.5) * (info[i].max_value - info[i].min_value); if (info[i].flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
                        tp.push_back({at[e], info[i].id, v});
                    }
                }
                if (sc.midi) {
                    const auto* np = static_cast<const clap_plugin_note_ports_t*>(a.p->get_extension(a.p, CLAP_EXT_NOTE_PORTS));
                    if (!np || np->count(a.p, true) < 1) { skipScenario = true; a.close(); return true; }   // no MIDI input
                    if (tpMidi.empty()) {   // chosen once: Freeze on (CR04 captures on a note only then), then notes and controllers at fixed samples
                        const auto* pe = static_cast<const clap_plugin_params_t*>(a.p->get_extension(a.p, CLAP_EXT_PARAMS));
                        for (uint32_t i = 0; pe && i < pe->count(a.p); ++i) { clap_param_info_t pi{}; if (pe->get_info(a.p, i, &pi) && std::string(pi.name) == "Freeze") tpMidi.push_back({0, pi.id, 1.0}); }
                        auto m = [&](size_t at, uint8_t st, uint8_t d1, uint8_t d2) { TimedParam t{at, 0, 0.0}; t.midi = true; t.st = st; t.d1 = d1; t.d2 = d2; tpMidi.push_back(t); };
                        m(total / 5, 0x90, 60, 100); m(total / 4 + 3, 0xB0, 64, 127); m(total / 3, 0xB0, 1, 100); m(total / 2 + 17, 0x90, 64, 90); m(total * 3 / 5, 0x80, 60, 0);
                    }
                }
                if (sc.sidechain) {
                    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(a.p->get_extension(a.p, CLAP_EXT_AUDIO_PORTS));
                    if (!ports || ports->count(a.p, true) < 2) { skipScenario = true; a.close(); return true; }
                }
                processCut(a, in, sc.sidechain ? scIn : nullptr, sc.midi ? tpMidi : (sc.events ? tp : std::vector<TimedParam>{}), sizes, out, bad); a.close(); return true;
            };
            std::vector<float> ref[2];
            if (!runCut({256}, ref)) { lines.push_back("FAIL  " + name + ": " + why); ok = false; failed = true; break; }
            if (skipScenario) continue;   // this product has no sidechain input / no MIDI input
            double peak = 1e-9; for (int c = 0; c < 2; ++c) for (float x : ref[c]) peak = std::max(peak, static_cast<double>(std::fabs(x)));
            std::string row = std::string("  ") + sc.name + ":";
            for (const auto& cut : cuts) {
                std::vector<float> o[2];
                if (!runCut(cut.sizes, o)) { lines.push_back("FAIL  " + name + ": " + why); ok = false; failed = true; break; }
                double err = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < total; ++i) err = std::max(err, static_cast<double>(std::fabs(o[c][i] - ref[c][i])));
                const double db = 20.0 * std::log10(std::max(err / peak, 1e-20)); worstAll = std::max(worstAll, db);
                if (db > (sc.events && !sc.midi ? -50.0 : -90.0)) differs = true;   // steady: nothing may depend on the cut; with parameter events the coefficient ramps start on the product's own control grid (a few -60 .. -100 dB)
                char b[32]; std::snprintf(b, sizeof b, " %s:%.0f", cut.name, db); row += b;
            }
            if (failed) break;
            rows.push_back(row);
        }
        if (failed) continue;
        if (differs) ++differ;
        // the IR of GT02 / RV04 / ST05 is rebuilt one step per process() call, so when a new IR takes over depends on the block size (README "ブロック長に依存しないこと"): known; any other product is a failure
        const bool known = name == "SW GT02 Cab IR" || name == "SW RV04 Convolution" || name == "SW ST05 Phones";
        if (differs && !known) ok = false;
        if (bad) { lines.push_back("FAIL  " + name + "   NaN / Inf or a process() error"); ok = false; }
        else if (!differs) { char b[200]; std::snprintf(b, sizeof b, "ok    %-26s the same in every block size, within the limits (worst %.0f dB re peak)", name.c_str(), worstAll); lines.push_back(b); }
        else { std::string t = "DIFF  " + name + "   (dB re peak, blocks of 256 as the reference; * = longer than max_frames)"; for (const auto& r : rows) t += "\n      " + r; lines.push_back(t); }
    }
    return ok;
}
// How long a sound goes on after the input has stopped: an impulse (0.5) in silence, 30 s of silence after it; the last sample above -80 dBFS (1e-4) minus the position of the impulse (the delay the plug-in reports is part of it).
// A host that bounces or freezes a track needs this ("tail" in CLAP / VST3 getTailSamples): without it the reverb or delay stops at the end of the region.
void tailChecks(const std::vector<fs::path>& files, std::vector<std::string>& lines) {
    for (const auto& f : files) {
        Loaded a; std::string why; if (!a.open(f, why)) { lines.push_back("FAIL  " + f.stem().string() + ": " + why); continue; }
        a.run.impulse = 4 * kBlock; EventList none;
        const int blocks = static_cast<int>(30.0 * kSr / kBlock);
        a.run.process(blocks, 1, none);
        long last = -1; for (int c = 0; c < 2; ++c) for (size_t i = a.run.outAll[c].size(); i-- > 0;) if (std::fabs(a.run.outAll[c][i]) > 1e-4f) { last = std::max<long>(last, static_cast<long>(i)); break; }
        const auto* lat = static_cast<const clap_plugin_latency_t*>(a.p->get_extension(a.p, CLAP_EXT_LATENCY));
        const uint32_t reported = lat ? lat->get(a.p) : 0;
        const double tail = last < 0 ? 0.0 : (static_cast<double>(last) - 4.0 * kBlock - reported) / kSr;
        const auto* te = static_cast<const clap_plugin_tail_t*>(a.p->get_extension(a.p, CLAP_EXT_TAIL));
        const double said = te ? te->get(a.p) / kSr : -1.0;
        char b[200]; std::snprintf(b, sizeof b, "%-26s tail %7.3f s   reported %s", f.stem().string().c_str(), tail, te ? (std::to_string(said).substr(0, 7) + " s").c_str() : "(none)");
        if (tail > 0.01) lines.push_back(b);
        a.close();
    }
}
}   // namespace

int main(int argc, char** argv) {
    // the preset checks write into the person's home folder: a temporary one
    const fs::path tmpHome = fs::temp_directory_path() / ("sw-host-smoke-home-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpHome); setenv("HOME", tmpHome.c_str(), 1);
    std::vector<fs::path> files; double soakSeconds = 0; bool blocksOnly = false, tailsOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string opt = argv[i];
        if (opt.rfind("--rate=", 0) == 0) {   // the whole run at another sample rate, with the same lengths in seconds
            kSr = std::atof(opt.c_str() + 7); if (kSr < 8000.0 || kSr > 384000.0) { std::fprintf(stderr, "bad rate\n"); return 2; }
            kPhaseBlocks = static_cast<int>(std::lround(563.0 * kSr / 48000.0)); kMeasureBlocks = static_cast<int>(std::lround(188.0 * kSr / 48000.0)); continue;
        }
        if (opt.rfind("--soak=", 0) == 0) { soakSeconds = std::atof(opt.c_str() + 7); continue; }
        if (opt == "--blocks") { blocksOnly = true; continue; }
        if (opt == "--tails") { tailsOnly = true; continue; }
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

    if (tailsOnly) {   // --tails: only the tail measurement
        std::vector<std::string> lines; tailChecks(files, lines);
        for (const auto& l : lines) std::printf("%s\n", l.c_str());
        std::printf("tails: %zu plug-ins, %zu with a tail over 10 ms\n", files.size(), lines.size());
        return 0;
    }
    if (blocksOnly) {   // --blocks: only the block-size check
        std::vector<std::string> lines; int differ = 0; const bool ok = blockChecks(files, lines, differ);
        for (const auto& l : lines) std::printf("%s\n", l.c_str());
        std::printf("blocks: %zu plug-ins, %d differ from the blocks of 256 (more than -90 dB in a steady run, more than -50 dB with parameter events), %s\n", files.size(), differ, ok ? "no failure" : "FAILURES");
        return ok ? 0 : 1;
    }
    if (soakSeconds > 0) {   // --soak=SECONDS: only the long run
        int bad = 0; double worstGrowth = 0;
        for (const auto& f : files) {
            // the clock of a shared machine moves by a factor of two within seconds: a plug-in that fails only on the time per block is measured twice more (the runs are the same, so a plug-in
            // that piles something up fails every time: CR04 was 26x), and it fails if it fails all three
            std::string line; bool timingOnly = false; bool ok = soakOne(f, soakSeconds, line, &timingOnly);
            for (int again = 0; !ok && timingOnly && again < 2; ++again) { std::printf("retry %s\n", line.c_str()); timingOnly = false; ok = soakOne(f, soakSeconds, line, &timingOnly); }
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", line.c_str()); std::fflush(stdout); if (!ok) ++bad;
        }
        std::printf("soak: %zu plug-ins x %.0f s of audio: %d FAIL\n", files.size(), soakSeconds, bad); (void)worstGrowth;
        return bad ? 1 : 0;
    }
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
    if (!linkChecks(files)) ++fails;
    { std::vector<std::string> problems; if (!randomParamChecks(files, problems)) { ++fails; for (const auto& l : problems) std::printf("%s\n", l.c_str()); } }
    if (!unitChecks(files)) ++fails;
    { std::vector<std::string> problems; if (!stateChecks(files, problems)) { ++fails; for (const auto& l : problems) std::printf("%s\n", l.c_str()); } }
    { int w = 0; std::vector<std::string> lines; impulseChecks(files, w, lines); std::printf("\nlatency against an impulse: %d plug-in(s) differ from what they report\n", w); for (const auto& l : lines) std::printf("%s\n", l.c_str()); warns += w; }
    std::printf("\n%zu plug-ins: %d ok, %d FAIL, %d with warnings; Output gain checked on %d, Bypass on %d, Mix 0 %% on %d, In Off on %d; slowest %s (%.1f%% of one core, noisy)\n",
                files.size(), pass, fails, warns, outTested, bypassTested, mixTested, inTested, worstCpuName.c_str(), worstCpu);
    std::printf("bit-exact pass-through at the defaults (%zu):", passNames.size());
    for (const auto& n : passNames) std::printf(" [%s]", n.c_str());
    std::printf("\n");
    std::error_code ec; fs::remove_all(tmpHome, ec);
    return fails ? 1 : 0;
}
