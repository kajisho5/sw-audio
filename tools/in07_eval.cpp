// SWINGBY (IN07) evaluation: the CPU of every factory preset on a held chord, a preset with many notes, the loudness at other sample rates,
// and steps (clicks) when the preset changes under held notes. The CPU numbers are this machine's (not a design estimate).
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_eval.cpp products/in07/*.cpp -o build/in07_eval
//   build/in07_eval cpu                  # every preset, C3 E3 G3 B3 held, 48 kHz: % of one core, finite, peak dBFS
//   build/in07_eval poly <preset> <n> [fs] # n notes held (Voices = n)
//   build/in07_eval rates                # audition-phrase loudness at 44.1 / 96 kHz against 48 kHz (every preset)
//   build/in07_eval rate192              # the same at 192 kHz for seven presets
//   build/in07_eval switch               # 60 random preset changes under a held chord: the largest 2nd difference in the 5 ms after the
//                                        # change against the 99.9th percentile of both presets' own sound (ratio > 4 is printed: a step)
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sw::in07;

static double benchChord(int preset, const std::vector<int>& keys, double fs, int voicesOverride, double seconds, bool& finite, double& peak) {
    Processor p;
    applyPreset(p, preset);
    if (voicesOverride > 0) p.setParam(Voices, voicesOverride);
    p.prepare(fs, 256);
    p.setTempo(120);
    std::vector<float> l(256), r(256);
    float* c[2] = {l.data(), r.data()};
    for (int k : keys) p.noteOn(k, 0.8);
    for (int i = 0; i < static_cast<int>(0.5 * fs / 256); ++i) p.process(c, 2, 256);   // past the attacks
    finite = true; peak = 0.0;
    double best = 1e30;
    for (int rep = 0; rep < 2; ++rep) {
        const int blocks = static_cast<int>(seconds * fs / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            p.process(c, 2, 256);
            for (int i = 0; i < 256; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
                peak = std::max({peak, static_cast<double>(std::fabs(l[i])), static_cast<double>(std::fabs(r[i]))});
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count() / seconds);   // seconds of CPU per second of audio
    }
    return best * 100.0;   // % of one core
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "cpu";
    const auto& pr = factoryPresets();
    if (mode == "cpu") {
        // every preset: a 4-note chord (C3 E3 G3 B3) held, 48 kHz
        const std::vector<int> chord = {48, 52, 55, 59};
        for (size_t i = 0; i < pr.size(); ++i) {
            bool fin; double pk;
            const double pct = benchChord(static_cast<int>(i), chord, 48000.0, 0, 1.5, fin, pk);
            std::printf("%3zu\t%-5s\t%-22s\t%.2f\t%s\t%.2f\n", i, pr[i].category.c_str(), pr[i].name.c_str(), pct, fin ? "ok" : "NONFINITE", 20 * std::log10(pk + 1e-12));
        }
    } else if (mode == "poly") {
        // one preset (argv[2]) with N held notes (argv[3]), voices = N, at fs (argv[4])
        const int idx = std::atoi(argv[2]), n = std::atoi(argv[3]);
        const double fs = argc > 4 ? std::atof(argv[4]) : 48000.0;
        std::vector<int> keys;
        for (int k = 0; k < n; ++k) keys.push_back(36 + 3 * k);
        bool fin; double pk;
        const double pct = benchChord(idx, keys, fs, n, 2.0, fin, pk);
        std::printf("%s  %d notes  %.0f Hz: %.1f %% of one core  %s  peak %.2f dBFS\n", pr[static_cast<size_t>(idx)].name.c_str(), n, fs, pct, fin ? "ok" : "NONFINITE", 20 * std::log10(pk + 1e-12));
    } else if (mode == "rates") {
        // loudness of every preset on its audition phrase at 44.1 / 96 / 192 kHz against 48 kHz
        for (size_t i = 0; i < pr.size(); ++i) {
            const auto m48 = measurePreset(static_cast<int>(i), 48000.0);
            const auto m44 = measurePreset(static_cast<int>(i), 44100.0);
            const auto m96 = measurePreset(static_cast<int>(i), 96000.0);
            std::printf("%3zu\t%-22s\t%.2f\t%+.2f\t%+.2f\t%.2f\t%.2f\n", i, pr[i].name.c_str(), m48.lufs, m44.lufs - m48.lufs, m96.lufs - m48.lufs, m48.peakDb, m96.peakDb);
        }
    } else if (mode == "switch") {
        // preset change while a chord is held: 2nd-difference peak in the 15 ms after the change against the 99.9th percentile of the 0.5 s before
        const double fs = 48000.0;
        const std::vector<int> chord = {48, 55, 60, 64};
        unsigned seed = 7;
        int flagged = 0, total = 0;
        for (int t = 0; t < 60; ++t) {
            seed = seed * 1103515245u + 12345u; const int a = static_cast<int>((seed >> 8) % pr.size());
            seed = seed * 1103515245u + 12345u; const int b = static_cast<int>((seed >> 8) % pr.size());
            Processor p; applyPreset(p, a); p.prepare(fs, 256); p.setTempo(120);
            for (int k : chord) p.noteOn(k, 0.8);
            std::vector<float> L, R, l(64), r(64); float* c[2] = {l.data(), r.data()};
            const int pre = static_cast<int>(1.0 * fs / 64), post = static_cast<int>(0.2 * fs / 64);
            for (int i = 0; i < pre + post; ++i) {
                if (i == pre) applyPreset(p, b);
                p.process(c, 2, 64); L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
            }
            const size_t sw = static_cast<size_t>(pre) * 64;
            auto d2 = [&](size_t i) -> double { return std::max(std::fabs(L[i] - 2 * L[i - 1] + L[i - 2]), std::fabs(R[i] - 2 * R[i - 1] + R[i - 2])); };
            std::vector<double> d;   // the new preset's own texture: 30..200 ms after the change, and the old one's: the 0.3 s before
            for (size_t i = sw + static_cast<size_t>(0.03 * fs); i < sw + static_cast<size_t>(0.2 * fs) - 1; ++i) d.push_back(d2(i));
            for (size_t i = sw - static_cast<size_t>(0.3 * fs); i < sw; ++i) d.push_back(d2(i));
            std::sort(d.begin(), d.end());
            const double ref = d[static_cast<size_t>(d.size() * 0.999)] + 1e-6;
            double after = 0.0;
            for (size_t i = sw; i < sw + static_cast<size_t>(0.005 * fs); ++i) after = std::max(after, d2(i));
            ++total;
            const double ratio = after / ref;
            if (ratio > 4.0) { ++flagged; std::printf("%-20s -> %-20s  ratio %.1f\n", pr[static_cast<size_t>(a)].name.c_str(), pr[static_cast<size_t>(b)].name.c_str(), ratio); }
        }
        std::printf("flagged %d / %d\n", flagged, total);
    } else if (mode == "rate192") {
        for (int i : {0, 3, 10, 30, 60, 90, 120}) {
            const auto m48 = measurePreset(i, 48000.0);
            const auto m192 = measurePreset(i, 192000.0);
            std::printf("%3d\t%-22s\t%.2f\t%+.2f\t%.2f\n", i, pr[static_cast<size_t>(i)].name.c_str(), m48.lufs, m192.lufs - m48.lufs, m192.peakDb);
        }
    }
    return 0;
}
