// SWINGBY (SW IN07): a preset pack outside the factory list — "ULTRA" (2026-10-10, the client: ゴリゴリの ultrasaw と ultrabass、ダブステップで
//使えるもの). The recipes are written in the factory tables' language (products/in07/preset_dsl.hpp), so a preset can move into the factory
// list as it is. Each one is levelled as the factory presets are (tools/in07_presets.cpp: staging at kPresetStageLufs, output at
// kPresetTargetLufs, on its category's audition phrase), written as a user preset (.swpreset), and played twice: the audition phrase, and a
// 140 bpm dubstep phrase (basses: a half-time riff on F1 with the mod wheel doubling the wobble in the second bar; leads: F minor chords).
// Printed: loudness, peaks, octave bands, the low end's width (side over mid under 120 Hz: a club needs it mono), the time per sample.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_pack.cpp products/in07/*.cpp -o build/in07_pack
//   build/in07_pack <out dir>      -> <out dir>/<Name>.swpreset, <out dir>/wav/<name>_{phrase,dubstep}.wav, <out dir>/wav/ultra_drop.wav
#include "in07_recipe.hpp"
#include "sw/fft.hpp"
#include "sw/loudness.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace sw::in07;
using namespace sw::in07::dsl;
using namespace sw::in07::recipe;

namespace {
constexpr double kFs = 48000.0;

// ---- the recipes
const std::vector<Raw>& pack() {
    static const std::vector<Raw> t = {
        // a festival supersaw, harder than Anthem Supersaw: three saw stacks of 8 (the middle one a hard-sync table for the bite), an octave
        // up and an octave down under them, each driven into its filter; then Drive, a presence EQ, a short delay and a room; the limiter
        // pushed for density. The mod wheel spreads the stacks further.
        R("Ultra Saw", "LEAD", N().drv(70, 90).eq(-3, 2, 5).dly("1/8 D", 25, 10).rev(1.4, 12)
            .g({{"fx.slot1", "Drive"}, {"fx.slot2", "EQ"}, {"fx.slot3", "Delay"}, {"fx.slot4", "Reverb"}, {"fx.slot5", "Chorus"}, {"fx.slot6", "Limit"},
                {"fx.limit.gain", 6}})
            .mod(1, "Mod wheel", "Detune", 30).mod(2, "Velocity", "Cutoff", 10)
            .L(1, wt("Classic", 0, 0, 8, 65, 100) + flt("LP 12", 14000, 0, 0, 70, 0) + amp(1, 300, 100, 250, 20))
            .L(2, wt("Sync", 35, 0, 8, 45, 90) + flt("LP 24", 7000, 10, 0, 60, 0) + amp(1, 300, 100, 250, 20) + lvl(-5))
            .L(3, saw(1, 8, 55, 100) + flt("LP 12", 18000, 0, 0, 50, 0) + amp(1, 300, 100, 250, 20) + lvl(-5))
            .L(4, saw(-1, 4, 20, 40) + flt("LP 24", 2200, 0, 0, 50, 0) + amp(1, 300, 100, 250, 20) + lvl(-7))),

        // a heavy growl: a sine sub (one voice, centre, kept under the growl) and an FM growl with feedback (one voice, centre) carry the low
        // end, so it stays mono for a club; a folded table (3 voices, wide) and a formant table an octave up (the vowel) are band-passed,
        // so their width is in the mids and the top only. The filters do not follow the key (a low note stays bright). One LFO at 1/8 (from
        // the note) sweeps the filters, the tables and the FM index together, the mod wheel doubles it; Drive bright, an EQ that lifts the
        // mids and the top, the limiter. Legato with a short glide (the riff talks).
        R("Ultra Bass", "BASS", N().legato(35).drv(85, 100).eq(0, 4, 7)
            .g({{"fx.limit.gain", 4}})
            .lfoSync(1, "Triangle", "1/8", true).lfoSync(2, "Random", "1/16")
            .mod(1, "LFO 1", "Cutoff", 50).mod(2, "LFO 1", "WT position", 45).mod(3, "LFO 1", "FM index", 35)
            .mod(4, "Mod wheel", "LFO 1 rate", 25).mod(5, "LFO 2", "Resonance", 15)
            .L(1, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 10, 0) + amp(1, 300, 100, 60, 10) + lvl(-6))
            .L(2, fm("1", 80, 10000, 60, 0, 1) + flt("LP 12", 3000, 35, 0, 90, 0) + amp(1, 300, 100, 60, 10))
            .L(3, wt("Fold", 70, 0, 3, 22, 90) + flt("BP 12", 1500, 15, 0, 80, 0) + amp(1, 300, 100, 60, 10) + lvl(-2))
            .L(4, wt("Formant", 30, 1, 2, 15, 80) + flt("BP 12", 2000, 45, 0, 50, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))),

        // the classic wobble: detuned saws and a half-square table into resonant 24 dB filters (not following the key) that a 1/8 triangle
        // opens and shuts over 7 octaves, a sine sub under them (its filter stays open), Drive; the mod wheel doubles the wobble (1/16)
        R("Ultra Wobble", "BASS", N().mono().drv(75, 100).eq(0, 3, 6)
            .g({{"fx.limit.gain", 3}})
            .lfoSync(1, "Triangle", "1/8", true)
            .mod(1, "LFO 1", "Cutoff", 70).mod(2, "LFO 1", "Resonance", 15).mod(3, "Mod wheel", "LFO 1 rate", 25)
            .L(1, saw(0, 3, 15, 15) + flt("LP 24", 700, 55, 0, 80, 0) + amp(1, 300, 100, 60, 10))
            .L(2, wt("Classic", 50, 0, 2, 12, 25) + flt("LP 24", 800, 40, 0, 70, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))
            .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 60, 10) + lvl(-5))),

        // riddim: a square and a hard FM pair, chopped by the trance gate on the 1/16 (a riddim pattern, on the host's beat), the filter
        // alternating every 1/16 (a square LFO) and a new FM brightness every 1/8 (random); the sub under it, Drive to the edge
        R("Ultra Riddim", "BASS", N().mono().drv(90, 80).eq(0, 2, 4)
            .g({{"fx.limit.gain", 3}, {"gate.on", 1}, {"gate.rate", "1/16"}, {"gate.depth", 95},
                {"gate.step1", 1}, {"gate.step2", 0}, {"gate.step3", 1}, {"gate.step4", 1}, {"gate.step5", 0}, {"gate.step6", 1}, {"gate.step7", 1}, {"gate.step8", 0},
                {"gate.step9", 1}, {"gate.step10", 0}, {"gate.step11", 1}, {"gate.step12", 1}, {"gate.step13", 0}, {"gate.step14", 1}, {"gate.step15", 0}, {"gate.step16", 1}})
            .lfoSync(1, "Square", "1/16", true).lfoSync(2, "Random", "1/8", true)
            .mod(1, "LFO 1", "Cutoff", 35).mod(2, "LFO 2", "FM index", 40).mod(3, "Mod wheel", "Drive", 30)
            .L(1, pulse(50, 0, 2, 8, 20) + flt("LP 24", 3000, 25, 0, 95, 0) + amp(1, 300, 100, 50, 10))
            .L(2, fm("2", 60, 10000, 70, 0, 1) + flt("LP 24", 4000, 20, 0, 70, 0) + amp(1, 300, 100, 50, 10) + lvl(-4))
            .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 50, 10) + lvl(-5))),
    };
    return t;
}

