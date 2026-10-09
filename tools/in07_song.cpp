// SWINGBY (SW IN07): a dubstep demo track made only with SWINGBY (2026-10-10, the client: 実際にダブステップつくってみてよ, then: the
// aggressive early-2010s style, "brostep"). An original piece: the style's ways only (a different bass sound every half beat, metallic FM
// screeches, a talking vowel bass, pitch dives, stutters, laser zaps, hard stops, a big snare), no melody or riff of any record. Every sound is a
// SWINGBY instance: the drums too (a kick from a sine falling ~3.5 octaves on four Env 2 -> Pitch slots, a snare from a triangle body and the
// Static noise sample, hats from Tick and Static), the factory presets (pad, pluck with the arpeggiator, impact, downlifter), the ULTRA pack
// (docs/dist/in07/ultra) and a riser written here. Each track is one instance fed the host's transport (140 bpm), so the arpeggiator, the
// trance gate and the synced LFOs run on the beat. The mix: track levels, a kick-keyed duck on the music (the "sidechain"), the sum through
// SW MS01 Maximizer (this bundle's mastering limiter) to the loudness target, ceiling -1 dBTP.
// Form (bars of 4 beats, F minor, Fm7 - Db - Ab - Eb): intro 8, build 8, drop 16 (the wobble answering), break 8, drop 16 (the riddim
// answering, the saw hook), outro 4. The drops' roots: F F Db Eb, a bar each.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts -Itools tools/in07_song.cpp products/in07/*.cpp products/ms01/ms01.cpp core/src/*.cpp
//       core/third_party/monocypher/*.c -o build/in07_song
//   build/in07_song <out.wav> [the drops' loudness, LUFS: -9 (past it the limiter only crushes: about 0.05 LU more per dB of gain)]
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

using namespace sw::in07;
using namespace sw::in07::dsl;
using namespace sw::in07::recipe;

