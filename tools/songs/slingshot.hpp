// "Slingshot": a dubstep demo made only with SWINGBY (2026-10-10, the client: 実際にダブステップつくってみてよ, then: the aggressive
// early-2010s style, "brostep"). An original piece: the style's ways only (a different bass sound every half beat, metallic FM screeches, a
// talking vowel bass, pitch dives, stutters, laser zaps, hard stops, a big snare), no melody or riff of any record. Every sound is a SWINGBY
// instance: the drums (the kit's), the factory presets (pad, pluck with the arpeggiator, impact, downlifter), the ULTRA pack
// (docs/dist/in07/ultra) and the basses written here.
// Form (bars of 4 beats, 140 bpm, F minor, Fm7 - Db - Ab - Eb): intro 8, build 8, drop 16 (the wobble answering), break 8, drop 16 (the riddim
// answering, the saw hook), outro 4. The drops' roots: F F Db Eb, a bar each. The drops at -9 LUFS (past it the limiter only crushes: about
// 0.05 LU more per dB of gain).
#pragma once
#include "in07_songkit.hpp"

namespace sw::in07::songs {
using namespace sw::in07::songkit;

namespace slingshot_detail {
using namespace sw::in07::dsl;
// a metallic screech: FM (feedback) and a hard-sync table an octave up, band-passed and driven; a random FM brightness every 1/16, the
// filter and the tables (the sync sweep: a held sync table rings on one harmonic) on a 1/8; the trance gate (1/32, every other step) is
// switched on by the arrangement for stutters
inline const Raw& screechRecipe() {
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
inline const Raw& yoiRecipe() {
    static const Raw r = R("Yoi", "BASS", N().legato(60).drv(70, 90).eq(0, 3, 4).g({{"bend", 12}})
        .lfoSync(1, "Saw", "1/4", true).mod(1, "LFO 1", "WT position", 70).mod(2, "LFO 1", "Cutoff", 30)
        .L(1, wt("Formant", 20, 0, 2, 12, 40) + flt("LP 24", 3000, 35, 0, 80, 0) + amp(1, 300, 100, 60, 10))
        .L(2, wt("Formant", 50, 1, 2, 15, 80) + flt("BP 12", 1800, 40, 0, 50, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))
        .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 60, 10) + lvl(-6))
        .L(4, fm("1", 50, 10000, 50, 0, 1) + flt("LP 12", 2500, 20, 0, 80, 0) + amp(1, 300, 100, 60, 10) + lvl(-4)));
    return r;
}
// a laser zap: saws and FM falling three octaves in 120 ms (three Env 2 -> Pitch slots), a 1/16 echo
inline const Raw& laserRecipe() {
    static const Raw r = R("Laser", "FX", N().drv(40, 80).dly("1/16", 40, 25).rev(1.0, 15)
        .mod(1, "Env 2", "Pitch", 100).mod(2, "Env 2", "Pitch", 100).mod(3, "Env 2", "Pitch", 100)
        .L(1, saw(0, 4, 30, 100) + flt("LP 12", 12000, 20, 0, 30, 0) + fenv(0.5, 120, 0, 50) + amp(0.5, 200, 0, 60, 30))
        .L(2, fm("2", 60, 300, 40, 0, 1) + flt("LP 12", 9000, 0, 0, 0, 0) + fenv(0.5, 120, 0, 50) + amp(0.5, 200, 0, 60, 30) + lvl(-4)));
    return r;
}
// a metal hit: two inharmonic-sounding FM pairs (high ratios, the index decaying fast) and a knock, driven, a short room
inline const Raw& metalRecipe() {
    static const Raw r = R("Metal", "FX", N().drv(60, 80).rev(1.2, 20)
        .L(1, fm("11", 70, 250, 30, 0, 1) + flt("HP 12", 400, 0, 0, 0, 0) + amp(0.5, 300, 0, 100, 20))
        .L(2, fm("7", 60, 400, 50, -1, 1) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(0.5, 350, 0, 100, 20) + lvl(-3))
        .L(3, smp("Knock", 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(0.5, 60, 0, 40, 20) + lvl(-6)));
    return r;
}
}  // namespace slingshot_detail

inline Song slingshot() {
    using namespace slingshot_detail;
    constexpr int kKickKey = 34;   // B flat 1, 58 Hz
    const std::string ultra = "docs/dist/in07/ultra/";
    Song s;
    s.title = "Slingshot"; s.style = "dubstep (brostep)"; s.bpm = 140; s.bars = 60;
    s.sections = {{"intro", 0, 8, false}, {"build", 8, 16, false}, {"drop 1", 16, 32, true}, {"break", 32, 40, false}, {"drop 2", 40, 56, true}, {"outro", 56, 60, false}};
    s.target = -9.0; s.clipDrive = 10.0;

    // the chords (Fm7, Db, Ab, Eb): the pad in octave 3, the saw and the arpeggio higher, close voice leading
    const std::vector<std::vector<int>> pad = {{53, 56, 60, 63}, {49, 53, 56, 60}, {44, 48, 51, 55}, {51, 55, 58, 63}};
    const std::vector<std::vector<int>> top = {{65, 68, 72}, {65, 68, 73}, {63, 68, 72}, {63, 67, 70}};

    Track kick = songTrack("kick", kickRecipe(), 4.0), snare = songTrack("snare", snareRecipe(), 2.0);
    Track hat = songTrack("hat", hatRecipe(false), -4.0), ohat = songTrack("open hat", hatRecipe(true), -7.0);
    Track riser = songTrack("riser", riserRecipe(), -6.0), impact = factoryTrack("impact", "Impact Moon", -3.0), down = factoryTrack("downlifter", "Downlifter", -6.0);
    Track padT = factoryTrack("pad (Glass Horizon)", "Glass Horizon", -13.0, 5.0), arp = factoryTrack("arp (Glass Pluck)", "Glass Pluck", -16.0, 5.0);
    Track saw = userTrack("Ultra Saw", ultra + "Ultra Saw.swpreset", -7.0, 5.0);
    Track bass = userTrack("Ultra Bass", ultra + "Ultra Bass.swpreset", -1.0, 8.0), wob = userTrack("Ultra Wobble", ultra + "Ultra Wobble.swpreset", -1.0, 8.0);
    Track rid = userTrack("Ultra Riddim", ultra + "Ultra Riddim.swpreset", 1.0, 8.0);
    Track scr = songTrack("Screech", screechRecipe(), -4.0, 6.0), yoi = songTrack("Yoi", yoiRecipe(), -1.0, 8.0);
    Track zap = songTrack("Laser", laserRecipe(), -3.0), metal = songTrack("Metal", metalRecipe(), -2.0);
    bass.at(Bend) = 12;   // Ultra Bass dives an octave on the bend
    // the pluck through SWINGBY's arpeggiator: up and down, 1/16, two octaves
    arp.at(ArpOn) = 1; arp.at(ArpMode) = 2; arp.at(ArpRate) = 1; arp.at(ArpOctaves) = 2;

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
    const double sawLevel = saw.at(Level);   // its preset level; the build comes up 8 dB to it
    saw.ramp(Level, bt(8), bt(15, 3), sawLevel - 8, sawLevel);
    for (int c = 0; c < 3; ++c) padT.chord(bt(8 + c * 2), 8.0 - 0.05, pad[static_cast<size_t>(c)], 0.6);
    for (int c = 0; c < 3; ++c) arp.chord(bt(8 + c * 2), 8.0 - 0.02, top[static_cast<size_t>(c)], 0.7);
    for (int bar = 8; bar < 14; ++bar) for (int q = 0; q < 4; ++q) kick.note(bt(bar, q), 0.5, kKickKey, 0.55 + 0.03 * (bar - 8));   // growing
    snareRoll(8, 8, 55);
    riseInto(riser, bt(8), bt(15, 3));

    // ---- the drops: a different bass every half beat. Patterns of one bar on a root (A: growl with a dive, zap, gated screech, vowel, metal
    // on the snare, wobble; B: vowel, growl stutter, screech bent up, zap, growl dive, wobble; C: growl into a 1/64 stutter, gated screech,
    // metal and vowel, two zaps, then half a beat of nothing; D: the end of a phrase, all of it stopping for the last beat)
    auto dive = [&](Track& t, double b0, double b1) { t.bendRamp(b0, b1, 0.0, -1.0); t.bend(b1 + 0.02, 0.0); };
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
        scr.bend(b + 2.0, -0.5); scr.note(b + 2.0, 0.7, r + 27, 0.9); scr.bendRamp(b + 2.0, b + 2.3, -0.5, 0.0);
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
    riseInto(riser, bt(36), bt(39, 3));

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

    for (Track* t : {&kick, &snare, &hat, &ohat}) t->role = "drums";
    for (Track* t : {&bass, &wob, &rid, &scr, &yoi, &zap, &metal}) t->role = "lead";
    for (Track* t : {&padT, &arp}) t->role = "bed";
    s.tracks = {kick, snare, hat, ohat, riser, impact, down, padT, arp, saw, bass, wob, rid, scr, yoi, zap, metal};
    return s;
}

}  // namespace sw::in07::songs
