// SWINGBY (SW IN07): level the factory presets. For each preset, play its category's audition phrase, measure BS.1770 integrated loudness,
// move the trim (every layer level together) by the difference to kPresetTargetLufs and measure again until within 0.05 LU (the limiter
// and the drives are not linear); then write the trims for products/in07/preset_trims.inc. With a second argument, also write each preset's
// audition phrase as a WAV (24-bit, 48 kHz) there.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_presets.cpp products/in07/in07.cpp products/in07/osc.cpp products/in07/presets.cpp -o build/in07_presets
//   build/in07_presets products/in07/preset_trims.inc [wav dir]
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
void play(int index, double trim, double fs, std::vector<float>& L, std::vector<float>& R, double& nsPerSample) {
    Processor p;
    applyPreset(p, index, trim);
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
    if (argc < 2) { std::fprintf(stderr, "usage: in07_presets <preset_trims.inc> [wav dir]\n"); return 2; }
    const double fs = 48000.0;
    const auto& P = factoryPresets();
    if (!presetErrors().empty()) { for (const auto& e : presetErrors()) std::fprintf(stderr, "%s\n", e.c_str()); return 1; }
    std::string out = "// written by tools/in07_presets.cpp (do not edit by hand): the trim (dB, added to every layer level that is on) for each factory preset,\n"
                      "// so that its audition phrase plays at kPresetTargetLufs with Level at kPresetLevel\n";
    std::printf("%-6s %-18s %7s %8s %7s %8s %9s %9s\n", "cat", "preset", "trim", "LUFS", "mono", "peak", "into lim", "ns/smp");
    int bad = 0;
    for (int i = 0; i < static_cast<int>(P.size()); ++i) {
        double trim = 0.0;
        PresetMeasure m;
        for (int it = 0; it < 8; ++it) {
            m = measurePreset(i, fs, trim);
            const double err = kPresetTargetLufs - m.lufs;
            if (std::abs(err) < 0.05) break;
            trim += err;
        }
        trim = std::round(trim * 100.0) / 100.0;
        m = measurePreset(i, fs, trim);
        std::vector<float> L, R;
        double ns = 1e30;
        for (int rep = 0; rep < 3; ++rep) { double t = 0.0; play(i, trim, fs, L, R, t); ns = std::min(ns, t); }   // the best of three (CPU)
        const auto& pr = P[static_cast<size_t>(i)];
        std::printf("%-6s %-18s %+7.2f %8.2f %7.2f %8.2f %9.2f %9.0f\n", pr.category.c_str(), pr.name.c_str(), trim, m.lufs, m.monoLufs, m.peakDb, m.rawPeakDb, ns);
        if (std::abs(m.lufs - kPresetTargetLufs) > 0.5) ++bad;
        char line[160];
        std::snprintf(line, sizeof line, "{\"%s\", %.2f},\n", pr.name.c_str(), trim);
        out += line;
        if (argc > 2) {
            std::string f = pr.name;
            for (char& c : f) if (c == ' ') c = '_';
            char num[16];
            std::snprintf(num, sizeof num, "%02d_", i + 1);
            writeWav(std::string(argv[2]) + "/" + num + f + ".wav", L, R, fs);
        }
    }
    std::ofstream(argv[1]) << out;
    std::printf("wrote %s (%d presets off by more than 0.5 LU)\n", argv[1], bad);
    return bad == 0 ? 0 : 1;
}
