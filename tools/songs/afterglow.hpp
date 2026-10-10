// "Afterglow": a future bass demo made only with SWINGBY (2026-10-10, the client's "7以外ぜんぶ！": genre demos, about a minute each). An
// original piece in the style's ways only: big detuned chords that pump under the kick and play a syncopated rhythm, a formant "vocal" lead
// sliding between notes, a pluck counter-line, a sine sub, claps on 2 and 4 of the half-time, triplet hat rolls, a riser and a pitch rise
// into the drop. No melody or chord rhythm of any record.
// 150 bpm (half time in the drop), D flat major: G flat maj7 - A flat add9 - F m7 - B flat m9, a bar each. Intro 4, build 8, drop 16, outro 4.
#pragma once
#include "in07_songkit.hpp"

namespace sw::in07::songs {
using namespace sw::in07::songkit;

inline Song afterglow() {
    Song s;
    s.title = "Afterglow"; s.style = "future bass"; s.bpm = 150; s.bars = 32;
    s.sections = {{"intro", 0, 4, false}, {"build", 4, 12, false}, {"drop", 12, 28, true}, {"outro", 28, 32, false}};
    s.target = -10.0; s.clipDrive = 8.0; s.duckRelease = 0.16;   // -10: at -9.5 the limiter took 4 LU off

    const std::vector<std::vector<int>> chords = {{54, 61, 65, 70}, {56, 63, 70, 72}, {53, 60, 63, 68}, {58, 61, 65, 72}};
    const int roots[4] = {30, 32, 29, 34};   // G flat 1, A flat 1, F 1, B flat 1

    Track kick = songTrack("kick", kickShaped(320, 70, 30, -12), 3.0);
    Track clap = songTrack("clap", clapRecipe(), 12.0), hat = songTrack("hat", hatRecipe(false), 0.0), crash = songTrack("crash", crashRecipe(), -9.0);
    Track riser = songTrack("riser", riserRecipe(), -8.0), impact = factoryTrack("impact", "Impact Moon", -5.0);
    Track pad = factoryTrack("pad (Gravity Choir)", "Gravity Choir", -10.0, 4.0);
    Track pluck = factoryTrack("pluck (Kalimba Moon)", "Kalimba Moon", -9.0, 3.0);
    Track saw = factoryTrack("chords (Anthem Supersaw)", "Anthem Supersaw", -5.0, 8.0);
    Track vox = factoryTrack("vocal lead (Vowel Lead)", "Vowel Lead", -6.0, 4.0);
    Track sub = factoryTrack("sub (Sub Orbit)", "Sub Orbit", -2.0, 9.0);
    Track mid = factoryTrack("mid bass (Saw Bass)", "Saw Bass", -4.0, 8.0);
    for (Track* t : {&kick, &clap, &hat, &crash}) t->role = "drums";
    for (Track* t : {&saw, &vox, &mid}) t->role = "lead";
    for (Track* t : {&pad}) t->role = "bed";
    saw.at(Bend) = 12;   // the chords rise an octave into the drop

    // ---- intro (0-3): the choir pad, the kalimba's line (the drop's melody, sparser), an octave up
    const struct { double b, len; int key; } line[] = {{0, 0.5, 77}, {0.75, 0.5, 75}, {1.5, 0.75, 73}, {2.5, 1.0, 70}, {4, 0.75, 75}, {4.75, 0.75, 72},
                                                       {5.5, 1.25, 70}, {8, 0.5, 77}, {8.75, 0.5, 80}, {9.5, 0.75, 77}, {10.5, 1.0, 75}, {12, 0.75, 73},
                                                       {12.75, 0.75, 72}, {13.5, 2.0, 68}};
    for (int c = 0; c < 4; ++c) pad.chord(bt(c), 4.0 - 0.05, chords[static_cast<size_t>(c)], 0.6);
    for (const auto& n : line) pluck.note(n.b, n.len, n.key + 12, 0.7);

    // ---- build (4-11): the chords held and opening (Bright 10 -> 60), the kick on the beat from bar 8, claps doubling up, hats in triplets,
    // the riser; the last beat silent, the chords bent up an octave over it
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 4; ++c) saw.chord(bt(4 + r * 4 + c), (r == 1 && c == 3) ? 3.0 : 4.0 - 0.05, chords[static_cast<size_t>(c)], 0.75);
    saw.ramp(Macro1, bt(4), bt(11, 3), 10, 60);
    for (int c = 0; c < 4; ++c) pad.chord(bt(4 + c), 4.0 - 0.05, chords[static_cast<size_t>(c)], 0.5);
    for (const auto& n : line) pluck.note(bt(4) + n.b, n.len, n.key + 12, 0.75);
    for (int bar = 8; bar < 11; ++bar) for (int q = 0; q < 4; ++q) kick.note(bt(bar, q), 0.3, 36, 0.6 + 0.05 * (bar - 8));
    for (int bar = 6; bar < 12; ++bar) {   // claps: quarters, then eighths, then sixteenths in the last bar (but its last beat)
        const double step = bar < 9 ? 1.0 : bar < 11 ? 0.5 : 0.25;
        for (double x = 0; x < (bar == 11 ? 3.0 : 4.0) - 1e-9; x += step) clapAt(clap, bt(bar, x), s.bpm, 0.5 + 0.04 * (bar - 6) + 0.05 * x / 4);
    }
    for (int bar = 4; bar < 11; ++bar) for (int k = 0; k < 12; ++k) hat.note(bt(bar, k / 3.0), 0.1, 60, k % 3 == 0 ? 0.55 : 0.35);   // triplets
    riseInto(riser, bt(8), bt(11, 3));
    saw.bendRamp(bt(11, 0), bt(11, 3), 0.0, 1.0); saw.bend(bt(12) - 0.01, 0.0);
    vox.note(bt(11, 2), 0.9, 80, 0.8); vox.bendRamp(bt(11, 2), bt(11, 2.9), -1.0, 0.0);   // a vocal swoop into the drop

