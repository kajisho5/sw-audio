// SWINGBY (SW IN07): play a factory preset on a chord phrase (128 BPM, Am F C G: 4 bars held, then 4 bars of eighths), write it as a WAV
// (16-bit) and measure the held bars: loudness (BS.1770), crest factor, spectral centroid, the share of the energy above 5 kHz and below
// 250 Hz, side over mid, CPU. Extra arguments switch effects off by number (0 Drive, 1 Chorus, 2 Delay, 3 Reverb, 4 EQ, 5 Limit).
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_chords.cpp products/in07/in07.cpp products/in07/osc.cpp products/in07/presets.cpp -o build/in07_chords
//   build/in07_chords <preset index> <out.wav> [fx off ...]
#include "in07/presets.hpp"
#include "sw/loudness.hpp"
#include "sw/fft.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
using namespace sw::in07;
void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r, double fs) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.put(char(v & 255)); f.put(char((v >> 8) & 255)); f.put(char((v >> 16) & 255)); f.put(char((v >> 24) & 255)); };
    auto u16 = [&](uint16_t v) { f.put(char(v & 255)); f.put(char((v >> 8) & 255)); };
    const uint32_t n = (uint32_t)l.size(), bytes = n * 4;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32((uint32_t)fs); u32((uint32_t)fs * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i) for (float x : {l[i], r[i]}) { int v = (int)std::lround(std::max(-1.f, std::min(1.f, x)) * 32767.f); u16((uint16_t)(int16_t)v); }
}
int main(int argc, char** argv) {
    const int idx = std::atoi(argv[1]); const double fs = 48000, beat = 60.0 / 128.0, bar = 4 * beat;
    const int chords[4][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}, {55, 60, 64, 67}, {55, 59, 62, 67}};   // Am F C G
    struct Ev { double t; int key; bool on; };
    std::vector<Ev> ev;
    for (int b = 0; b < 4; ++b) for (int k : chords[b]) { ev.push_back({0.05 + b * bar, k, true}); ev.push_back({0.05 + b * bar + bar * 0.97, k, false}); }
    for (int b = 0; b < 4; ++b) for (int e = 0; e < 8; ++e) for (int k : chords[b]) {
        const double t = 0.05 + (4 + b) * bar + e * beat / 2; ev.push_back({t, k, true}); ev.push_back({t + beat / 2 * 0.7, k, false}); }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && !a.on && b.on); });
    const double total = 0.05 + 8 * bar + 2.0;
    Processor p; applyPreset(p, idx); for (int a = 3; a < argc; ++a) p.setParam(fxOnId(std::atoi(argv[a])), 0); p.prepare(fs, 256); p.setTempo(128);
    const size_t N = (size_t)(total * fs); std::vector<float> L(N), R(N);
    size_t e = 0; auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < N;) {
        while (e < ev.size() && (size_t)std::llround(ev[e].t * fs) <= pos) { if (ev[e].on) p.noteOn(ev[e].key, 0.85); else p.noteOff(ev[e].key); ++e; }
        size_t n = std::min<size_t>(256, N - pos); if (e < ev.size()) n = std::min(n, (size_t)std::llround(ev[e].t * fs) - pos);
        float* c[2] = {L.data() + pos, R.data() + pos}; p.process(c, 2, (int)n); pos += n;
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N;
    writeWav(argv[2], L, R, fs);
    // metrics over the held chords (bars 1-4)
    const size_t a = (size_t)(0.5 * fs), b = (size_t)((0.05 + 4 * bar) * fs);
    sw::IntegratedLoudness il; il.setup(fs, 2, 0); { const float* c[2] = {L.data(), R.data()}; il.process(c, 2, (int)N); }
    double pk = 0, ms = 0, mid = 0, side = 0;
    for (size_t i = a; i < b; ++i) { pk = std::max({pk, (double)std::abs(L[i]), (double)std::abs(R[i])}); ms += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]); mid += m * m; side += s * s; }
    ms /= (b - a);
    // spectrum of the held part: centroid and the share above 5 kHz, below 250 Hz
    const int n = 32768; double num = 0, den = 0, hi = 0, lo = 0; int frames = 0;
    for (size_t off = a; off + n < b; off += n / 2, ++frames) {
        std::vector<std::complex<double>> x(n);
        for (int i = 0; i < n; ++i) x[i] = 0.5 * (L[off + i] + R[off + i]) * (0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / n));
        sw::Fft f(n); f.forward(x);
        for (int k = 1; k < n / 2; ++k) { const double fr = k * fs / n, pw = std::norm(x[k]); num += fr * pw; den += pw; if (fr > 5000) hi += pw; if (fr < 250) lo += pw; }
    }
    std::printf("%s: %.1f LUFS, crest %.1f dB, centroid %.0f Hz, above 5 kHz %.1f dB, below 250 Hz %.1f dB, side/mid %.1f dB, %.0f ns/sample\n",
                factoryPresets()[idx].name.c_str(), il.integrated(), 20 * std::log10(pk / std::sqrt(ms)), num / den, 10 * std::log10(hi / den), 10 * std::log10(lo / den), 10 * std::log10(side / mid), ns);
}