// ---- playing a phrase: notes (start s, length s, key, velocity) and mod wheel moves (s, value), at a tempo
struct Note { double start, length; int key; double vel; };
struct Wheel { double at, v; };
struct Take { std::vector<float> L, R; double lufs = -200, monoLufs = -200, peakDb = -200, rawPeakDb = -200, nsPerSample = 0; };
Take play(const std::vector<double>& plain, const std::vector<Note>& notes, const std::vector<Wheel>& wheel, double seconds, double bpm, bool preFx = false) {
    Processor p;
    recipe::apply(p, plain);
    if (preFx) { for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0); p.setParam(Level, 0); }
    p.prepare(kFs, 256);
    p.setTempo(bpm);
    struct Ev { long long at; int kind; int key; double v; };   // kind 0 off, 1 wheel, 2 on (offs first at the same sample)
    std::vector<Ev> ev;
    for (const auto& n : notes) { ev.push_back({std::llround(n.start * kFs), 2, n.key, n.vel}); ev.push_back({std::llround((n.start + n.length) * kFs), 0, n.key, 0}); }
    for (const auto& w : wheel) ev.push_back({std::llround(w.at * kFs), 1, 0, w.v});
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at || (a.at == b.at && a.kind < b.kind); });
    const long long end = std::llround(seconds * kFs);
    Take t;
    t.L.assign(static_cast<size_t>(end), 0.0f); t.R.assign(static_cast<size_t>(end), 0.0f);
    sw::IntegratedLoudness st, mo;
    st.setup(kFs, 2, 0.0); mo.setup(kFs, 2, 0.0);
    std::vector<float> ml(256);
    double peak = 0;
    size_t e = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long pos = 0; pos < end;) {
        while (e < ev.size() && ev[e].at <= pos) {
            if (ev[e].kind == 2) p.noteOn(ev[e].key, ev[e].v); else if (ev[e].kind == 0) p.noteOff(ev[e].key); else p.modWheel(ev[e].v);
            ++e;
        }
        long long n = std::min<long long>(256, end - pos);
        if (e < ev.size()) n = std::min(n, ev[e].at - pos);
        const int k = static_cast<int>(n);
        float* c[2] = {t.L.data() + pos, t.R.data() + pos};
        p.process(c, 2, k);
        for (int i = 0; i < k; ++i) {
            peak = std::max({peak, static_cast<double>(std::abs(c[0][i])), static_cast<double>(std::abs(c[1][i]))});
            ml[static_cast<size_t>(i)] = 0.5f * (c[0][i] + c[1][i]);
        }
        const float* cs[2] = {c[0], c[1]};
        const float* cm[2] = {ml.data(), ml.data()};
        st.process(cs, 2, k); mo.process(cm, 2, k);
        pos += n;
    }
    t.nsPerSample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / static_cast<double>(end);
    t.lufs = st.integrated(); t.monoLufs = mo.integrated();
    t.peakDb = 20 * std::log10(peak + 1e-12);
    t.rawPeakDb = 20 * std::log10(p.limiterPeak() + 1e-12);
    return t;
}
std::vector<Note> auditionNotes(const std::string& cat, double& total) {
    std::vector<Note> v;
    for (const auto& n : audition(cat, total)) v.push_back({n.start, n.length, n.key, 0.8});
    return v;
}
// 140 bpm. Basses: two bars of half-time riff on F1, twice; the wheel up for the second bar of each (the wobble doubles). Leads: F minor
// chords, a bar each (Fm, Db, Eb, C).
constexpr double kBpm = 140.0, kBeat = 60.0 / kBpm;
void dubstep(const std::string& cat, std::vector<Note>& n, std::vector<Wheel>& w, double& seconds) {
    n.clear(); w.clear();
    if (cat == "BASS") {
        const struct { double b, len; int key; } riff[] = {{0, 1.5, 29}, {1.5, 0.5, 29}, {2.5, 0.5, 32}, {3, 1, 29},
                                                           {4, 1.5, 36}, {6, 0.5, 34}, {6.5, 0.5, 32}, {7, 1, 29}};
        for (int rep = 0; rep < 2; ++rep) {
            for (const auto& r : riff) n.push_back({(rep * 8 + r.b) * kBeat + 0.002, r.len * kBeat - 0.01, r.key, 0.9});
            w.push_back({(rep * 8 + 4) * kBeat, 1.0});
            w.push_back({(rep * 8 + 6) * kBeat, 0.0});
        }
        seconds = 16 * kBeat + 1.0;
    } else {
        const int chords[4][3] = {{65, 68, 72}, {61, 65, 68}, {63, 67, 70}, {60, 64, 67}};
        for (int c = 0; c < 4; ++c) for (int k : chords[c]) n.push_back({c * 4 * kBeat + 0.002, 4 * kBeat - 0.05, k, 0.85});
        seconds = 16 * kBeat + 1.5;
    }
}

