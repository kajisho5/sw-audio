// "Escape Velocity": a drum & bass demo made only with SWINGBY (2026-10-10, the client's "7以外ぜんぶ！": genre demos). An original piece in
// the (liquid) style's ways only: a two-step break (kick on 1 and the "and" of 3, snare on 2 and 4, ghost notes), a sine sub with a reese
// (two saws that beat, lock and drift: the factory's Drift Bass) an octave over it, electric piano chords, an atmospheric pad, a pluck line,
// a riser and a snare roll into the drop. No melody or break of any record.
// 174 bpm, F minor: Fm9 - Dbmaj9 - Bbm9 - C7sus4, two bars each. Intro 8, drop 16, break 4, drop 8, outro 4.
#pragma once
#include "in07_songkit.hpp"

namespace sw::in07::songs {
using namespace sw::in07::songkit;

inline Song escapeVelocity() {
    Song s;
    s.title = "Escape Velocity"; s.style = "drum & bass"; s.bpm = 174; s.bars = 40;
    s.sections = {{"intro", 0, 8, false}, {"drop", 8, 24, true}, {"break", 24, 28, false}, {"drop 2", 28, 36, true}, {"outro", 36, 40, false}};
    s.target = -9.5; s.clipDrive = 9.0; s.duckRelease = 0.09;

    const std::vector<std::vector<int>> ep = {{56, 60, 63, 67}, {53, 56, 60, 63}, {56, 60, 61, 65}, {55, 58, 60, 65}};
    const int roots[4] = {29, 25, 34, 36};   // F1 Db1 Bb1 C2

    Track kick = songTrack("kick", kickShaped(220, 60, 30, -8), 3.0), snare = songTrack("snare", snareRecipe(), 3.0);
    Track hat = songTrack("hat", hatRecipe(false), -3.0), ohat = songTrack("open hat", hatRecipe(true), -7.0), crash = songTrack("crash", crashRecipe(), -10.0);
    Track riser = songTrack("riser", riserRecipe(), -9.0);
    Track pad = factoryTrack("pad (Deep Field)", "Deep Field", -15.0, 3.0);
    Track keys = factoryTrack("keys (Tine Piano)", "Tine Piano", -9.0, 4.0);
    Track pluck = factoryTrack("pluck (Harp Light)", "Harp Light", -13.0, 3.0);
    Track sub = factoryTrack("sub (Sub Orbit)", "Sub Orbit", -3.0, 8.0);
    Track reese = factoryTrack("reese (Drift Bass)", "Drift Bass", -7.0, 7.0);
    for (Track* t : {&kick, &snare, &hat, &ohat, &crash}) t->role = "drums";
    for (Track* t : {&reese, &pluck}) t->role = "lead";
    pad.role = "bed";
    reese.at(Macro1) = 75;   // Bright: the reese's filter a little open (its harmonics up into the 1-2 kHz presence)

    auto chordsAt = [&](int bar0, int bars, double vel) {   // the pad and the keys over [bar0, bar0 + bars), two bars a chord
        for (int b = bar0; b < bar0 + bars; b += 2) {
            const int c = ((b - bar0) / 2) % 4;
            pad.chord(bt(b), 8.0 - 0.05, ep[static_cast<size_t>(c)], vel * 0.8);
            keys.chord(bt(b), 3.0, ep[static_cast<size_t>(c)], vel);              // a long chord, then a push on the "and" of 4 of the 2nd bar
            keys.chord(bt(b + 1, 3.5), 0.9, ep[static_cast<size_t>(c)], vel * 0.8);
        }
    };
    auto breakbeat = [&](int bar, bool fillEnd) {
        kick.note(bt(bar), 0.3, 36, 0.95); kick.note(bt(bar, 2.5), 0.3, 36, 0.9);
        snare.note(bt(bar, 1), 0.3, 55, 0.95);
        snare.note(bt(bar, 1.75), 0.1, 55, 0.3);   // a ghost
        if (!fillEnd) { snare.note(bt(bar, 3), 0.3, 55, 0.95); snare.note(bt(bar, 3.75), 0.1, 55, 0.25); }
        else for (int k = 0; k < 4; ++k) snare.note(bt(bar, 3.0 + k * 0.25), 0.1, 56 + k, 0.6 + 0.1 * k);
        for (int k = 0; k < 8; ++k) {
            if (k == 7) { ohat.note(bt(bar, 3.5), 0.4, 60, 0.6); continue; }
            hat.note(bt(bar, k * 0.5), 0.1, 60, k % 2 ? 0.5 : 0.75);
            hat.note(bt(bar, k * 0.5 + 0.25), 0.05, 60, 0.3);
        }
    };
    auto bassAt = [&](int bar0, int bars) {
        for (int b = bar0; b < bar0 + bars; b += 2) {
            const int c = ((b - bar0) / 2) % 4, r = roots[c];
            const bool end = b + 2 == bar0 + bars;
            sub.note(bt(b), end ? 7.0 : 8.0 - 0.05, r, 0.9);
            reese.note(bt(b), 2.5, r + 12, 0.85); reese.note(bt(b, 3.0), 1.0, r + 12, 0.75);   // the reese: held, then answering
            reese.note(bt(b + 1, 0.5), 1.5, r + 12, 0.8); reese.note(bt(b + 1, 2.5), end ? 0.5 : 1.25, r + 15, 0.8);
        }
    };
    const struct { double b, len; int key; } line[] = {{0, 0.5, 72}, {0.5, 0.5, 75}, {1, 1.0, 77}, {2.5, 0.5, 75}, {3, 1.0, 72},
                                                       {4, 0.5, 70}, {4.5, 0.5, 72}, {5, 1.5, 68}, {7, 1.0, 67}};
    auto lineAt = [&](int bar0, int bars) {
        for (int b = bar0; b < bar0 + bars; b += 2)
            for (const auto& n : line) pluck.note(bt(b) + n.b, n.len - 0.05, n.key + 12 + (((b - bar0) / 2) % 4 == 2 ? -2 : 0), 0.8);   // a step lower on Bbm9
    };

    // ---- intro (0-7): the pad and the keys; hats from bar 4; the riser and a snare roll into the drop
    chordsAt(0, 8, 0.65);
    for (int bar = 4; bar < 8; ++bar) for (int k = 0; k < 8; ++k) hat.note(bt(bar, k * 0.5), 0.1, 60, k % 2 ? 0.45 : 0.6);
    riseInto(riser, bt(4), bt(7, 3));
    for (int bar = 6; bar < 8; ++bar) {
        const double step = bar == 6 ? 0.5 : 0.25;
        for (double x = 0; x < (bar == 7 ? 3.0 : 4.0) - 1e-9; x += step) snare.note(bt(bar, x), 0.1, 55 + static_cast<int>(x + 4 * (bar - 6)), 0.4 + 0.07 * (x + 4 * (bar - 6)));
    }

    // ---- drop (8-23), break (24-27), drop 2 (28-35), outro (36-39)
    for (int bar = 8; bar < 24; ++bar) breakbeat(bar, bar == 15 || bar == 23);
    for (int bar = 28; bar < 36; ++bar) breakbeat(bar, bar == 35);
    for (int b : {8, 16, 28}) crash.note(bt(b), 2.0, 60, 0.8);
    chordsAt(8, 16, 0.7); bassAt(8, 16); lineAt(16, 8);   // (the line from the drop's second half: bar 16 starts the progression again)
    chordsAt(24, 4, 0.6);
    for (int bar = 24; bar < 28; ++bar) for (int k = 0; k < 8; ++k) hat.note(bt(bar, k * 0.5), 0.1, 60, k % 2 ? 0.4 : 0.55);
    for (int k = 0; k < 8; ++k) snare.note(bt(27, k * 0.375), 0.1, 56 + k, 0.4 + 0.07 * k);   // a dotted roll back in
    chordsAt(28, 8, 0.7); bassAt(28, 8); lineAt(28, 8);
    chordsAt(36, 4, 0.55);
    sub.note(bt(36), 6.0, 29, 0.8); kick.note(bt(36), 0.4, 36, 0.8); crash.note(bt(36), 2.0, 60, 0.6);

    s.tracks = {kick, snare, hat, ohat, crash, riser, pad, keys, pluck, sub, reese};
    return s;
}

}  // namespace sw::in07::songs
