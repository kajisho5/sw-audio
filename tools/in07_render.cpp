// IN07 engine: render listening demos and measure the cost of one voice (and a chord) on this machine.
//   g++ -std=c++17 -O2 -Icore/include -Iproducts tools/in07_render.cpp products/in07/in07.cpp -o build/in07_render && build/in07_render build/in07
// Writes demo_lead.wav, demo_bass.wav, demo_pad.wav, demo_gravity.wav, demo_flyby.wav, demo_orbit_lfo.wav (the default effects on, peak-normalised to -1 dBFS), sweep_naive.wav / sweep_minblep.wav (saw 100 Hz -> 10 kHz, -12 dBFS)
// and prints the time per sample of one voice in four set-ups, the default effect chain and an 8-note chord. The numbers are this machine's, not a design estimate.
#include "in07/in07.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
constexpr double kFs = 48000.0;

struct Ev { double t; int note; bool on; double vel; };

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double peakDb) {
    double peak = 1e-9;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max({peak, static_cast<double>(std::abs(l[i])), static_cast<double>(std::abs(r[i]))});
    const double g = peakDb > 0 ? 1.0 : std::pow(10.0, peakDb / 20.0) / peak;   // peakDb > 0: keep the level as it is
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    const uint32_t n = static_cast<uint32_t>(l.size()), data = n * 4, fs = static_cast<uint32_t>(kFs), rate = fs * 4;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    const uint32_t fmtLen = 16, riff = 36 + data;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtLen, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&fs, 4, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    uint32_t seed = 1;
    auto tpdf = [&]() { seed = seed * 1664525u + 1013904223u; const double a = (seed >> 8) / 16777216.0; seed = seed * 1664525u + 1013904223u; return a - (seed >> 8) / 16777216.0; };
    for (uint32_t i = 0; i < n; ++i) {
        for (const float* c : {l.data(), r.data()}) {
            const double v = std::clamp(c[i] * g * 32767.0 + tpdf(), -32768.0, 32767.0);
            const int16_t s = static_cast<int16_t>(std::lround(v));
            std::fwrite(&s, 2, 1, f);
        }
    }
    std::fclose(f);
}

// sample-accurate: the block is split at every event
void play(Processor& p, std::vector<Ev> ev, double seconds, std::vector<float>& l, std::vector<float>& r) {
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const size_t n = static_cast<size_t>(seconds * kFs);
    l.assign(n, 0.0f); r.assign(n, 0.0f);
    size_t pos = 0, e = 0;
    while (pos < n) {
        while (e < ev.size() && static_cast<size_t>(ev[e].t * kFs) <= pos) {
            if (ev[e].on) p.noteOn(ev[e].note, ev[e].vel); else p.noteOff(ev[e].note);
            ++e;
        }
        size_t end = std::min(n, pos + 256);
        if (e < ev.size()) end = std::min(end, std::max(pos + 1, static_cast<size_t>(ev[e].t * kFs)));
        float* c[2] = {l.data() + pos, r.data() + pos};
        p.process(c, 2, static_cast<int>(end - pos));
        pos = end;
    }
}

void seq(std::vector<Ev>& ev, const std::vector<int>& notes, double start, double step, double gate, double vel) {
    for (size_t i = 0; i < notes.size(); ++i) {
        const double t = start + static_cast<double>(i) * step;
        ev.push_back({t, notes[i], true, vel});
        ev.push_back({t + step * gate, notes[i], false, 0.0});
    }
}

void leadPatch(Processor& p) {
    p.setParam(lp(0, Wave), Saw); p.setParam(lp(0, Unison), 8); p.setParam(lp(0, Detune), 25); p.setParam(lp(0, Spread), 85);
    p.setParam(lp(0, FilterType), LP24); p.setParam(lp(0, Cutoff), 2400); p.setParam(lp(0, Resonance), 30); p.setParam(lp(0, Drive), 18);
    p.setParam(lp(0, FilterEnv), 40); p.setParam(lp(0, KeyTrack), 50);
    p.setParam(lp(0, AmpA), 5); p.setParam(lp(0, AmpD), 320); p.setParam(lp(0, AmpS), 70); p.setParam(lp(0, AmpR), 420);
    p.setParam(lp(0, FenvA), 2); p.setParam(lp(0, FenvD), 600); p.setParam(lp(0, FenvS), 20); p.setParam(lp(0, FenvR), 500);
    p.setParam(lp(0, VelSens), 50); p.setParam(Glide, 0); p.setParam(Level, -6);
}