// ---- numbers about a take: octave bands (31 Hz .. 16 kHz, dB of the total) and the low end's width (side over mid under 120 Hz, dB)
struct Shape { std::vector<double> bands; double lowWidth = 0, width = 0; };
Shape shape(const Take& t) {
    Shape s;
    const size_t n0 = t.L.size();
    int n = 1; while (static_cast<size_t>(n) < n0) n *= 2;
    std::vector<std::complex<double>> m(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    for (size_t i = 0; i < n0; ++i) { m[i] = 0.5 * (t.L[i] + t.R[i]); d[i] = 0.5 * (t.L[i] - t.R[i]); }
    sw::Fft f(n); f.forward(m); f.forward(d);
    std::vector<double> e(9, 1e-20); double tot = 1e-20, lm = 1e-20, ls = 1e-20, am = 1e-20, as = 1e-20;
    for (int k = 1; k < n / 2; ++k) {
        const double fr = k * kFs / n, pm = std::norm(m[static_cast<size_t>(k)]), ps = std::norm(d[static_cast<size_t>(k)]);
        tot += pm + ps; am += pm; as += ps;
        if (fr < 120) { lm += pm; ls += ps; }
        const int b = static_cast<int>(std::floor(std::log2(fr / 31.25)));
        if (b >= 0 && b < 9) e[static_cast<size_t>(b)] += pm + ps;
    }
    for (double v : e) s.bands.push_back(10 * std::log10(v / tot));
    s.lowWidth = 10 * std::log10(ls / lm);
    s.width = 10 * std::log10(as / am);
    return s;
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
std::string fileName(std::string s) { for (auto& c : s) c = c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: in07_pack <out dir>\n"); return 2; }
    namespace fs = std::filesystem;
    const fs::path out = argv[1];
    fs::create_directories(out / "wav");
    auto r2 = [](double x) { return std::round(x * 100.0) / 100.0; };
    std::printf("%-13s %-5s %6s %6s %5s | %6s %6s %6s %7s | %6s %7s %7s %6s | bands 31..16k (dB of total)\n",
                "preset", "cat", "trim", "level", "boost", "LUFS", "mono", "peak", "intoLim", "drop", "lowW", "width", "ns/s");
    std::map<std::string, Take> drops;
    for (const Raw& raw : pack()) {
        std::vector<double> plain;
        if (!plainOf(raw, plain)) return 1;
        double total = 0;
        const auto notes = auditionNotes(raw.cat, total);
        // staging, then the output (tools/in07_presets.cpp)
        PresetLevels lv;
        for (int it = 0; it < 8; ++it) {
            const double err = kPresetStageLufs - play(levelled(plain, lv), notes, {}, total, 120.0, true).lufs;
            if (std::abs(err) < 0.02) break;
            lv.trim += err;
        }
        lv.trim = r2(lv.trim);
        double c = kPresetTargetLufs - play(levelled(plain, lv), notes, {}, total, 120.0).lufs;
        if (c <= 0.0) lv.level = r2(c);
        else {
            for (int it = 0; it < 12 && std::abs(c) >= 0.05; ++it) {
                lv.boost = std::clamp(lv.boost + c, 0.0, 12.0);
                c = kPresetTargetLufs - play(levelled(plain, lv), notes, {}, total, 120.0).lufs;
            }
            lv.boost = r2(lv.boost);
        }
        const std::vector<double> fin = levelled(plain, lv);
        const Take a = play(fin, notes, {}, total + 2.0, 120.0);
        std::vector<Note> dn; std::vector<Wheel> dw; double secs = 0;
        dubstep(raw.cat, dn, dw, secs);
        const Take d = play(fin, dn, dw, secs, kBpm);
        const Shape sa = shape(a), sd = shape(d);
        std::printf("%-13s %-5s %6.2f %6.2f %5.2f | %6.2f %6.2f %6.2f %7.2f | %6.2f %7.1f %7.1f %6.0f |",
                    raw.name, raw.cat, lv.trim, lv.level, lv.boost, a.lufs, a.monoLufs, a.peakDb, a.rawPeakDb, d.lufs, sd.lowWidth, sd.width, d.nsPerSample);
        for (double b : sd.bands) std::printf(" %5.1f", b);
        std::printf("\n");
        (void)sa;
        sw::presetfile::Meta meta{raw.name, raw.cat, "SEVENTHWELL", "ULTRA pack (2026-10-10): for dubstep and other heavy styles. Levelled to -16 LUFS like the factory presets."};
        const std::string text = userPresetText(fin, meta);
        std::ofstream(out / (std::string(raw.name) + ".swpreset"), std::ios::binary) << text;
        {   // read back as the plug-in reads a user preset: the same values
            std::vector<double> back; sw::presetfile::Meta m2; std::string err;
            if (!userPresetValues(text, back, m2, err)) { std::fprintf(stderr, "%s: the file does not read back: %s\n", raw.name, err.c_str()); return 1; }
            for (int i = 0; i < kNumParams; ++i)
                if (i != PresetSelect && back[static_cast<size_t>(i)] != fin[static_cast<size_t>(i)]) { std::fprintf(stderr, "%s: %s reads back as %g, not %g\n", raw.name, specs()[static_cast<size_t>(i)].id, back[static_cast<size_t>(i)], fin[static_cast<size_t>(i)]); return 1; }
            if (m2.name != raw.name || m2.category != raw.cat) { std::fprintf(stderr, "%s: name or category lost\n", raw.name); return 1; }
        }
        writeWav((out / "wav" / (fileName(raw.name) + "_phrase.wav")).string(), a.L, a.R);
        writeWav((out / "wav" / (fileName(raw.name) + "_dubstep.wav")).string(), d.L, d.R);
        drops[raw.name] = d;
    }
    // the drop: Ultra Saw's chords over Ultra Bass's riff (each -6 dB: two presets at -16 LUFS add up)
    const Take& s = drops["Ultra Saw"];
    const Take& b = drops["Ultra Bass"];
    const size_t n = std::max(s.L.size(), b.L.size());
    std::vector<float> L(n, 0.0f), R(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const float k = 0.5f;
        if (i < s.L.size()) { L[i] += k * s.L[i]; R[i] += k * s.R[i]; }
        if (i < b.L.size()) { L[i] += k * b.L[i]; R[i] += k * b.R[i]; }
    }
    writeWav((out / "wav" / "ultra_drop.wav").string(), L, R);
    // the reference: the factory's heaviest preset on the same chords (the time per sample to compare)
    std::vector<double> ref; presetValues(0, ref);
    std::vector<Note> dn; std::vector<Wheel> dw; double secs = 0;
    dubstep("LEAD", dn, dw, secs);
    std::printf("reference: Anthem Supersaw on the same chords %0.f ns/sample\n", play(ref, dn, dw, secs, kBpm).nsPerSample);
    return 0;
}
