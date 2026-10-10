// "Neon Coastline": a synthwave demo made only with SWINGBY (2026-10-10, the client's "7以外ぜんぶ！": genre demos). An original piece in the
// style's ways only: eighth-note saw bass on the roots, a warm analog pad, the arpeggiator running sixteenths over two octaves, a big snare in
// a long room, tom fills, a detuned lead with glide and vibrato for the chorus. No melody of any record.
// 105 bpm, A minor: Am - F - C - G, a bar each. Intro 4, verse 8, chorus 8, outro 4.
#pragma once
#include "in07_songkit.hpp"

namespace sw::in07::songs {
using namespace sw::in07::songkit;

namespace neon_detail {
using namespace sw::in07::dsl;
// the 80s snare: a triangle body and noise, driven a little, into a big bright room (most of its size is the room)
inline const Raw& bigSnare() {
    static const Raw r = R("Big Snare", "FX", N().drv(25, 60).eq(-2, 2, 3).rev(2.6, 38, 20)
        .mod(1, "Env 2", "Pitch", 60)
        .L(1, wave("Triangle", 0, 1, 0, 0) + flt("LP 12", 7000, 0, 0, 20, 0) + fenv(0.5, 30, 0, 20) + amp(0.5, 140, 0, 80, 40))
        .L(2, smp("Static", 0, 2, 70) + flt("BP 12", 2800, 10, 0, 20, 0) + fenv(0.5, 1, 0, 1) + amp(0.5, 200, 0, 90, 40) + lvl(-1)));
    return r;
}
}  // namespace neon_detail

inline Song neonCoastline() {
    using namespace neon_detail;
    Song s;
    s.title = "Neon Coastline"; s.style = "synthwave"; s.bpm = 105; s.bars = 24;
    s.sections = {{"intro", 0, 4, false}, {"verse", 4, 12, false}, {"chorus", 12, 20, true}, {"outro", 20, 24, false}};
    s.target = -11.0; s.clipDrive = 6.0; s.duckRelease = 0.10;

    const std::vector<std::vector<int>> pad = {{57, 60, 64, 69}, {57, 60, 65, 69}, {55, 60, 64, 67}, {55, 59, 62, 67}};   // Am F C G (close)
    const std::vector<std::vector<int>> arpKeys = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};
    const int roots[4] = {33, 29, 36, 31};   // A1 F1 C2 G1

    Track kick = songTrack("kick", kickShaped(260, 50, 25, -10), 2.0), snare = songTrack("snare", bigSnare(), 4.0);
    Track hat = songTrack("hat", hatRecipe(false), -3.0), tom = songTrack("toms", tomRecipe(), -6.0), crash = songTrack("crash", crashRecipe(), -10.0);
    Track padT = factoryTrack("pad (Warm Analog Pad)", "Warm Analog Pad", -9.0, 2.0);
    Track arp = factoryTrack("arp (Arp Pulse)", "Arp Pulse", -10.0, 3.0);
    Track bass = factoryTrack("bass (Saw Bass)", "Saw Bass", -2.0, 5.0);
    Track lead = factoryTrack("lead (Detuned Lead)", "Detuned Lead", -6.0, 2.0);
    Track riser = songTrack("riser", riserRecipe(), -12.0);
    Track sub = factoryTrack("sub (Sub Orbit)", "Sub Orbit", -6.0, 5.0);   // under the chorus
    for (Track* t : {&kick, &snare, &hat, &tom, &crash}) t->role = "drums";
    for (Track* t : {&lead, &bass}) t->role = "lead";
    padT.role = "bed";

    // ---- the pad and the arpeggio through the song; the arp opens (Bright) as it goes
    for (int bar = 0; bar < 24; ++bar) {
        const int c = bar % 4;
        padT.chord(bt(bar), 4.0 - 0.04, pad[static_cast<size_t>(c)], bar < 4 ? 0.55 : 0.65);
        if (bar >= 2) arp.chord(bt(bar), 4.0 - 0.02, arpKeys[static_cast<size_t>(c)], 0.75);
    }
    arp.ramp(Macro1, bt(2), bt(12), 20, 55);
    arp.ramp(Macro1, bt(20), bt(24), 55, 20);

    // ---- verse and chorus: eighths on the root (the octave on the last two of each bar), the drums
    for (int bar = 4; bar < 20; ++bar) {
        const int c = bar % 4, r = roots[c];
        for (int k = 0; k < 8; ++k) bass.note(bt(bar, k * 0.5), 0.42, (k >= 6 && bar >= 12) ? r + 12 : r, k % 2 ? 0.7 : 0.9);
        const bool chorus = bar >= 12;
        const bool fill = bar == 11 || bar == 19;
        for (int q = 0; q < 4; ++q) {
            if (fill && q == 3) break;
            if (chorus || q % 2 == 0) kick.note(bt(bar, q), 0.3, 36, 0.9);   // the verse on 1 and 3, the chorus four on the floor
        }
        if (!chorus) kick.note(bt(bar, 2.5), 0.3, 36, 0.7);
        snare.note(bt(bar, 1), 0.4, 55, 0.9);
        if (!fill) snare.note(bt(bar, 3), 0.4, 55, 0.9);
        for (int k = 0; k < 8; ++k) {
            if (fill && k >= 6) break;
            hat.note(bt(bar, k * 0.5), 0.1, 60, k % 2 ? 0.7 : 0.45);   // the offbeats louder
        }
        if (chorus) sub.note(bt(bar), 3.9, r, 0.85);
        if (fill) for (int k = 0; k < 4; ++k) tom.note(bt(bar, 3.0 + k * 0.25), 0.25, 52 - 4 * k, 0.75 + 0.05 * k);   // high to low
    }
    for (int b : {4, 12, 20}) crash.note(bt(b), 2.0, 60, 0.8);
    riseInto(riser, bt(10), bt(11, 3));

    // ---- chorus (12-19): the lead, twice (the second time resolving down to A)
    const struct { double b, len; int key; } mel[] = {{0, 1.5, 76}, {1.5, 0.5, 74}, {2, 1, 72}, {3, 1, 71},
                                                      {4, 1, 69}, {5, 0.5, 72}, {5.5, 0.5, 74}, {6, 2, 76},
                                                      {8, 1.5, 79}, {9.5, 0.5, 77}, {10, 1, 76}, {11, 1, 74},
                                                      {12, 1.5, 74}, {13.5, 0.5, 76}, {14, 2, 71}};
    for (int rep = 0; rep < 2; ++rep)
        for (const auto& n : mel) {
            int key = n.key; double len = n.len;
            if (rep == 1 && n.b >= 14) { key = 69; len = 2.5; }
            lead.note(bt(12 + rep * 4) + n.b, len - 0.04, key, 0.85);
        }
    lead.wheel(bt(12), 0.0); lead.ramp(Macro1, bt(12), bt(20), 35, 55, 4);

    // ---- outro (20-23): the pad, the arp closing, a last low A under it
    bass.note(bt(20), 6.0, 33, 0.8);
    kick.note(bt(20), 0.4, 36, 0.8);

    s.tracks = {kick, snare, hat, tom, crash, riser, padT, arp, bass, sub, lead};
    return s;
}

}  // namespace sw::in07::songs