double benchNsPerSample(Processor& p, bool hold, int chord = 1) {
    const int seconds = 20, block = 256;
    std::vector<float> l(block), r(block);
    float* c[2] = {l.data(), r.data()};
    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep) {
        p.allNotesOff();
        for (int i = 0; i < 200; ++i) p.process(c, 2, block);   // let a previous note sleep
        if (hold) for (int k = 0; k < chord; ++k) p.noteOn(57 + 3 * k, 0.9);
        const auto t0 = std::chrono::steady_clock::now();
        const int blocks = static_cast<int>(seconds * kFs / block);
        volatile float sink = 0.0f;
        for (int b = 0; b < blocks; ++b) { p.process(c, 2, block); sink = sink + l[0]; }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / (blocks * static_cast<double>(block)));
    }
    return best;
}
}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<float> l, r;

    {   // lead: a supersaw line, 138 bpm, 8th notes, A minor (Am F C G) twice
        Processor p; p.prepare(kFs, 256); leadPatch(p);
        const double step = 60.0 / 138.0 / 2.0;
        const std::vector<int> bars = {69, 72, 76, 81, 79, 76, 74, 76,  65, 69, 72, 77, 76, 72, 69, 72,
                                       64, 67, 72, 76, 74, 72, 67, 72,  74, 71, 67, 71, 74, 79, 74, 71};
        std::vector<Ev> ev;
        seq(ev, bars, 0.25, step, 0.8, 0.85);
        seq(ev, bars, 0.25 + 32 * step, step, 0.8, 0.95);
        ev.push_back({0.25 + 64 * step, 69, true, 0.9}); ev.push_back({0.25 + 64 * step + 1.2, 69, false, 0.0});
        play(p, ev, 0.25 + 64 * step + 2.2, l, r);
        writeWav(dir + "/demo_lead.wav", l, r, -1.0);
    }
    {   // bass: a 16th-note pluck, octave jumps, same chords
        Processor p; p.prepare(kFs, 256);
        p.setParam(lp(0, Wave), Square); p.setParam(lp(0, PulseWidth), 35); p.setParam(lp(0, Unison), 1);
        p.setParam(lp(0, FilterType), LP24); p.setParam(lp(0, Cutoff), 180); p.setParam(lp(0, Resonance), 45); p.setParam(lp(0, Drive), 30);
        p.setParam(lp(0, FilterEnv), 75); p.setParam(lp(0, KeyTrack), 30);
        p.setParam(lp(0, AmpA), 1); p.setParam(lp(0, AmpD), 300); p.setParam(lp(0, AmpS), 40); p.setParam(lp(0, AmpR), 80);
        p.setParam(lp(0, FenvA), 1); p.setParam(lp(0, FenvD), 160); p.setParam(lp(0, FenvS), 0); p.setParam(lp(0, FenvR), 100);
        p.setParam(lp(0, VelSens), 60); p.setParam(Level, -6);
        const double step = 60.0 / 138.0 / 4.0;
        std::vector<int> notes;
        for (int root : {33, 29, 36, 31, 33, 29, 36, 31})
            for (int i = 0; i < 16; ++i) notes.push_back(root + ((i % 4 == 2) ? 12 : 0));
        std::vector<Ev> ev;
        for (size_t i = 0; i < notes.size(); ++i) {
            const double t = 0.25 + static_cast<double>(i) * step, vel = (i % 4 == 0) ? 1.0 : 0.7;
            ev.push_back({t, notes[i], true, vel}); ev.push_back({t + step * 0.55, notes[i], false, 0.0});
        }
        play(p, ev, 0.25 + notes.size() * step + 0.6, l, r);
        writeWav(dir + "/demo_bass.wav", l, r, -1.0);
    }
    {   // pad: two layers (L1 detuned saws, L2 a square an octave down panned against it), four-note chords, 8 bars
        Processor p; p.prepare(kFs, 256); leadPatch(p);
        p.setParam(lp(0, Cutoff), 1400); p.setParam(lp(0, AmpA), 400); p.setParam(lp(0, AmpR), 1800); p.setParam(lp(0, FilterEnv), 25);
        p.setParam(lp(0, Pan), -25);
        p.setParam(lp(1, On), 1); p.setParam(lp(1, Wave), Square); p.setParam(lp(1, PulseWidth), 30); p.setParam(lp(1, Octave), -1);
        p.setParam(lp(1, Unison), 2); p.setParam(lp(1, Detune), 10); p.setParam(lp(1, Cutoff), 700); p.setParam(lp(1, Resonance), 20);
        p.setParam(lp(1, AmpA), 600); p.setParam(lp(1, AmpR), 2000); p.setParam(lp(1, LayerLevel), -6); p.setParam(lp(1, Pan), 25);
        const double bar = 60.0 / 90.0 * 4.0;
        const std::vector<std::vector<int>> chords = {{57, 60, 64, 67}, {53, 57, 60, 64}, {48, 55, 60, 64}, {55, 59, 62, 67}};
        std::vector<Ev> ev;
        for (int b = 0; b < 8; ++b)
            for (int k : chords[static_cast<size_t>(b % 4)]) { const double t = 0.25 + b * bar; ev.push_back({t, k, true, 0.8}); ev.push_back({t + bar * 0.95, k, false, 0.0}); }
        play(p, ev, 0.25 + 8 * bar + 2.5, l, r);
        writeWav(dir + "/demo_pad.wav", l, r, -1.0);
    }
    // the planet features
    auto route = [](Processor& p, int slot, int src, int dst, double amount) {
        p.setParam(modId(slot, ModSrc), src); p.setParam(modId(slot, ModDst), dst); p.setParam(modId(slot, ModAmount), amount); p.setParam(modId(slot, ModOn), 1);
    };
    {   // gravity: a supersaw chord, the LFO (slow triangle) pulls the copies into step and lets them go (wide <-> one saw)
        Processor p; p.prepare(kFs, 256); leadPatch(p);
        p.setParam(lp(0, Detune), 35); p.setParam(lp(0, Cutoff), 3500); p.setParam(lp(0, AmpA), 30); p.setParam(lp(0, AmpR), 1500);
        p.setParam(Lfo2Shape, LfoTriangle); p.setParam(Lfo2Rate, 0.2);
        route(p, 0, SrcLfo2, DstGravity, 50.0);   // gravity 0 .. 100 % (from 50 % +- 50)
        p.setParam(lp(0, Gravity), 50);
        std::vector<Ev> ev;
        for (int k : {57, 60, 64, 69}) { ev.push_back({0.25, k, true, 0.8}); ev.push_back({10.25, k, false, 0.0}); }
        play(p, ev, 12.5, l, r);
        writeWav(dir + "/demo_gravity.wav", l, r, -1.0);
    }
    {   // flyby: Arrive on a lead (each note comes in from the side, alternating), then Pass on long notes
        Processor p; p.prepare(kFs, 256); leadPatch(p);
        p.setParam(FlybyMode, FlybyArrive); p.setParam(FlybyDepth, 70); p.setParam(FlybyTime, 0.6); p.setParam(FlybyNear, 55); p.setParam(FlybySide, 2);
        std::vector<Ev> ev;
        const double beat = 60.0 / 110.0;
        const std::vector<int> mel = {69, 72, 76, 74, 72, 69, 67, 69};
        for (size_t i = 0; i < mel.size(); ++i) { const double t = 0.25 + i * beat * 2; ev.push_back({t, mel[i], true, 0.9}); ev.push_back({t + beat * 1.8, mel[i], false, 0.0}); }
        play(p, ev, 0.25 + mel.size() * beat * 2 + 1.5, l, r);
        std::vector<float> l2, r2;
        Processor q; q.prepare(kFs, 256); leadPatch(q);
        q.setParam(FlybyMode, FlybyPass); q.setParam(FlybyDepth, 100); q.setParam(FlybyTime, 2.5); q.setParam(FlybyNear, 40); q.setParam(FlybySide, 2);
        q.setParam(lp(0, AmpR), 600);
        std::vector<Ev> ev2 = {{0.25, 57, true, 0.9}, {2.9, 57, false, 0.0}, {3.25, 64, true, 0.9}, {5.9, 64, false, 0.0}};
        play(q, ev2, 7.5, l2, r2);
        l.insert(l.end(), l2.begin(), l2.end()); r.insert(r.end(), r2.begin(), r2.end());
        writeWav(dir + "/demo_flyby.wav", l, r, -1.0);
    }
    {   // orbit LFO: eccentricity 85 % synced to 1/4 on the cutoff of a pad: a short bright peak each beat, then the long dark swing out
        Processor p; p.prepare(kFs, 256); leadPatch(p); p.setTempo(120.0);
        p.setParam(lp(0, Cutoff), 500); p.setParam(lp(0, FilterEnv), 0); p.setParam(lp(0, Resonance), 45); p.setParam(lp(0, AmpR), 1200);
        p.setParam(Lfo1Shape, LfoOrbit); p.setParam(Lfo1Ecc, 85); p.setParam(Lfo1Sync, 5); p.setParam(Lfo1Trigger, 1);
        route(p, 0, SrcLfo1, DstCutoff, 45.0);    // +-2.25 octaves
        const double bar = 2.0;
        const std::vector<std::vector<int>> chords = {{57, 60, 64}, {53, 57, 60}, {48, 55, 60}, {55, 59, 62}};
        std::vector<Ev> ev;
        for (int b = 0; b < 4; ++b) for (int k : chords[static_cast<size_t>(b)]) { const double t = 0.25 + b * bar; ev.push_back({t, k, true, 0.8}); ev.push_back({t + bar * 0.98, k, false, 0.0}); }
        play(p, ev, 0.25 + 4 * bar + 2.0, l, r);
        writeWav(dir + "/demo_orbit_lfo.wav", l, r, -1.0);
    }
    for (int correct = 0; correct < 2; ++correct) {   // saw sweep 100 Hz -> 10 kHz over 8 s, exponential
        BlepOsc o; o.setWave(Saw); o.setCorrection(correct == 1);
        const size_t n = static_cast<size_t>(8.0 * kFs);
        l.assign(n, 0.0f);
        for (size_t i = 0; i < n; ++i) { o.setIncrement(100.0 * std::pow(100.0, i / static_cast<double>(n)) / kFs); l[i] = static_cast<float>(0.25 * o.next()); }
        writeWav(dir + (correct ? "/sweep_minblep.wav" : "/sweep_naive.wav"), l, l, 1.0);
    }

    struct Setup { const char* name; int unison; int type; double drive; double spread; bool hold; bool fx; };
    const Setup setups[] = {
        {"saw x1, LP12, Drive 0            ", 1, LP12, 0, 0, true, false},
        {"saw x1, LP24, Drive 18 (2x)      ", 1, LP24, 18, 0, true, false},
        {"saw x8 stereo, LP24, Drive 18 (2x)", 8, LP24, 18, 80, true, false},
        {"no note (the voice sleeps)       ", 8, LP24, 18, 80, false, false},
        {"saw x1, LP12 + the default effects", 1, LP12, 0, 0, true, true},
    };
    std::printf("one voice (effects off unless named), 48 kHz, blocks of 256 (best of 3 x 20 s):\n");
    for (const auto& s : setups) {
        Processor p; p.prepare(kFs, 256); leadPatch(p);
        if (!s.fx) for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);
        p.setParam(lp(0, Unison), s.unison); p.setParam(lp(0, FilterType), s.type); p.setParam(lp(0, Drive), s.drive); p.setParam(lp(0, Spread), s.spread);
        p.setParam(lp(0, AmpS), 100);
        const double ns = benchNsPerSample(p, s.hold);
        std::printf("  %s  %7.1f ns/sample  = %6.3f %% of one core in real time\n", s.name, ns, ns * kFs / 1e9 * 100.0);
    }
    {   // a chord of 8 notes on L1 (saw x8 stereo, LP24, Drive 18) and the same with all four layers on
        Processor p; p.prepare(kFs, 256); leadPatch(p); p.setParam(lp(0, AmpS), 100);
        for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0);
        std::printf("  8 notes, L1 only (saw x8 stereo, LP24, Drive 18)  %7.1f ns/sample\n", benchNsPerSample(p, true, 8));
        for (int l = 1; l < kLayers; ++l) {
            p.setParam(lp(l, On), 1);
            for (int id = LayerLevel; id < kLayerParams; ++id) p.setParam(lp(l, id), p.param(lp(0, id)));
        }
        const double ns = benchNsPerSample(p, true, 8);
        std::printf("  8 notes, 4 layers alike                          %7.1f ns/sample  = %6.3f %% of one core in real time\n", ns, ns * kFs / 1e9 * 100.0);
    }
    return 0;
}