namespace {
constexpr double kFs = 48000.0, kBpm = 140.0, kBeat = 60.0 / kBpm;
constexpr int kBars = 60;
constexpr int kKickKey = 34;   // B flat 1, 58 Hz
constexpr double kClipDrive = 10.0;   // dB into the clipper: the mix peaks at -6 dBFS, so about 4 dB of the transients are shaved
constexpr double kTail = 4.0;   // seconds after the last bar

// ---- the instruments written here
const Raw& kickRecipe() {
    static const Raw r = R("Kick", "FX", N().drv(25, 40)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 100).mod(3, "Env 2", "Pitch", 100).mod(4, "Env 2", "Pitch", 60)   // +43 st at the start
        .L(1, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 20, 0) + fenv(0.5, 55, 0, 40) + amp(0.5, 380, 0, 60, 30))
        .L(2, smp("Knock", 0) + flt("HP 12", 1500, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 25, 0, 20, 30) + lvl(-10)));
    return r;
}
const Raw& snareRecipe() {
    static const Raw r = R("Snare", "FX", N().drv(30, 60).eq(0, 2, 3).rev(1.3, 18, 40)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 50)   // the body falls 18 semitones in its first 25 ms
        .L(1, wave("Triangle", 0, 1, 0, 0) + flt("LP 12", 8000, 0, 0, 30, 0) + fenv(0.5, 25, 0, 20) + amp(0.5, 120, 0, 60, 40))
        .L(2, smp("Static", 0, 2, 60) + flt("BP 12", 3500, 10, 0, 20, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 220, 0, 80, 40) + lvl(-2))
        .L(3, smp("Knock", 1) + flt("HP 12", 800, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 60, 0, 40, 40) + lvl(-6)));
    return r;
}
const Raw& hatRecipe(bool open) {
    static const Raw closed = R("Hat", "FX", N()
        .L(1, smp("Tick", 1) + flt("HP 12", 7000, 10, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 35, 0, 30, 70))
        .L(2, smp("Static", 2, 1) + flt("HP 12", 9000, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 25, 0, 20, 70) + lvl(-6)));
    static const Raw opened = R("Open hat", "FX", N()
        .L(1, smp("Tick", 1) + flt("HP 12", 7000, 10, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 60, 0, 60, 70))
        .L(2, smp("Static", 2, 1) + flt("HP 12", 8000, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 220, 0, 150, 70) + lvl(-4)));
    return open ? opened : closed;
}
// the brostep basses written here (the ULTRA pack's Ultra Bass, Ultra Wobble and Ultra Riddim play with them)
// a metallic screech: FM (feedback) and a hard-sync table an octave up, band-passed and driven; a random FM brightness every 1/16, the
// filter and the tables (the sync sweep: a held sync table rings on one harmonic) on a 1/8; the trance gate (1/32, every other step) is
// switched on by the arrangement for stutters
const Raw& screechRecipe() {
    static const Raw r = R("Screech", "BASS", N().mono().drv(80, 100).eq(-4, 4, 6)
        .g({{"bend", 12}, {"gate.on", 0}, {"gate.rate", "1/32"}, {"gate.depth", 100},
            {"gate.step2", 0}, {"gate.step4", 0}, {"gate.step6", 0}, {"gate.step8", 0}, {"gate.step10", 0}, {"gate.step12", 0}, {"gate.step14", 0}, {"gate.step16", 0}})
        .lfoSync(1, "Random", "1/16").lfoSync(2, "Triangle", "1/8", true)
        .mod(1, "LFO 1", "FM index", 40).mod(2, "LFO 2", "Cutoff", 35).mod(3, "Mod wheel", "WT position", 40).mod(4, "LFO 2", "WT position", 50)
        .L(1, fm("2", 75, 10000, 80, 0, 2, 15, 60) + flt("BP 12", 2200, 45, 0, 90, 0) + amp(0.5, 200, 100, 40, 10))
        .L(2, wt("Sync", 50, 1, 3, 20, 80) + flt("LP 24", 5000, 30, 0, 80, 0) + amp(0.5, 200, 100, 40, 10) + lvl(-6))
        .L(3, wt("Formant", 60, 0, 2, 10, 70) + flt("BP 12", 1400, 40, 0, 60, 0) + amp(0.5, 200, 100, 40, 10) + lvl(-4)));
    return r;
}
// the talking bass ("yoi"): two formant tables swept by a saw LFO restarted with each note (1/4), with an FM growl and a sine sub; legato
const Raw& yoiRecipe() {
    static const Raw r = R("Yoi", "BASS", N().legato(60).drv(70, 90).eq(0, 3, 4).g({{"bend", 12}})
        .lfoSync(1, "Saw", "1/4", true).mod(1, "LFO 1", "WT position", 70).mod(2, "LFO 1", "Cutoff", 30)
        .L(1, wt("Formant", 20, 0, 2, 12, 40) + flt("LP 24", 3000, 35, 0, 80, 0) + amp(1, 300, 100, 60, 10))
        .L(2, wt("Formant", 50, 1, 2, 15, 80) + flt("BP 12", 1800, 40, 0, 50, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))
        .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 60, 10) + lvl(-6))
        .L(4, fm("1", 50, 10000, 50, 0, 1) + flt("LP 12", 2500, 20, 0, 80, 0) + amp(1, 300, 100, 60, 10) + lvl(-4)));
    return r;
}
// a laser zap: saws and FM falling three octaves in 120 ms (three Env 2 -> Pitch slots), a 1/16 echo
const Raw& laserRecipe() {
    static const Raw r = R("Laser", "FX", N().drv(40, 80).dly("1/16", 40, 25).rev(1.0, 15)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 100).mod(3, "Env 2", "Pitch", 100)
        .L(1, saw(0, 4, 30, 100) + flt("LP 12", 12000, 20, 0, 30, 0) + fenv(0.5, 120, 0, 50) + amp(0.5, 200, 0, 60, 30))
        .L(2, fm("2", 60, 300, 40, 0, 1) + flt("LP 12", 9000, 0, 0, 0, 0) + fenv(0.5, 120, 0, 50) + amp(0.5, 200, 0, 60, 30) + lvl(-4)));
    return r;
}
// a metal hit: two inharmonic-sounding FM pairs (high ratios, the index decaying fast) and a knock, driven, a short room
const Raw& metalRecipe() {
    static const Raw r = R("Metal", "FX", N().drv(60, 80).rev(1.2, 20)
        .L(1, fm("11", 70, 250, 30, 0, 1) + flt("HP 12", 400, 0, 0, 0, 0) + amp(0.5, 300, 0, 100, 20))
        .L(2, fm("7", 60, 400, 50, -1, 1) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(0.5, 350, 0, 100, 20) + lvl(-3))
        .L(3, smp("Knock", 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(0.5, 60, 0, 40, 20) + lvl(-6)));
    return r;
}

// the riser: noise through a band-pass and a saw stack, both opened by automation, the pitch bent up an octave (bend range 12)
const Raw& riserRecipe() {
    static const Raw r = R("Riser", "FX", N().rev(4, 40).dly("1/8 D", 35, 20).g({{"bend", 12}})
        .L(1, smp("Static", 0, 2, 100) + flt("BP 12", 400, 30, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(1500, 100, 100, 300))
        .L(2, saw(0, 6, 40, 100) + flt("LP 24", 800, 20, 0, 30, 0) + amp(3000, 100, 100, 300) + lvl(-8)));
    return r;
}

// ---- the arrangement
enum Kind { NoteOff = 0, SetParam = 1, ModWheel = 2, EvBend = 3, NoteOn = 4 };   // at the same time: offs, values, then ons
struct Ev { double beat; int kind; int a; double v; };
struct Track {
    std::string name;
    std::vector<double> plain;
    double gainDb = 0;
    double duckDb = 0;   // the kick-keyed duck on this track
    std::vector<Ev> ev;
    std::vector<float> L, R;
    void note(double beat, double len, int key, double vel = 0.85) { ev.push_back({beat, NoteOn, key, vel}); ev.push_back({beat + len, NoteOff, key, 0}); }
    void chord(double beat, double len, std::initializer_list<int> keys, double vel = 0.8) { for (int k : keys) note(beat, len, k, vel); }
    void chord(double beat, double len, const std::vector<int>& keys, double vel = 0.8) { for (int k : keys) note(beat, len, k, vel); }
    void wheel(double beat, double v) { ev.push_back({beat, ModWheel, 0, v}); }
    void param(double beat, int id, double v) { ev.push_back({beat, SetParam, id, v}); }
    void ramp(int id, double b0, double b1, double v0, double v1, double perBeat = 16) {
        const int n = std::max(1, static_cast<int>((b1 - b0) * perBeat));
        for (int i = 0; i <= n; ++i) param(b0 + (b1 - b0) * i / n, id, v0 + (v1 - v0) * i / n);
    }
    void bendRamp(double b0, double b1, double v0, double v1) {
        const int n = std::max(1, static_cast<int>((b1 - b0) * 16));
        for (int i = 0; i <= n; ++i) ev.push_back({b0 + (b1 - b0) * i / n, EvBend, 0, v0 + (v1 - v0) * i / n});
    }
};
double bt(int bar, double beat = 0) { return bar * 4.0 + beat; }

std::vector<double> factory(const std::string& name) {
    const auto& n = presetNames();
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == name) { std::vector<double> v; presetValues(static_cast<int>(i), v); return v; }
    std::fprintf(stderr, "no factory preset %s\n", name.c_str()); std::exit(1);
}
std::vector<double> userFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
    std::vector<double> v; sw::presetfile::Meta m; std::string err;
    if (!userPresetValues(ss.str(), v, m, err)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str()); std::exit(1); }
    return v;
}
std::vector<double> written(const Raw& r) { std::vector<double> v; if (!plainOf(r, v)) std::exit(1); return v; }