    // ---- drop (12-27): half time. The chords in a syncopated rhythm (on 1, the "a" of 1, the "and" of 2, 3, the "e" of 4), pumping under
    // the kick; the sub on the roots; the vocal lead's line; claps on 3; triplet hat rolls at the end of each 4 bars
    impact.note(bt(12), 2.0, 60, 0.9); crash.note(bt(12), 2.0, 60, 0.8); crash.note(bt(20), 2.0, 60, 0.7);
    saw.param(bt(12), Macro1, 60);
    const double rhythm[][2] = {{0, 0.6}, {0.75, 0.6}, {1.5, 0.9}, {2.5, 0.4}, {3.0, 0.6}, {3.25, 0.7}};
    for (int bar = 12; bar < 28; ++bar) {
        const int c = (bar - 12) % 4;
        const bool last = bar == 27;
        for (const auto& r : rhythm) {
            if (last && r[0] >= 3.0) break;
            saw.chord(bt(bar, r[0]), r[1] - 0.03, chords[static_cast<size_t>(c)], 0.85);
            mid.note(bt(bar, r[0]), r[1] - 0.03, roots[c] + 12, 0.9);   // the root an octave over the sub, with the chords (the low mids' body)
        }
        sub.note(bt(bar), last ? 2.9 : 3.95, roots[c], 0.9);
        kick.note(bt(bar), 0.4, 36, 0.95);
        kick.note(bt(bar, (bar % 2) ? 2.5 : 1.75), 0.3, 36, 0.8);
        clapAt(clap, bt(bar, 2), s.bpm, 0.95);
        const bool roll = (bar - 12) % 4 == 3;
        for (int k = 0; k < 8; ++k) {
            if (roll && k >= 6) break;
            hat.note(bt(bar, k * 0.5 + 0.25), 0.1, 60, k % 2 ? 0.5 : 0.65);   // the offbeat sixteenths
        }
        if (roll && !last) for (int k = 0; k < 6; ++k) hat.note(bt(bar, 3.0 + k / 6.0), 0.05, 60, 0.4 + 0.07 * k);   // 1/24 roll
    }
    for (int rep = 0; rep < 4; ++rep) for (const auto& n : line) {
        if (rep == 3 && n.b >= 12) continue;
        vox.note(bt(12 + rep * 4) + n.b, n.len - 0.05, n.key, 0.85);
    }
    for (int rep = 0; rep < 4; ++rep) {   // the slides: into the long notes, a bend up from a whole tone under
        vox.bend(bt(12 + rep * 4, 2.5) - 0.001, -1.0); vox.bendRamp(bt(12 + rep * 4, 2.5), bt(12 + rep * 4, 2.75), -1.0, 0.0);
    }
    for (int rep = 2; rep < 4; ++rep) for (const auto& n : line) pluck.note(bt(12 + rep * 4) + n.b + 0.5, 0.25, n.key + 12, 0.5);   // an echo

    // ---- outro (28-31): the pad and the kalimba, the chords' last hit ringing
    for (int c = 0; c < 4; ++c) pad.chord(bt(28 + c), c == 3 ? 6.0 : 4.0 - 0.05, chords[static_cast<size_t>(c)], 0.6);
    for (const auto& n : line) pluck.note(bt(28) + n.b, n.len, n.key + 12, 0.6);
    saw.chord(bt(28), 2.0, chords[0], 0.6);
    crash.note(bt(28), 2.0, 60, 0.6);

    s.tracks = {kick, clap, hat, crash, riser, impact, pad, pluck, saw, mid, vox, sub};
    return s;
}

}  // namespace sw::in07::songs
