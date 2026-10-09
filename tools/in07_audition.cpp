// SWINGBY: every factory preset's audition phrase (the one its level was measured on, products/in07/presets.cpp audition()) as a WAV file,
// exactly as the plug-in plays it (48 kHz, 120 bpm, velocity 0.8, the preset's own effects and level; no normalising: they are all at
// -16 LUFS already), with 2 s after the phrase for the tails. For the listening page (tools/in07_audition_page.py) and ear checks.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_audition.cpp products/in07/*.cpp -o build/in07_audition
//   build/in07_audition <out dir>     -> <out dir>/<NNN>.wav (NNN = preset number from 001) and index.json (name, category, file)
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
void put16(std::FILE* f, uint16_t v) { std::fputc(v & 255, f); std::fputc(v >> 8, f); }
void put32(std::FILE* f, uint32_t v) { put16(f, static_cast<uint16_t>(v & 0xFFFF)); put16(f, static_cast<uint16_t>(v >> 16)); }
bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(l.size()), bytes = n * 4;
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, static_cast<uint32_t>(fs)); put32(f, static_cast<uint32_t>(fs) * 4); put16(f, 4); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float x : {l[i], r[i]}) { const long v = std::lround(std::clamp(static_cast<double>(x), -1.0, 1.0) * 32767.0); put16(f, static_cast<uint16_t>(static_cast<int16_t>(v))); }
    return std::fclose(f) == 0;
}
std::string json(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: in07_audition <out dir>\n"); return 2; }
    const std::string dir = argv[1];
    constexpr double fs = 48000.0;
    const auto& P = factoryPresets();
    std::string index = "[";
    for (size_t i = 0; i < P.size(); ++i) {
        Processor p;
        applyPreset(p, static_cast<int>(i));
        p.prepare(fs, 256);
        p.setTempo(120.0);
        double total = 0.0;
        const auto notes = audition(P[i].category, total);
        struct Ev { long long at; int key; bool on; };
        std::vector<Ev> ev;
        for (const auto& n : notes) { ev.push_back({std::llround(n.start * fs), n.key, true}); ev.push_back({std::llround((n.start + n.length) * fs), n.key, false}); }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at || (a.at == b.at && !a.on && b.on); });
        const long long end = std::llround((total + 2.0) * fs);
        std::vector<float> L(static_cast<size_t>(end)), R(static_cast<size_t>(end));
        size_t e = 0;
        for (long long pos = 0; pos < end;) {
            while (e < ev.size() && ev[e].at <= pos) { if (ev[e].on) p.noteOn(ev[e].key, 0.8); else p.noteOff(ev[e].key); ++e; }
            long long n = std::min<long long>(256, end - pos);
            if (e < ev.size()) n = std::min(n, ev[e].at - pos);
            float* c[2] = {L.data() + pos, R.data() + pos};
            p.process(c, 2, static_cast<int>(n));
            pos += n;
        }
        char name[16];
        std::snprintf(name, sizeof name, "%03zu.wav", i + 1);
        if (!writeWav(dir + "/" + name, L, R, static_cast<int>(fs))) { std::fprintf(stderr, "cannot write %s/%s\n", dir.c_str(), name); return 1; }
        index += std::string(i ? "," : "") + "{\"name\":" + json(P[i].name) + ",\"category\":" + json(P[i].category) + ",\"file\":\"" + name + "\",\"seconds\":" + std::to_string(total + 2.0) + "}";
        std::fprintf(stderr, "\r%zu / %zu", i + 1, P.size());
    }
    index += "]";
    std::FILE* f = std::fopen((dir + "/index.json").c_str(), "wb");
    if (!f) return 1;
    std::fwrite(index.data(), 1, index.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "\n");
    return 0;
}
