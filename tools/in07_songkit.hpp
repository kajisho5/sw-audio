// SWINGBY (SW IN07) tools: the kit the demo songs are made with (tools/in07_song.cpp, the songs in tools/songs/). A song is tracks, each one
// SWINGBY instance with its preset (a factory preset, a user preset file or a recipe written in the song) and its events (notes, values, the
// mod wheel, the bend, on beats). The kit renders each track with the host's transport playing (so the arpeggiator, the trance gate and the
// synced LFOs run on the beat), ducks the music under the kick, sums, and masters the sum with this bundle's own processors (SW MS04 Clipper,
// SW MS01 Maximizer) to a loudness target measured on the song's loud sections. It can also write each track (stems) and a JSON of every
// track's values and events (for a video that follows the song: tools/in07_song_video.py).
#pragma once
#include "in07_recipe.hpp"
#include "ms01/ms01.hpp"
#include "ms04/ms04.hpp"
#include "sw/loudness.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace sw::in07::songkit {
using dsl::Raw;
constexpr double kFs = 48000.0;

// ---- tracks and events
enum Kind { NoteOff = 0, SetParam = 1, ModWheel = 2, EvBend = 3, NoteOn = 4 };   // at the same time: offs, values, then ons
struct Ev { double beat; int kind; int a; double v; };
struct Track {
    std::string name;
    std::vector<double> plain;
    double gainDb = 0;
    double duckDb = 0;   // the kick-keyed duck on this track
    std::vector<Ev> ev;
    std::vector<float> L, R;
    std::string preset, category, source;   // what the window shows: the preset's name, its category; "factory" / "user" / "song"
    int factoryIndex = -1;
    std::string role = "music";   // for the video: "lead" (shown first), "music", "bed" (shown when nothing else plays), "drums" (never shown)
    void note(double beat, double len, int key, double vel = 0.85) { ev.push_back({beat, NoteOn, key, vel}); ev.push_back({beat + len, NoteOff, key, 0}); }
    void chord(double beat, double len, std::initializer_list<int> keys, double vel = 0.8) { for (int k : keys) note(beat, len, k, vel); }
    void chord(double beat, double len, const std::vector<int>& keys, double vel = 0.8) { for (int k : keys) note(beat, len, k, vel); }
    void wheel(double beat, double v) { ev.push_back({beat, ModWheel, 0, v}); }
    void param(double beat, int id, double v) { ev.push_back({beat, SetParam, id, v}); }
    void ramp(int id, double b0, double b1, double v0, double v1, double perBeat = 16) {
        const int n = std::max(1, static_cast<int>((b1 - b0) * perBeat));
        for (int i = 0; i <= n; ++i) param(b0 + (b1 - b0) * i / n, id, v0 + (v1 - v0) * i / n);
    }
    void bend(double beat, double v) { ev.push_back({beat, EvBend, 0, v}); }
    void bendRamp(double b0, double b1, double v0, double v1) {
        const int n = std::max(1, static_cast<int>((b1 - b0) * 16));
        for (int i = 0; i <= n; ++i) bend(b0 + (b1 - b0) * i / n, v0 + (v1 - v0) * i / n);
    }
    double& at(int id) { return plain[static_cast<size_t>(id)]; }
};
inline double bt(int bar, double beat = 0) { return bar * 4.0 + beat; }   // 4/4

inline Track factoryTrack(const std::string& name, const std::string& preset, double gainDb, double duckDb = 0) {
    const auto& n = presetNames();
    for (size_t i = 0; i < n.size(); ++i)
        if (n[i] == preset) {
            Track t{name, {}, gainDb, duckDb};
            presetValues(static_cast<int>(i), t.plain);
            t.preset = preset; t.category = factoryPresets()[i].category; t.source = "factory"; t.factoryIndex = static_cast<int>(i);
            return t;
        }
    std::fprintf(stderr, "no factory preset %s\n", preset.c_str()); std::exit(1);
}
inline Track userTrack(const std::string& name, const std::string& path, double gainDb, double duckDb = 0) {
    std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
    Track t{name, {}, gainDb, duckDb};
    sw::presetfile::Meta m; std::string err;
    if (!userPresetValues(ss.str(), t.plain, m, err)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str()); std::exit(1); }
    t.preset = m.name; t.category = m.category; t.source = "user";
    return t;
}
inline Track songTrack(const std::string& name, const Raw& r, double gainDb, double duckDb = 0) {   // a recipe written in the song
    Track t{name, {}, gainDb, duckDb};
    if (!recipe::plainOf(r, t.plain)) std::exit(1);
    t.preset = r.name; t.category = r.cat; t.source = "song";
    return t;
}

