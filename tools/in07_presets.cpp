// SWINGBY (SW IN07): level the factory presets in two stages (presets.hpp), on each preset's category audition phrase (BS.1770):
//   staging: the trim on every layer that is on, until the voices' sum (every effect off, Level 0) is at kPresetStageLufs (within 0.02 LU);
//   output:  then, with the effects, to kPresetTargetLufs: a negative difference goes to the master Level (linear: exact), a positive one
//            into the limiter's gain, measured again until within 0.05 LU (the limiter is not linear).
// Writes products/in07/preset_levels.inc. With a second argument, also each preset's audition phrase as a WAV (24-bit, 48 kHz) there.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_presets.cpp products/in07/in07.cpp products/in07/osc.cpp products/in07/presets.cpp -o build/in07_presets
//   build/in07_presets products/in07/preset_levels.inc [wav dir]
#include "in07/presets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double fs) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.put(char(v & 255)); f.put(char((v >> 8) & 255)); f.put(char((v >> 16) & 255)); f.put(char((v >> 24) & 255)); };
    auto u16 = [&](uint16_t v) { f.put(char(v & 255)); f.put(char((v >> 8) & 255)); };
    const uint32_t n = static_cast<uint32_t>(l.size()), bytes = n * 2 * 3;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(fs)); u32(static_cast<uint32_t>(fs) * 6); u16(6); u16(24);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float x : {l[i], r[i]}) {
            const int32_t v = static_cast<int32_t>(std::lround(std::max(-1.0f, std::min(1.0f, x)) * 8388607.0f));
            f.put(char(v & 255)); f.put(char((v >> 8) & 255)); f.put(char((v >> 16) & 255));
        }
}
// the audition phrase into l / r (as measurePreset plays it)
void play(int index, const PresetLevels& lv, double fs, std::vector<float>& L, std::vector<float>& R, double& nsPerSample) {
    Processor p;
    applyPreset(p, index, lv);
    p.prepare(fs, 256); p.setTempo(120.0);
    double total = 0.0;
    const auto notes = audition(factoryPresets()[static_cast<size_t>(index)].category, total);
    struct Ev { long long at; int key; bool on; };
    std::vector<Ev> ev;
    for (const auto& n : notes) { ev.push_back({std::llround(n.start * fs), n.key, true}); ev.push_back({std::llround((n.start + n.length) * fs), n.key, false}); }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at || (a.at == b.at && !a.on && b.on); });
    const long long end = std::llround(total * fs);
    L.assign(static_cast<size_t>(end), 0.0f); R.assign(static_cast<size_t>(end), 0.0f);
    size_t e = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long pos = 0; pos < end;) {
        while (e < ev.size() && ev[e].at <= pos) { if (ev[e].on) p.noteOn(ev[e].key, 0.8); else p.noteOff(ev[e].key); ++e; }
        long long n = std::min<long long>(256, end - pos);
        if (e < ev.size()) n = std::min(n, ev[e].at - pos);
        float* c[2] = {L.data() + pos, R.data() + pos};
        p.process(c, 2, static_cast<int>(n));
        pos += n;
    }
    nsPerSample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / static_cast<double>(end);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: in07_presets <preset_levels.inc> [wav dir]\n"); return 2; }
    const double fs = 48000.0;
    const auto& P = factoryPresets();
    if (!presetErrors().empty()) { for (const auto& e : presetErrors()) std::fprintf(stderr, "%s\n", e.c_str()); return 1; }
    std::string out = "// written by tools/in07_presets.cpp (do not edit by hand): name, staging trim (dB on every layer that is on), master Level (dB), limiter gain added (dB)\n";
    std::printf("%-6s %-18s %7s %7s %6s %8s %7s %8s %9s %9s\n", "cat", "preset", "trim", "level", "boost", "LUFS", "mono", "peak", "into lim", "ns/smp");
    int bad = 0;
    auto r2 = [](double x) { return std::round(x * 100.0) / 100.0; };
    for (int i = 0; i < static_cast<int>(P.size()); ++i) {
        PresetLevels lv;
        for (int it = 0; it < 8; ++it) {   // staging (linear in the trim, but for layers clamped at their ends)
            const double err = kPresetStageLufs - measurePreset(i, fs, &lv, true).lufs;
            if (std::abs(err) < 0.02) break;
            lv.trim += err;
        }
        lv.trim = r2(lv.trim);
        double c = kPresetTargetLufs - measurePreset(i, fs, &lv).lufs;
        if (c <= 0.0) lv.level = r2(c);
        else {
            const double room = 12.0 - factoryPresets()[static_cast<size_t>(i)].values.size() * 0.0;   // the limiter's gain range (its own setting is added in applyPreset)
            for (int it = 0; it < 8 && std::abs(c) >= 0.05; ++it) { lv.boost = std::clamp(lv.boost + c, 0.0, room); c = kPresetTargetLufs - measurePreset(i, fs, &lv).lufs; }
            lv.boost = r2(lv.boost);
        }
        const PresetMeasure m = measurePreset(i, fs, &lv);
        std::vector<float> L, R;
        double ns = 1e30;
        for (int rep = 0; rep < 3; ++rep) { double t = 0.0; play(i, lv, fs, L, R, t); ns = std::min(ns, t); }   // the best of three (CPU)
        const auto& pr = P[static_cast<size_t>(i)];
        std::printf("%-6s %-18s %+7.2f %+7.2f %6.2f %8.2f %7.2f %8.2f %9.2f %9.0f\n", pr.category.c_str(), pr.name.c_str(), lv.trim, lv.level, lv.boost, m.lufs, m.monoLufs, m.peakDb, m.rawPeakDb, ns);
        if (std::abs(m.lufs - kPresetTargetLufs) > 0.5) ++bad;
        char line[200];
        std::snprintf(line, sizeof line, "{\"%s\", %.2f, %.2f, %.2f},\n", pr.name.c_str(), lv.trim, lv.level, lv.boost);
        out += line;
        if (argc > 2) {
            std::string f = pr.name;
            for (char& ch : f) if (ch == ' ') ch = '_';
            char num[16];
            std::snprintf(num, sizeof num, "%02d_", i + 1);
            writeWav(std::string(argv[2]) + "/" + num + f + ".wav", L, R, fs);
        }
    }
    std::ofstream(argv[1]) << out;
    std::printf("wrote %s (%d presets off by more than 0.5 LU)\n", argv[1], bad);
    return bad == 0 ? 0 : 1;
}
