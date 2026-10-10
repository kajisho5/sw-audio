// "Low Orbit": a lo-fi hip hop demo made only with SWINGBY (2026-10-10, the client's "7以外ぜんぶ！": genre demos). An original piece in the
// style's ways only: swung, soft boom-bap drums played a little loose, an FM electric piano on jazz sevenths and ninths with a slow tape wow
// (the pitch bend moving), a warm round bass, a lazy pluck line, vinyl crackle and rain under it all. No melody of any record.
// 80 bpm, C major: Fmaj9 - Em7 - Dm9 - Cmaj9, a bar each. Intro 2, A 8, B 8 (the line), outro 2.
#pragma once
#include "in07_songkit.hpp"
#include <cstdint>

namespace sw::in07::songs {
using namespace sw::in07::songkit;

namespace lofi_detail {
using namespace sw::in07::dsl;
// a soft snare: a higher triangle body, a knock (the rim) and a little noise, darkened, a small room
inline const Raw& softSnare() {
    static const Raw r = R("Soft Snare", "FX", N().eq(1, 0, -4).rev(1.0, 14, 70)
        .mod(1, "Env 2", "Pitch", 40)
        .L(1, wave("Triangle", 0, 1, 0, 0) + flt("LP 12", 4000, 0, 0, 10, 0) + fenv(0.5, 20, 0, 20) + amp(0.5, 90, 0, 60, 60))
        .L(2, smp("Knock", 1) + flt("LP 12", 5000, 0, 0, 0, 0) + amp(0.5, 50, 0, 40, 60) + lvl(-4))
        .L(3, smp("Static", 0, 2, 50) + flt("BP 12", 2500, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 160, 0, 80, 60) + lvl(-9)));
    return r;
}
// a dull hat: Tick and Static band-passed around 6 kHz
inline const Raw& dullHat() {
    static const Raw r = R("Dull Hat", "FX", N()
        .L(1, smp("Tick", 1) + flt("BP 12", 6000, 10, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 40, 0, 30, 70))
        .L(2, smp("Static", 1, 1) + flt("BP 12", 7000, 0, 0, 0, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 30, 0, 25, 70) + lvl(-8)));
    return r;
}
// vinyl: a crackle (a click, high-passed, very short) played at random times
inline const Raw& crackle() {
    static const Raw r = R("Crackle", "FX", N()
        .L(1, smp("Tick", 0) + flt("HP 12", 2500, 0, 0, 0, 0) + amp(0.5, 6, 0, 5, 100)));
    return r;
}
// a noise bed: the Static sample band-passed, held
inline const Raw& hiss() {
    static const Raw r = R("Hiss", "FX", N()
        .L(1, smp("Static", 0, 2, 100) + flt("BP 12", 4500, 0, 0, 0, 0) + amp(200, 100, 100, 1000)));
    return r;
}
}  // namespace lofi_detail

inline Song lowOrbit() {
    using namespace lofi_detail;
    Song s;
    s.title = "Low Orbit"; s.style = "lo-fi hip hop"; s.bpm = 80; s.bars = 20;
    s.sections = {{"intro", 0, 2, false}, {"A", 2, 10, true}, {"B", 10, 18, true}, {"outro", 18, 20, false}};
    s.target = -13.0; s.clipDrive = 3.0; s.duckRelease = 0.12; s.tail = 5.0; s.fadeOut = 3.0;

    const std::vector<std::vector<int>> chords = {{53, 57, 64, 67}, {52, 59, 62, 67}, {50, 57, 60, 64}, {48, 55, 59, 62}};
    const int roots[4] = {41, 40, 38, 36};   // F2 E2 D2 C2
    const int approach[4] = {43, 39, 37, 40};  // into the next root: G2 (the 9th) to E, E flat and C sharp a half step over, E a half step under F
    uint32_t seed = 20261010;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };   // 0..1
    const double swingBeats = 0.08;   // the off sixteenths late by 60 ms
    auto sw = [&](double b) { const long i = std::lround(b * 4); return (std::abs(b * 4 - i) < 1e-6 && (i % 2)) ? b + swingBeats : b; };
    auto loose = [&](double b) { return sw(b) + (rnd() - 0.5) * 0.012; };   // +-4.5 ms