// one instance through the whole song, the host's transport playing from bar 0
void render(Track& t, long long total) {
    Processor p;
    recipe::apply(p, t.plain);
    p.prepare(kFs, 256);
    p.setTempo(kBpm);
    std::stable_sort(t.ev.begin(), t.ev.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat || (a.beat == b.beat && a.kind < b.kind); });
    t.L.assign(static_cast<size_t>(total), 0.0f); t.R.assign(static_cast<size_t>(total), 0.0f);
    auto at = [](double beat) { return std::llround(beat * kBeat * kFs); };
    size_t e = 0;
    for (long long pos = 0; pos < total;) {
        while (e < t.ev.size() && at(t.ev[e].beat) <= pos) {
            const Ev& x = t.ev[e++];
            if (x.kind == NoteOn) p.noteOn(x.a, x.v); else if (x.kind == NoteOff) p.noteOff(x.a);
            else if (x.kind == ModWheel) p.modWheel(x.v); else if (x.kind == EvBend) p.pitchBend(x.v); else p.setParam(x.a, x.v);
        }
        long long n = std::min<long long>(256, total - pos);
        if (e < t.ev.size()) n = std::max<long long>(1, std::min(n, at(t.ev[e].beat) - pos));
        p.setTransport(true, pos / kFs / kBeat);
        float* c[2] = {t.L.data() + pos, t.R.data() + pos};
        p.process(c, 2, static_cast<int>(n));
        pos += n;
    }
}
double lufsOf(const std::vector<float>& L, const std::vector<float>& R, size_t a = 0, size_t b = 0) {
    if (b == 0 || b > L.size()) b = L.size();
    sw::IntegratedLoudness m; m.setup(kFs, 2, 0.0);
    for (size_t i = a; i < b; i += 4096) {
        const int n = static_cast<int>(std::min<size_t>(4096, b - i));
        const float* c[2] = {L.data() + i, R.data() + i};
        m.process(c, 2, n);
    }
    return m.integrated();
}
void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
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
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: in07_song <out.wav> [target LUFS]\n"); return 2; }
    const double target = argc > 2 ? std::atof(argv[2]) : -9.0;
    const std::string ultra = "docs/dist/in07/ultra/";

    // the chords (Fm7, Db, Ab, Eb): the pad in octave 3, the saw and the arpeggio higher, close voice leading
    const std::vector<std::vector<int>> pad = {{53, 56, 60, 63}, {49, 53, 56, 60}, {44, 48, 51, 55}, {51, 55, 58, 63}};
    const std::vector<std::vector<int>> top = {{65, 68, 72}, {65, 68, 73}, {63, 68, 72}, {63, 67, 70}};

    Track kick{"kick", written(kickRecipe()), 4.0}, snare{"snare", written(snareRecipe()), 2.0};
    Track hat{"hat", written(hatRecipe(false)), -4.0}, ohat{"open hat", written(hatRecipe(true)), -7.0};
    Track riser{"riser", written(riserRecipe()), -6.0}, impact{"impact", factory("Impact Moon"), -3.0}, down{"downlifter", factory("Downlifter"), -6.0};
    Track padT{"pad (Glass Horizon)", factory("Glass Horizon"), -13.0, 5.0}, arp{"arp (Glass Pluck)", factory("Glass Pluck"), -16.0, 5.0};
    Track saw{"Ultra Saw", userFile(ultra + "Ultra Saw.swpreset"), -7.0, 5.0};
    Track bass{"Ultra Bass", userFile(ultra + "Ultra Bass.swpreset"), -1.0, 8.0}, wob{"Ultra Wobble", userFile(ultra + "Ultra Wobble.swpreset"), -1.0, 8.0};
    Track rid{"Ultra Riddim", userFile(ultra + "Ultra Riddim.swpreset"), 1.0, 8.0};
    Track scr{"Screech", written(screechRecipe()), -4.0, 6.0}, yoi{"Yoi", written(yoiRecipe()), -1.0, 8.0};
    Track zap{"Laser", written(laserRecipe()), -3.0}, metal{"Metal", written(metalRecipe()), -2.0};
    bass.plain[static_cast<size_t>(Bend)] = 12;   // Ultra Bass dives an octave on the bend
    // the pluck through SWINGBY's arpeggiator: up and down, 1/16, two octaves
    arp.plain[static_cast<size_t>(ArpOn)] = 1; arp.plain[static_cast<size_t>(ArpMode)] = 2; arp.plain[static_cast<size_t>(ArpRate)] = 1; arp.plain[static_cast<size_t>(ArpOctaves)] = 2;

    auto snareRoll = [&](int bar0, int bars, int key0) {   // quarters, eighths, sixteenths, thirty-seconds; louder and higher
        for (int b = 0; b < bars; ++b) {
            const double step = b < bars / 4 ? 1.0 : b < bars / 2 ? 0.5 : b < bars - 1 ? 0.25 : 0.125;
            const double end = b == bars - 1 ? 3.0 : 4.0;   // the last beat before a drop is silent
            for (double x = 0; x < end - 1e-9; x += step) {
                const double t = (b * 4.0 + x) / (bars * 4.0);
                snare.note(bt(bar0 + b, x), std::min(step, 0.25), key0 + static_cast<int>(t * 10), 0.45 + 0.5 * t);
            }
        }
    };

    // ---- intro (bars 0-7): the pad, the arpeggio from bar 2 opening up, hats from bar 4
    for (int c = 0; c < 4; ++c) padT.chord(bt(c * 2), 8.0 - 0.05, pad[static_cast<size_t>(c)], 0.7);
    for (int c = 1; c < 4; ++c) arp.chord(bt(c * 2), 8.0 - 0.02, top[static_cast<size_t>(c)], 0.7);
    arp.ramp(Macro1, bt(2), bt(8), 15, 50);
    for (int bar = 4; bar < 8; ++bar) for (int k = 0; k < 8; ++k) hat.note(bt(bar, k * 0.5), 0.25, 60, k % 2 ? 0.4 : 0.55);

    // ---- build (bars 8-15): the saw chords opening (Bright 0 -> 50), four on the floor, a snare roll, the riser; one beat of silence
    for (int c = 0; c < 4; ++c) saw.chord(bt(8 + c * 2), c == 3 ? 7.0 : 8.0 - 0.05, top[static_cast<size_t>(c)], 0.85);
    saw.ramp(Macro1, bt(8), bt(15, 3), 0, 50);
    const double sawLevel = saw.plain[static_cast<size_t>(Level)];   // its preset level; the build comes up 8 dB to it
    saw.ramp(Level, bt(8), bt(15, 3), sawLevel - 8, sawLevel);
    for (int c = 0; c < 3; ++c) padT.chord(bt(8 + c * 2), 8.0 - 0.05, pad[static_cast<size_t>(c)], 0.6);
    for (int c = 0; c < 3; ++c) arp.chord(bt(8 + c * 2), 8.0 - 0.02, top[static_cast<size_t>(c)], 0.7);
    for (int bar = 8; bar < 14; ++bar) for (int q = 0; q < 4; ++q) kick.note(bt(bar, q), 0.5, kKickKey, 0.55 + 0.03 * (bar - 8));   // growing
    snareRoll(8, 8, 55);
    riser.note(bt(8), 31.0, 53, 0.9);
    riser.ramp(lp(0, Cutoff), bt(8), bt(15, 3), 400, 9000, 8); riser.ramp(lp(1, Cutoff), bt(8), bt(15, 3), 800, 12000, 8);
    riser.ramp(Level, bt(8), bt(15, 3), -24, 0, 8);
    riser.bendRamp(bt(8), bt(15, 3), -1.0, 1.0);

    // ---- the drops: a different bass every half beat. Patterns of one bar on a root (A: growl with a dive, zap, gated screech, vowel, metal
    // on the snare, wobble; B: vowel, growl stutter, screech bent up, zap, growl dive, wobble; C: growl into a 1/64 stutter, gated screech,
    // metal and vowel, two zaps, then half a beat of nothing; D: the end of a phrase, all of it stopping for the last beat)
    auto dive = [&](Track& t, double b0, double b1) { t.bendRamp(b0, b1, 0.0, -1.0); t.ev.push_back({b1 + 0.02, EvBend, 0, 0.0}); };
    auto stutter = [&](Track& t, double b, double len, int key, double step) { for (double x = 0; x < len - 1e-9; x += step) t.note(b + x, step * 0.6, key, 0.9); };
    auto gated = [&](double b, double len, int key) { scr.param(b, GateOn, 1); scr.note(b, len, key, 0.9); scr.param(b + len, GateOn, 0); };
    auto answer = [&](Track& t, double b, double len, int key, bool fast) { if (fast) t.wheel(b, 1.0); t.note(b, len, key, 0.9); if (fast) t.wheel(b + len, 0.0); };
    auto barA = [&](int bar, int r, Track& w) {
        const double b = bt(bar);
        bass.note(b, 0.7, r, 0.95); dive(bass, b + 0.45, b + 0.7);
        zap.note(b + 0.75, 0.2, 65, 0.9);
        gated(b + 1.0, 0.5, r + 24);
        yoi.note(b + 1.5, 0.45, r, 0.9);
        metal.note(b + 2.0, 0.25, r + 12, 0.9); bass.note(b + 2.0, 0.45, r + 3, 0.9);
        yoi.note(b + 2.5, 0.45, r + 7, 0.9);
        answer(w, b + 3.0, 0.7, r, true);
    };
    auto barB = [&](int bar, int r, Track& w) {
        const double b = bt(bar);
        yoi.note(b, 0.95, r, 0.9);
        stutter(bass, b + 1.0, 0.5, r, 0.125); bass.note(b + 1.5, 0.45, r + 3, 0.9);
        scr.ev.push_back({b + 2.0, EvBend, 0, -0.5}); scr.note(b + 2.0, 0.7, r + 27, 0.9); scr.bendRamp(b + 2.0, b + 2.3, -0.5, 0.0);
        zap.note(b + 2.75, 0.2, 68, 0.9);
        bass.note(b + 3.0, 0.45, r + 7, 0.9); dive(bass, b + 3.2, b + 3.45);
        answer(w, b + 3.5, 0.45, r, true);
    };
    auto barC = [&](int bar, int r, Track& w) {
        const double b = bt(bar);
        bass.note(b, 0.45, r, 0.95); stutter(bass, b + 0.5, 0.5, r, 0.0625);
        gated(b + 1.0, 0.95, r + 24);
        metal.note(b + 2.0, 0.25, r + 12, 0.9); yoi.note(b + 2.0, 0.7, r, 0.9);
        zap.note(b + 3.0, 0.2, 70, 0.9); zap.note(b + 3.25, 0.2, 65, 0.9);
        (void)w;   // 3.5 .. 4: nothing
    };
    auto barD = [&](int bar, int r, Track& w) {
        const double b = bt(bar);
        yoi.note(b, 0.95, r, 0.9);
        stutter(bass, b + 1.0, 1.0, r + 3, 0.125);
        answer(w, b + 2.0, 0.95, r, true);
        zap.note(b + 3.0, 0.4, 72, 0.9);   // and then the last beat stops
    };
    auto drop = [&](int bar0, Track& w) {
        const int roots8[8] = {29, 29, 25, 27, 29, 29, 25, 27};
        for (int k = 0; k < 16; ++k) {
            const int bar = bar0 + k, r = roots8[k % 8];
            switch (k % 8) {
                case 0: case 4: barA(bar, r, w); break;
                case 1: case 5: barB(bar, r, w); break;
                case 2: case 6: barC(bar, r, w); break;
                default: if (k == 7 || k == 15) barD(bar, r, w); else barC(bar, r, w); break;
            }
        }
    };
    auto drums = [&](int bar, bool busy, bool stopLast) {   // half time; a phrase's last bar stops on its last beat (but for a snare fill)
        kick.note(bt(bar), 0.5, kKickKey, 0.95);
        if (bar % 2 == 1) kick.note(bt(bar, 2.75), 0.25, kKickKey, 0.8); else kick.note(bt(bar, 1.75), 0.25, kKickKey, 0.7);
        snare.note(bt(bar, 2), 0.5, 55, 0.95);
        for (int k = 0; k < 8; ++k) {
            if (stopLast && k >= 6) break;
            if (k == 7) { ohat.note(bt(bar, 3.5), 0.5, 60, 0.7); continue; }
            hat.note(bt(bar, k * 0.5), 0.25, 60, k % 2 ? 0.55 : 0.8);
            if (busy && (k == 2 || k == 6)) hat.note(bt(bar, k * 0.5 + 0.25), 0.25, 60, 0.45);
        }
        if (stopLast) for (int k = 0; k < 4; ++k) snare.note(bt(bar, 3.0 + k * 0.125), 0.1, 58 + k, 0.6 + 0.1 * k);
    };

    // ---- drop 1 (bars 16-31): the impact, then the patterns, Ultra Wobble answering; saw stabs in the second half
    impact.note(bt(16), 2.0, 60, 0.9);
    for (int bar = 16; bar < 32; ++bar) drums(bar, bar >= 24, (bar - 16) % 8 == 7);
    drop(16, wob);
    saw.param(bt(16), Macro1, 50); saw.param(bt(16), Level, sawLevel);
    for (int pr = 4; pr < 8; ++pr) saw.chord(bt(16 + pr * 2), 1.0, top[static_cast<size_t>(pr % 4)], 0.8);

    // ---- break (bars 32-39): the pad, the arpeggio, the saw soft; hats back at 36, a shorter roll and riser into drop 2
    for (int c = 0; c < 4; ++c) padT.chord(bt(32 + c * 2), 8.0 - 0.05, pad[static_cast<size_t>(c)], 0.7);
    for (int c = 0; c < 4; ++c) arp.chord(bt(32 + c * 2), 8.0 - 0.02, top[static_cast<size_t>(c)], 0.7);
    saw.param(bt(32), Macro1, 25); saw.param(bt(32), Level, sawLevel - 6);
    saw.ramp(Level, bt(36), bt(39, 3), sawLevel - 6, sawLevel);
    for (int c = 0; c < 4; ++c) saw.chord(bt(32 + c * 2), c == 3 ? 7.0 : 8.0 - 0.05, top[static_cast<size_t>(c)], 0.6);
    saw.ramp(Macro1, bt(36), bt(39, 3), 25, 50);
    for (int bar = 36; bar < 40; ++bar) for (int k = 0; k < 8; ++k) hat.note(bt(bar, k * 0.5), 0.25, 60, k % 2 ? 0.45 : 0.6);
    snareRoll(36, 4, 57);
    riser.note(bt(36), 15.0, 53, 0.9);
    riser.param(bt(36), lp(0, Cutoff), 400); riser.param(bt(36), lp(1, Cutoff), 800); riser.param(bt(36), Level, -24);
    riser.ramp(lp(0, Cutoff), bt(36), bt(39, 3), 400, 9000, 8); riser.ramp(lp(1, Cutoff), bt(36), bt(39, 3), 800, 12000, 8);
    riser.ramp(Level, bt(36), bt(39, 3), -24, 0, 8);
    riser.bendRamp(bt(36), bt(39, 3), -1.0, 1.0);

    // ---- drop 2 (bars 40-55): the same patterns with Ultra Riddim answering (gated on the beat); the saw hook over it from bar 48
    impact.note(bt(40), 2.0, 60, 0.9);
    for (int bar = 40; bar < 56; ++bar) drums(bar, true, (bar - 40) % 8 == 7);
    drop(40, rid);
    saw.param(bt(40), Macro1, 50);
    const struct { double b, len; int key; } hook[] = {{0, 0.75, 72}, {0.75, 0.75, 75}, {1.5, 1.0, 77}, {2.5, 0.5, 75}, {3.0, 1.0, 72},
                                                       {4, 1.5, 68}, {5.5, 0.5, 70}, {6, 1.9, 72}};
    for (int rep = 0; rep < 4; ++rep) for (const auto& h : hook) saw.note(bt(48 + rep * 2, h.b), h.len - 0.03, h.key, 0.9);

    // ---- outro (bars 56-59): the pad on F minor, the downlifter, the arpeggio fading
    padT.chord(bt(56), 15.5, pad[0], 0.7);
    arp.chord(bt(56), 8.0, top[0], 0.6);
    arp.ramp(Macro1, bt(56), bt(58), 50, 10);
    down.note(bt(56), 4.0, 60, 0.9);

    // ---- render every track
    const long long total = std::llround((kBars * 4 * kBeat + kTail) * kFs);
    std::vector<Track*> tracks = {&kick, &snare, &hat, &ohat, &riser, &impact, &down, &padT, &arp, &saw, &bass, &wob, &rid, &scr, &yoi, &zap, &metal};
    for (Track* t : tracks) { render(*t, total); std::fprintf(stderr, "rendered %s\n", t->name.c_str()); }

    // ---- the duck: every kick pulls the music down by its track's depth, back over ~120 ms (3 ms in)
    std::vector<double> duck(static_cast<size_t>(total), 0.0);   // 0..1 of the depth
    for (const Ev& e : kick.ev) {
        if (e.kind != NoteOn) continue;
        const long long s0 = std::llround(e.beat * kBeat * kFs);
        for (long long i = s0; i < std::min<long long>(total, s0 + static_cast<long long>(0.6 * kFs)); ++i) {
            const double t = (i - s0) / kFs, w = std::min(1.0, t / 0.003) * std::exp(-t / 0.12) * e.v;
            duck[static_cast<size_t>(i)] = std::max(duck[static_cast<size_t>(i)], w);
        }
    }
    if (const char* stems = std::getenv("SW_SONG_STEMS")) {   // each track as it goes into the mix (its gain, before the duck), for checks
        for (Track* t : tracks) {
            std::string f = std::string(stems) + "/" + t->name + ".wav";
            for (auto& ch : f) if (ch == ' ' || ch == '(' || ch == ')') ch = '_';
            const float g = static_cast<float>(std::pow(10.0, t->gainDb / 20.0));
            std::vector<float> a = t->L, b = t->R; for (size_t i = 0; i < a.size(); ++i) { a[i] *= g; b[i] *= g; }
            writeWav(f, a, b);
        }
    }
    std::vector<float> L(static_cast<size_t>(total), 0.0f), R(static_cast<size_t>(total), 0.0f);
    std::printf("%-22s %8s %8s\n", "track", "gain dB", "LUFS");
    for (Track* t : tracks) {
        const double g = std::pow(10.0, t->gainDb / 20.0);
        for (size_t i = 0; i < L.size(); ++i) {
            const double d = t->duckDb > 0 ? std::pow(10.0, -t->duckDb * duck[i] / 20.0) : 1.0;
            L[i] += static_cast<float>(t->L[i] * g * d); R[i] += static_cast<float>(t->R[i] * g * d);
        }
        std::printf("%-22s %8.1f %8.1f\n", t->name.c_str(), t->gainDb, lufsOf(t->L, t->R) + t->gainDb);
    }
    // ---- the master: SW MS04 Clipper shaves the transients first (the mix set to a -6 dBFS peak, kClipDrive into a -0.3 dB ceiling,
    // half-soft knee), then SW MS01 Maximizer takes it to the loudness target (its Gain found by measuring the whole song)
    {   // a 20 Hz high-pass (2nd order, Butterworth) on the sum: the drives leave a little DC and nothing below 20 Hz is music
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
        c.setParam(sw::ms04::Drive, kClipDrive); c.setParam(sw::ms04::Ceiling, -0.3); c.setParam(sw::ms04::Knee, 50); c.setParam(sw::ms04::GainMatch, 0);
        c.prepare(kFs, 4096);
        c.snapToTargets();
        stage(c, CL, CR);
    }
    const auto sec = [](int bar) { return static_cast<size_t>(bar * 4 * kBeat * kFs); };
    auto dropLufs = [&](const std::vector<float>& l, const std::vector<float>& r) { return std::max(lufsOf(l, r, sec(16), sec(32)), lufsOf(l, r, sec(40), sec(56))); };
    const double clipLufs = dropLufs(CL, CR);
    double gain = std::clamp(target - clipLufs, 0.0, 24.0);
    std::vector<float> OL, OR;
    for (int it = 0; it < 12; ++it) {
        sw::ms01::Processor m;
        m.setParam(sw::ms01::Gain, gain); m.setParam(sw::ms01::Ceiling, -1.0); m.setParam(sw::ms01::TruePeak, 1); m.setParam(sw::ms01::Release, 120);
        m.setParam(sw::ms01::LowGuard, 1); m.setParam(sw::ms01::Dither, 24); m.setParam(sw::ms01::CharX, 20);   // a light slow stage
        m.prepare(kFs, 4096);
        m.snapToTargets();
        OL = CL; OR = CR;
        stage(m, OL, OR);
        const double got = dropLufs(OL, OR);
        std::fprintf(stderr, "master: gain %.2f dB -> drops %.2f LUFS\n", gain, got);
        if (std::abs(got - target) < 0.1) break;
        gain = std::clamp(gain + 1.4 * (target - got), 0.0, 24.0);   // the limiter gives back less than the gain put in
    }
    std::printf("drops: mix %.2f LUFS; clipper (+%.0f dB drive) %.2f LUFS: %.1f LU taken off; limiter (+%.2f dB) %.2f LUFS: %.1f LU taken off\n",
                dropLufs(L, R), kClipDrive, clipLufs, dropLufs(L, R) + kClipDrive - clipLufs, gain, dropLufs(OL, OR), clipLufs + gain - dropLufs(OL, OR));
    // a short fade at the very end
    const size_t fade = static_cast<size_t>(1.5 * kFs);
    for (size_t k = 0; k < fade; ++k) { const float g = static_cast<float>(k) / fade; OL[OL.size() - 1 - k] *= g; OR[OR.size() - 1 - k] *= g; }
    double pk = 0; for (size_t i = 0; i < OL.size(); ++i) pk = std::max({pk, static_cast<double>(std::abs(OL[i])), static_cast<double>(std::abs(OR[i]))});
    std::printf("master: %.2f LUFS (target %.1f), sample peak %.2f dBFS, MS01 gain %.2f dB; drop 1 %.1f LUFS, drop 2 %.1f LUFS, intro %.1f LUFS; %.1f s\n",
                lufsOf(OL, OR), target, 20 * std::log10(pk + 1e-12), gain, lufsOf(OL, OR, sec(16), sec(32)), lufsOf(OL, OR, sec(40), sec(56)),
                lufsOf(OL, OR, sec(0), sec(8)), OL.size() / kFs);
    writeWav(argv[1], OL, OR);
    return 0;
}