// ---- the recipes every song may use (the drums are SWINGBY too)
// a kick: a sine falling ~3.5 octaves on four Env 2 -> Pitch slots (+43 st at the start) and a knock
inline const Raw& kickRecipe() {
    using namespace dsl;
    static const Raw r = R("Kick", "FX", N().drv(25, 40)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 100).mod(3, "Env 2", "Pitch", 100).mod(4, "Env 2", "Pitch", 60)
        .L(1, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 20, 0) + fenv(0.5, 55, 0, 40) + amp(0.5, 380, 0, 60, 30))
        .L(2, smp("Knock", 0) + flt("HP 12", 1500, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 25, 0, 20, 30) + lvl(-10)));
    return r;
}
// a snare: a triangle body falling 18 semitones in its first 25 ms, the Static noise sample band-passed, a knock; drive, a room
inline const Raw& snareRecipe() {
    using namespace dsl;
    static const Raw r = R("Snare", "FX", N().drv(30, 60).eq(0, 2, 3).rev(1.3, 18, 40)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 50)
        .L(1, wave("Triangle", 0, 1, 0, 0) + flt("LP 12", 8000, 0, 0, 30, 0) + fenv(0.5, 25, 0, 20) + amp(0.5, 120, 0, 60, 40))
        .L(2, smp("Static", 0, 2, 60) + flt("BP 12", 3500, 10, 0, 20, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 220, 0, 80, 40) + lvl(-2))
        .L(3, smp("Knock", 1) + flt("HP 12", 800, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 60, 0, 40, 40) + lvl(-6)));
    return r;
}
// hats from the Tick and Static samples, high-passed
inline const Raw& hatRecipe(bool open) {
    using namespace dsl;
    static const Raw closed = R("Hat", "FX", N()
        .L(1, smp("Tick", 1) + flt("HP 12", 7000, 10, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 35, 0, 30, 70))
        .L(2, smp("Static", 2, 1) + flt("HP 12", 9000, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 25, 0, 20, 70) + lvl(-6)));
    static const Raw opened = R("Open hat", "FX", N()
        .L(1, smp("Tick", 1) + flt("HP 12", 7000, 10, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 60, 0, 60, 70))
        .L(2, smp("Static", 2, 1) + flt("HP 12", 8000, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 220, 0, 150, 70) + lvl(-4)));
    return open ? opened : closed;
}
// a riser: noise through a band-pass and a saw stack, both opened by automation (riseInto), the pitch bent up an octave (bend range 12)
inline const Raw& riserRecipe() {
    using namespace dsl;
    static const Raw r = R("Riser", "FX", N().rev(4, 40).dly("1/8 D", 35, 20).g({{"bend", 12}})
        .L(1, smp("Static", 0, 2, 100) + flt("BP 12", 400, 30, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(1500, 100, 100, 300))
        .L(2, saw(0, 6, 40, 100) + flt("LP 24", 800, 20, 0, 30, 0) + amp(3000, 100, 100, 300) + lvl(-8)));
    return r;
}
// the riser's sweep from b0 to b1 (its note held over it)
inline void riseInto(Track& riser, double b0, double b1) {
    riser.note(b0, b1 - b0, 53, 0.9);
    riser.param(b0, lp(0, Cutoff), 400); riser.param(b0, lp(1, Cutoff), 800); riser.param(b0, Level, -24);
    riser.ramp(lp(0, Cutoff), b0, b1, 400, 9000, 8); riser.ramp(lp(1, Cutoff), b0, b1, 800, 12000, 8);
    riser.ramp(Level, b0, b1, -24, 0, 8);
    riser.bendRamp(b0, b1, -1.0, 1.0);
}

// ---- a song
struct Section { std::string name; int bar0, bar1; bool loud; };   // bars [bar0, bar1); the loud ones set the loudness
struct Song {
    std::string title, style;
    double bpm = 120;
    int bars = 0;
    double tail = 4.0;            // seconds after the last bar
    std::vector<Section> sections;
    std::vector<Track> tracks;
    std::string duckBy = "kick";  // the track whose notes duck the others (by their duckDb)
    double duckRelease = 0.12;    // seconds
    double clipDrive = 10.0;      // dB into SW MS04 (the mix peaking at -6 dBFS)
    double target = -9.0;         // LUFS of the loudest loud section
    double limiterRelease = 120;  // ms (SW MS01)
    double charX = 20;            // SW MS01 character
    double fadeOut = 1.5;         // seconds at the very end
};

// one instance through the whole song, the host's transport playing from bar 0
inline void render(Track& t, long long total, double bpm) {
    const double beat = 60.0 / bpm;
    Processor p;
    recipe::apply(p, t.plain);
    p.prepare(kFs, 256);
    p.setTempo(bpm);
    std::stable_sort(t.ev.begin(), t.ev.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat || (a.beat == b.beat && a.kind < b.kind); });
    t.L.assign(static_cast<size_t>(total), 0.0f); t.R.assign(static_cast<size_t>(total), 0.0f);
    auto at = [&](double b) { return std::llround(b * beat * kFs); };
    size_t e = 0;
    for (long long pos = 0; pos < total;) {
        while (e < t.ev.size() && at(t.ev[e].beat) <= pos) {
            const Ev& x = t.ev[e++];
            if (x.kind == NoteOn) p.noteOn(x.a, x.v); else if (x.kind == NoteOff) p.noteOff(x.a);
            else if (x.kind == ModWheel) p.modWheel(x.v); else if (x.kind == EvBend) p.pitchBend(x.v); else p.setParam(x.a, x.v);
        }
        long long n = std::min<long long>(256, total - pos);
        if (e < t.ev.size()) n = std::max<long long>(1, std::min(n, at(t.ev[e].beat) - pos));
        p.setTransport(true, pos / kFs / beat);
        float* c[2] = {t.L.data() + pos, t.R.data() + pos};
        p.process(c, 2, static_cast<int>(n));
        pos += n;
    }
}
inline double lufsOf(const std::vector<float>& L, const std::vector<float>& R, size_t a = 0, size_t b = 0) {
    if (b == 0 || b > L.size()) b = L.size();
    sw::IntegratedLoudness m; m.setup(kFs, 2, 0.0);
    for (size_t i = a; i < b; i += 4096) {
        const int n = static_cast<int>(std::min<size_t>(4096, b - i));
        const float* c[2] = {L.data() + i, R.data() + i};
        m.process(c, 2, n);
    }
    return m.integrated();
}
inline void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) f.put(char((v >> (8 * k)) & 255)); };
    auto u16 = [&](uint16_t v) { f.put(char(v & 255)); f.put(char(v >> 8)); };
    const uint32_t n = static_cast<uint32_t>(l.size()), bytes = n * 2 * 3;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(kFs)); u32(static_cast<uint32_t>(kFs) * 6); u16(6); u16(24);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float x : {l[i], r[i]}) {
            const int32_t v = static_cast<int32_t>(std::lround(std::clamp(static_cast<double>(x), -1.0, 1.0) * 8388607.0));
            f.put(char(v & 255)); f.put(char((v >> 8) & 255)); f.put(char((v >> 16) & 255));
        }
}
inline std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
// every track's values and events, sorted (for the video); the times in beats, the tempo given
inline void writeEvents(const std::string& path, const Song& s) {
    std::ofstream f(path);
    char buf[64];
    f << "{\"title\":" << jsonStr(s.title) << ",\"style\":" << jsonStr(s.style) << ",\"bpm\":" << s.bpm << ",\"bars\":" << s.bars << ",\"sections\":[";
    for (size_t i = 0; i < s.sections.size(); ++i)
        f << (i ? "," : "") << "{\"name\":" << jsonStr(s.sections[i].name) << ",\"bar0\":" << s.sections[i].bar0 << ",\"bar1\":" << s.sections[i].bar1 << "}";
    f << "],\"tracks\":[";
    for (size_t t = 0; t < s.tracks.size(); ++t) {
        const Track& k = s.tracks[t];
        f << (t ? "," : "") << "\n{\"name\":" << jsonStr(k.name) << ",\"preset\":" << jsonStr(k.preset) << ",\"category\":" << jsonStr(k.category)
          << ",\"source\":" << jsonStr(k.source) << ",\"role\":" << jsonStr(k.role) << ",\"factory\":" << k.factoryIndex << ",\"gainDb\":" << k.gainDb << ",\"plain\":[";
        for (size_t i = 0; i < k.plain.size(); ++i) { std::snprintf(buf, sizeof buf, "%.9g", k.plain[i]); f << (i ? "," : "") << buf; }
        f << "],\"events\":[";
        for (size_t i = 0; i < k.ev.size(); ++i) { std::snprintf(buf, sizeof buf, "[%.9g,%d,%d,%.9g]", k.ev[i].beat, k.ev[i].kind, k.ev[i].a, k.ev[i].v); f << (i ? "," : "") << buf; }
        f << "]}";
    }
    f << "]}\n";
}