    Track kick = songTrack("kick", kickShaped(300, 40, 10, -16, true), 1.0), snare = songTrack("snare", softSnare(), 5.0);
    Track hat = songTrack("hat", dullHat(), -4.0), vinyl = songTrack("crackle", crackle(), -10.0), noise = songTrack("hiss", hiss(), -36.0);
    Track rain = factoryTrack("rain (Rain Planet)", "Rain Planet", -20.0);
    Track keys = factoryTrack("keys (Satellite EP)", "Satellite EP", -5.0, 2.0);
    Track bass = factoryTrack("bass (Liquid Bass)", "Liquid Bass", -4.5, 3.0);
    Track pluck = factoryTrack("line (Lo-Fi Pluck)", "Lo-Fi Pluck", -7.0, 1.5);
    for (Track* t : {&kick, &snare, &hat, &vinyl, &noise}) t->role = "drums";
    for (Track* t : {&pluck, &keys}) t->role = "lead";
    rain.role = "bed";

    // ---- under everything: rain, hiss, crackle (random clicks, about 7 a second, quieter ones more often)
    const double endBeat = bt(20) + 4.0;
    rain.note(0.0, endBeat, 60, 0.6); noise.note(0.0, endBeat, 60, 0.7);
    for (double b = 0; b < endBeat;) { vinyl.note(b, 0.02, 60 + static_cast<int>(rnd() * 12), 0.2 + 0.8 * rnd() * rnd()); b += 0.02 + rnd() * 0.2; }

    // ---- the keys through the song: a chord on 1, struck again on the "and" of 3 (swung, loose); the tape wow on the bend
    for (int bar = 0; bar < 20; ++bar) {
        const int c = bar % 4;
        const bool last = bar == 19;
        keys.chord(loose(bt(bar)), last ? 6.0 : 2.4, chords[static_cast<size_t>(c)], 0.7 + 0.1 * rnd());
        if (!last) keys.chord(loose(bt(bar, 2.5)), 1.3, chords[static_cast<size_t>(c)], 0.55 + 0.1 * rnd());
    }
    for (double b = 0; b < endBeat; b += 0.125) {   // a slow wow (a 0.33 Hz sine, +-0.1 semitone at the default bend of 2) and a faster flutter
        const double t = b * 60.0 / s.bpm;
        const double v = 0.05 * std::sin(2 * M_PI * 0.33 * t) + 0.012 * std::sin(2 * M_PI * 3.1 * t);
        keys.bend(b, v); pluck.bend(b, v);
    }

    // ---- A and B (2-17): the drums, the bass; B adds the line
    for (int bar = 2; bar < 18; ++bar) {
        const int c = bar % 4, r = roots[c];
        kick.note(loose(bt(bar)), 0.3, 36, 0.9);
        kick.note(loose(bt(bar, 1.75)), 0.3, 36, 0.6);
        kick.note(loose(bt(bar, 2.5)), 0.3, 36, 0.8);
        snare.note(loose(bt(bar, 1)), 0.3, 60, 0.85 + 0.1 * rnd());
        snare.note(loose(bt(bar, 3)), 0.3, 60, 0.85 + 0.1 * rnd());
        if (bar % 4 == 3) snare.note(loose(bt(bar, 3.75)), 0.1, 60, 0.35);   // a ghost into the next phrase
        for (int k = 0; k < 8; ++k) hat.note(loose(bt(bar, k * 0.5)), 0.1, 60, (k % 2 ? 0.4 : 0.6) + 0.15 * rnd());
        if (bar % 2) hat.note(loose(bt(bar, 3.25)), 0.05, 60, 0.3);
        bass.note(loose(bt(bar)), 1.6, r, 0.85);
        bass.note(loose(bt(bar, 2.5)), 0.9, r, 0.7);
        bass.note(loose(bt(bar, 3.5)), 0.4, approach[c], 0.6);   // a step toward the next root
    }
    const struct { double b, len; int key; } line[] = {{0.5, 0.5, 76}, {1, 0.5, 74}, {1.5, 1.5, 72}, {3, 0.5, 69}, {4.5, 0.5, 71}, {5, 1, 74},
                                                       {6.5, 1.5, 67}, {8, 0.5, 69}, {8.5, 0.5, 72}, {9, 0.5, 74}, {9.5, 1.5, 76}, {11, 0.5, 74},
                                                       {12.5, 0.5, 72}, {13, 0.5, 71}, {13.5, 2.5, 67}};
    for (int rep = 0; rep < 2; ++rep) for (const auto& n : line) pluck.note(loose(bt(10 + rep * 4) + n.b), n.len - 0.05, n.key, 0.7 + 0.15 * rnd());

    // ---- outro (18-19): the keys alone, the last chord ringing
    s.tracks = {kick, snare, hat, vinyl, noise, rain, keys, bass, pluck};
    return s;
}

}  // namespace sw::in07::songs