// render, mix and master a song; the master written to out (24-bit WAV; "-": nothing rendered). SW_SONG_STEMS=<dir>: each track as it goes into the mix (its gain,
// before the duck); SW_SONG_EVENTS=<file>: the values and events (writeEvents)
inline int make(Song& s, const std::string& out, double target) {
    const double beat = 60.0 / s.bpm;
    const long long total = std::llround((s.bars * 4 * beat + s.tail) * kFs);
    for (Track& t : s.tracks)
        std::stable_sort(t.ev.begin(), t.ev.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat || (a.beat == b.beat && a.kind < b.kind); });
    if (const char* evf = std::getenv("SW_SONG_EVENTS")) writeEvents(evf, s);
    if (out == "-") return 0;   // the events only
    for (Track& t : s.tracks) { render(t, total, s.bpm); std::fprintf(stderr, "rendered %s\n", t.name.c_str()); }

    // ---- the duck: every note of the duck track pulls the music down by its track's depth, back over duckRelease (3 ms in)
    std::vector<double> duck(static_cast<size_t>(total), 0.0);   // 0..1 of the depth
    for (const Track& k : s.tracks) {
        if (k.name != s.duckBy) continue;
        for (const Ev& e : k.ev) {
            if (e.kind != NoteOn) continue;
            const long long s0 = std::llround(e.beat * beat * kFs);
            for (long long i = s0; i < std::min<long long>(total, s0 + static_cast<long long>(5 * s.duckRelease * kFs)); ++i) {
                const double t = (i - s0) / kFs, w = std::min(1.0, t / 0.003) * std::exp(-t / s.duckRelease) * e.v;
                duck[static_cast<size_t>(i)] = std::max(duck[static_cast<size_t>(i)], w);
            }
        }
    }
    if (const char* stems = std::getenv("SW_SONG_STEMS")) {
        for (const Track& t : s.tracks) {
            std::string f = std::string(stems) + "/" + t.name + ".wav";
            for (auto& ch : f) if (ch == ' ' || ch == '(' || ch == ')') ch = '_';
            const float g = static_cast<float>(std::pow(10.0, t.gainDb / 20.0));
            std::vector<float> a = t.L, b = t.R; for (size_t i = 0; i < a.size(); ++i) { a[i] *= g; b[i] *= g; }
            writeWav(f, a, b);
        }
    }
    std::vector<float> L(static_cast<size_t>(total), 0.0f), R(static_cast<size_t>(total), 0.0f);
    std::printf("%-22s %8s %8s\n", "track", "gain dB", "LUFS");
    for (const Track& t : s.tracks) {
        const double g = std::pow(10.0, t.gainDb / 20.0);
        for (size_t i = 0; i < L.size(); ++i) {
            const double d = t.duckDb > 0 ? std::pow(10.0, -t.duckDb * duck[i] / 20.0) : 1.0;
            L[i] += static_cast<float>(t.L[i] * g * d); R[i] += static_cast<float>(t.R[i] * g * d);
        }
        std::printf("%-22s %8.1f %8.1f\n", t.name.c_str(), t.gainDb, lufsOf(t.L, t.R) + t.gainDb);
    }
    // ---- the master: a 20 Hz high-pass (2nd order, Butterworth: the drives leave a little DC, nothing below 20 Hz is music), SW MS04 Clipper
    // shaves the transients (the mix set to a -6 dBFS peak, clipDrive into a -0.3 dB ceiling, half-soft knee), then SW MS01 Maximizer takes
    // the loudest loud section to the target (its Gain found by measuring)
    {
        const double w = std::tan(3.14159265358979 * 20.0 / kFs), k = 1.0 / (1.0 + std::sqrt(2.0) * w + w * w);
        const double b0 = k, b1 = -2 * k, b2 = k, a1 = 2 * (w * w - 1) * k, a2 = (1 - std::sqrt(2.0) * w + w * w) * k;
        for (auto* ch : {&L, &R}) {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : *ch) { const double x = v, y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; v = static_cast<float>(y); }
        }
    }
    double inPk = 0; for (size_t i = 0; i < L.size(); ++i) inPk = std::max({inPk, static_cast<double>(std::abs(L[i])), static_cast<double>(std::abs(R[i]))});
    const float pre = static_cast<float>(std::pow(10.0, -6.0 / 20.0) / inPk);
    for (size_t i = 0; i < L.size(); ++i) { L[i] *= pre; R[i] *= pre; }
    auto stage = [](auto& proc, std::vector<float>& l, std::vector<float>& r) {   // a processor over the whole song, its latency taken out
        const int lat = proc.latencySamples();
        l.resize(l.size() + static_cast<size_t>(lat), 0.0f); r.resize(r.size() + static_cast<size_t>(lat), 0.0f);
        for (size_t i = 0; i < l.size(); i += 4096) {
            const int n = static_cast<int>(std::min<size_t>(4096, l.size() - i));
            float* c[2] = {l.data() + i, r.data() + i};
            proc.process(c, 2, n);
        }
        l.erase(l.begin(), l.begin() + lat); r.erase(r.begin(), r.begin() + lat);
    };
    std::vector<float> CL = L, CR = R;
    {
        sw::ms04::Processor c;
        c.setParam(sw::ms04::Drive, s.clipDrive); c.setParam(sw::ms04::Ceiling, -0.3); c.setParam(sw::ms04::Knee, 50); c.setParam(sw::ms04::GainMatch, 0);
        c.prepare(kFs, 4096);
        c.snapToTargets();
        stage(c, CL, CR);
    }
    const auto sec = [&](int bar) { return static_cast<size_t>(bar * 4 * beat * kFs); };
    auto loudLufs = [&](const std::vector<float>& l, const std::vector<float>& r) {
        double m = -200;
        for (const Section& x : s.sections) if (x.loud) m = std::max(m, lufsOf(l, r, sec(x.bar0), sec(x.bar1)));
        return m > -199 ? m : lufsOf(l, r);
    };
    const double clipLufs = loudLufs(CL, CR);
    double gain = std::clamp(target - clipLufs, 0.0, 24.0);
    std::vector<float> OL, OR;
    for (int it = 0; it < 12; ++it) {
        sw::ms01::Processor m;
        m.setParam(sw::ms01::Gain, gain); m.setParam(sw::ms01::Ceiling, -1.0); m.setParam(sw::ms01::TruePeak, 1); m.setParam(sw::ms01::Release, s.limiterRelease);
        m.setParam(sw::ms01::LowGuard, 1); m.setParam(sw::ms01::Dither, 24); m.setParam(sw::ms01::CharX, s.charX);
        m.prepare(kFs, 4096);
        m.snapToTargets();
        OL = CL; OR = CR;
        stage(m, OL, OR);
        const double got = loudLufs(OL, OR);
        std::fprintf(stderr, "master: gain %.2f dB -> loud sections %.2f LUFS\n", gain, got);
        if (std::abs(got - target) < 0.1) break;
        gain = std::clamp(gain + 1.4 * (target - got), 0.0, 24.0);   // the limiter gives back less than the gain put in
    }
    std::printf("loud sections: mix %.2f LUFS; clipper (+%.0f dB drive) %.2f LUFS: %.1f LU taken off; limiter (+%.2f dB) %.2f LUFS: %.1f LU taken off\n",
                loudLufs(L, R), s.clipDrive, clipLufs, loudLufs(L, R) + s.clipDrive - clipLufs, gain, loudLufs(OL, OR), clipLufs + gain - loudLufs(OL, OR));
    const size_t fade = static_cast<size_t>(s.fadeOut * kFs);
    for (size_t k = 0; k < fade && k < OL.size(); ++k) { const float g = static_cast<float>(k) / fade; OL[OL.size() - 1 - k] *= g; OR[OR.size() - 1 - k] *= g; }
    double pk = 0; for (size_t i = 0; i < OL.size(); ++i) pk = std::max({pk, static_cast<double>(std::abs(OL[i])), static_cast<double>(std::abs(OR[i]))});
    std::printf("master: %.2f LUFS (loud sections' target %.1f), sample peak %.2f dBFS, MS01 gain %.2f dB; %.1f s\n",
                lufsOf(OL, OR), target, 20 * std::log10(pk + 1e-12), gain, OL.size() / kFs);
    for (const Section& x : s.sections)
        std::printf("  %-10s bars %2d-%2d  %6.1f LUFS\n", x.name.c_str(), x.bar0, x.bar1 - 1, lufsOf(OL, OR, sec(x.bar0), sec(x.bar1)));
    writeWav(out, OL, OR);
    return 0;
}

}  // namespace sw::in07::songkit
