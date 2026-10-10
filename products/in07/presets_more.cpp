// SWINGBY (SW IN07) — the larger factory library (the client asked for many presets, 2026-10-08). Each preset is a recipe in the usual shape
// of its kind (pads swell and ring out, plucks and keys start at once and decay, basses are mono, sequences stay short); the loudness is
// measured and set by tools/in07_presets.cpp (preset_levels.inc), not here. Effects not named are off (N()).
#include "in07/preset_dsl.hpp"

namespace sw::in07::dsl {

const std::vector<Raw>& morePresets() {
    static const std::vector<Raw> t = {
        // ================= LEAD
        R("Trance Lead", "LEAD", N().dly("1/8 D", 40, 25).rev(2.5, 25)
            .L(1, saw(0, 7, 30, 80) + flt("LP 24", 5000, 20, 20, 20) + fenv(1, 500, 30, 300) + amp(2, 400, 90, 350))
            .L(2, saw(1, 3, 20, 70) + flt("LP 24", 6000, 10, 20, 0) + fenv(1, 500, 30, 300) + amp(2, 400, 90, 350) + lvl(-8))),
        R("Square Pilot", "LEAD", N().mono(30).lfo(1, "Triangle", 6.0).mod(1, "LFO 1", "Pitch", 0.8).dly("1/16", 30, 15).rev(1.2, 12)
            .L(1, pulse(50) + flt("LP 12", 12000) + amp(1, 200, 80, 120))),
        R("Sine Glide", "LEAD", N().legato(120).lfo(1, "Triangle", 5.5).mod(1, "LFO 1", "Pitch", 1.2).dly("1/8 D", 30, 18).rev(1.6, 18)
            .L(1, wave("Sine") + open() + amp(5, 500, 90, 300))
            .L(2, wave("Triangle", 1) + open() + amp(5, 500, 90, 300) + lvl(-14))),
        R("Acid Spike", "LEAD", N().mono(50).drv(35, 70).dly("1/16", 35, 15)
            .L(1, saw() + flt("LP 24", 900, 75, 70, 40) + fenv(0.5, 220, 10, 150) + amp(1, 300, 80, 150, 60))),
        R("FM Laser Lead", "LEAD", N().dly("1/8 D", 35, 20).rev(1.5, 15)
            .L(1, fm("2", 40, 10000, 30, 0, 2, 10, 60) + flt("LP 12", 9000) + amp(2, 300, 85, 250))
            .L(2, saw(0, 2, 15, 60) + flt("LP 24", 3000, 10) + amp(2, 300, 85, 250) + lvl(-10))),
        R("Vowel Lead", "LEAD", N().legato(60).lfo(1, "Triangle", 0.6).mod(1, "LFO 1", "WT position", 40).cho(0.3, 40, 25).dly("1/8 D", 30, 20).rev(2, 18)
            .L(1, wt("Formant", 20, 0, 2, 12, 50) + flt("LP 12", 8000) + amp(3, 400, 90, 300))),
        R("Screamer", "LEAD", N().mod(1, "Env 2", "WT position", 30).drv(50, 80).eq(-2, 0, 4).rev(1.4, 12)
            .L(1, wt("Sync", 60, 0, 4, 30, 80) + flt("LP 24", 7000, 25, 0, 70) + fenv(0.5, 300, 0, 200) + amp(1, 300, 90, 200))),
        R("Flute Moon", "LEAD", N().legato(40).lfo(1, "Triangle", 5.0).mod(1, "LFO 1", "Pitch", 0.8).dly("1/4", 25, 15).rev(2.2, 25)
            .L(1, wave("Triangle") + flt("LP 12", 4000) + amp(40, 300, 85, 250))
            .L(2, smp("Breath") + flt("BP 12", 2500, 10) + amp(40, 300, 85, 250) + lvl(-14))),
        R("Bell Lead", "LEAD", N().dly("1/8 D", 30, 20).rev(2.5, 25)
            .L(1, fm("7", 25, 600) + open() + amp(1, 1500, 40, 400))
            .L(2, wave("Sine") + open() + amp(2, 600, 80, 300) + lvl(-6))),
        // ULTRA (2026-10-10, in the place of Wide Saw Lead, a near duplicate of Anthem Supersaw): a festival supersaw, harder than Anthem
        // Supersaw: two stacks of 8 (one a hard-sync table for the bite), 6 an octave up and 2 an octave down (24 copies, the factory's
        // limit), each driven into its filter; then Drive, a presence EQ, a short delay and a room; the limiter pushed for density. The mod
        // wheel spreads the stacks further.
        R("Ultra Saw", "LEAD", N().drv(70, 90).eq(-3, 2, 5).dly("1/8 D", 25, 10).rev(1.4, 12)
            .g({{"fx.slot1", "Drive"}, {"fx.slot2", "EQ"}, {"fx.slot3", "Delay"}, {"fx.slot4", "Reverb"}, {"fx.slot5", "Chorus"}, {"fx.slot6", "Limit"},
                {"fx.limit.gain", 6}})
            .mod(1, "Mod wheel", "Detune", 30).mod(2, "Velocity", "Cutoff", 10)
            .L(1, wt("Classic", 0, 0, 8, 65, 100) + flt("LP 12", 14000, 0, 0, 70, 0) + amp(1, 300, 100, 250, 20))
            .L(2, wt("Sync", 35, 0, 8, 45, 90) + flt("LP 24", 7000, 10, 0, 60, 0) + amp(1, 300, 100, 250, 20) + lvl(-5))
            .L(3, saw(1, 6, 55, 100) + flt("LP 12", 18000, 0, 0, 50, 0) + amp(1, 300, 100, 250, 20) + lvl(-5))
            .L(4, saw(-1, 2, 20, 40) + flt("LP 24", 2200, 0, 0, 50, 0) + amp(1, 300, 100, 250, 20) + lvl(-7))),
        R("Gravity Lead", "LEAD", N().lfo(1, "Orbit", 0.5, 70).mod(1, "LFO 1", "Gravity", 100).dly("1/8 D", 30, 20).rev(2, 20)
            .L(1, saw(0, 6, 40, 80) + grav(0) + flt("LP 24", 5000, 15) + amp(2, 300, 90, 300))),
        R("Orbit Wah Lead", "LEAD", N().mono(40).lfoSync(1, "Orbit", "1/8", false, 60).mod(1, "LFO 1", "Cutoff", 25).dly("1/16", 30, 15).rev(1.2, 12)
            .L(1, wt("Bright", 70, 0, 3, 20, 60) + flt("LP 24", 3000, 40) + amp(1, 300, 90, 200))),
        R("Brass Lead", "LEAD", N().cho(0.4, 30, 25).rev(1.8, 18)
            .L(1, saw(0, 3, 15, 60) + flt("LP 24", 1200, 10, 60) + fenv(30, 400, 40, 300) + amp(15, 300, 85, 250, 60))
            .L(2, pulse(40) + flt("LP 24", 1200, 10, 60) + fenv(30, 400, 40, 300) + amp(15, 300, 85, 250, 60) + lvl(-6))),

        // ================= PAD
        R("Warm Analog Pad", "PAD", N().cho(0.25, 40, 35).rev(4, 35)
            .L(1, saw(0, 6, 25, 90) + flt("LP 24", 1500, 10, 20) + fenv(800, 2000, 60, 2500) + amp(800, 1500, 90, 2500))
            .L(2, pulse(40, -1, 2, 15, 60) + flt("LP 24", 800) + amp(1000, 1500, 90, 2500) + lvl(-8))),
        R("String Ensemble", "PAD", N().lfo(2, "Triangle", 4.5).mod(1, "LFO 2", "Pitch", 0.3).cho(0.6, 50, 40).rev(3, 30)
            .L(1, saw(0, 8, 18, 100) + flt("LP 12", 3500) + amp(400, 1500, 90, 2000))
            .L(2, saw(1, 4, 15, 100) + flt("LP 12", 5000) + amp(500, 1500, 90, 2000) + lvl(-10))),
        R("Cathedral Choir", "PAD", N().lfo(2, "Triangle", 0.08).mod(1, "LFO 2", "WT position", 20).rev(8, 50, 45)
            .L(1, wt("Formant", 75, 0, 8, 20, 100) + flt("LP 12", 4000) + amp(900, 2000, 90, 3000))
            .L(2, wt("Formant", 40, -1, 4, 15, 80) + flt("LP 12", 3000) + amp(900, 2000, 90, 3000) + lvl(-8))
            .L(3, smp("Breath") + flt("BP 12", 2000) + amp(1000, 2000, 100, 3000) + lvl(-20))),
        R("Glass Cloud", "PAD", N().lfo(1, "Triangle", 0.1).mod(1, "LFO 1", "WT position", 30).cho(0.2, 40, 30).dly("1/4 D", 35, 20).rev(6, 45)
            .L(1, wt("Glass", 60, 0, 6, 15, 100) + flt("LP 12", 9000) + amp(600, 2000, 80, 3500))
            .L(2, fm("4", 10, 10000, 0, 1) + open() + amp(700, 2000, 80, 3500) + lvl(-14))),
        R("Dust Pad", "PAD", N().drv(25, 30, 60).eq(0, -2, -4).rev(3, 30)
            .L(1, saw(0, 4, 30, 80) + flt("LP 24", 1200, 20, 0, 30) + amp(700, 1500, 90, 2500))
            .L(2, smp("Static") + flt("HP 12", 2000) + amp(500, 1500, 100, 2500) + lvl(-22))),
        R("PWM Dream", "PAD", N().lfo(1, "Triangle", 0.4).mod(1, "LFO 1", "Pulse width", 60).cho(0.3, 50, 35).rev(4, 35)
            .L(1, pulse(50, 0, 4, 20, 90) + flt("LP 24", 2500, 10) + amp(500, 1500, 90, 2500))
            .L(2, pulse(30, -1, 2, 15, 60) + flt("LP 24", 1200) + amp(500, 1500, 90, 2500) + lvl(-8))),
        R("Slow Sweep", "PAD", N().dly("1/4 D", 40, 20).rev(5, 40)
            .L(1, saw(0, 6, 30, 100) + flt("LP 24", 400, 40, 60) + fenv(4000, 4000, 50, 4000) + amp(1500, 2000, 100, 3000))
            .L(2, saw(1, 2, 20, 80) + flt("LP 24", 400, 40, 60) + fenv(4000, 4000, 50, 4000) + amp(1500, 2000, 100, 3000) + lvl(-10))),
        R("Orbit Shimmer", "PAD", N().lfo(1, "Orbit", 0.12, 80).mod(1, "LFO 1", "WT position", 50).lfo(2, "Orbit", 0.07, 60).mod(2, "LFO 2", "Pan", 30)
            .dly("1/4", 30, 18).rev(7, 45)
            .L(1, wt("Bright", 30, 0, 8, 25, 100) + flt("LP 12", 6000) + amp(1000, 2000, 90, 3500))
            .L(2, wave("Sine", 1) + open() + amp(1200, 2000, 90, 3500) + lvl(-16))),
        R("Deep Field", "PAD", N().lfo(1, "Triangle", 0.05).mod(1, "LFO 1", "WT position", 40).rev(10, 50, 60)
            .L(1, wt("Fold", 20, -1, 6, 20, 80) + grav(30) + flt("LP 24", 700, 20) + amp(2000, 3000, 100, 4000))
            .L(2, smp("Air") + flt("BP 12", 800, 10) + amp(2000, 3000, 100, 4000) + lvl(-18))),
        R("FM Halo Pad", "PAD", N().cho(0.3, 40, 30).rev(5, 40)
            .L(1, fm("2", 20, 4000, 0, 0, 4, 15, 90) + flt("LP 12", 6000) + amp(700, 2000, 90, 3000))
            .L(2, fm("1", 10, 10000, 0, -1, 2, 10, 60) + flt("LP 12", 3000) + amp(700, 2000, 90, 3000) + lvl(-6))),
        R("Breath Pad", "PAD", N().rev(6, 45)
            .L(1, smp("Breath", 0, 2, 100) + flt("BP 12", 1500, 10) + amp(800, 2000, 100, 3000))
            .L(2, wave("Sine") + open() + amp(1000, 2000, 100, 3000) + lvl(-10))
            .L(3, wt("Formant", 50, 0, 4, 15, 100) + flt("LP 12", 3000) + amp(1000, 2000, 90, 3000) + lvl(-12))),
        R("Pump Pad", "PAD", N().lfoSync(1, "Saw", "1/4", true).mod(1, "LFO 1", "Level", 100).cho(0.3, 30, 25).rev(2, 20)
            .L(1, saw(0, 8, 25, 100) + flt("LP 24", 3000, 10) + amp(150, 1500, 100, 1500))
            .L(2, saw(1, 4, 20, 100) + flt("LP 24", 4000) + amp(150, 1500, 100, 1500) + lvl(-8))),
        R("Frozen Pad", "PAD", N().dly("1/2", 45, 20).rev(9, 38)
            .L(1, wt("Glass", 80, 0, 6, 10, 80) + flt("HP 12", 400) + amp(1200, 2000, 90, 4000))),
        R("Tape Pad", "PAD", N().lfo(1, "Random", 0.5).mod(1, "LFO 1", "Pitch", 0.6).lfo(2, "Triangle", 0.3).mod(2, "LFO 2", "Pitch", 0.4)
            .cho(0.2, 50, 40).eq(1, 0, -3).rev(3, 30)
            .L(1, saw(0, 4, 20, 80) + flt("LP 24", 2000, 10) + amp(600, 1500, 90, 2500))
            .L(2, wave("Triangle", -1) + open() + amp(600, 1500, 90, 2500) + lvl(-6))),

        // ================= BASS
        R("Sub Pressure", "BASS", N().mono().drv(30, 40)   // a saturated sub: its harmonics carry it on small speakers (Sub Orbit is the clean one)
            .L(1, wave("Sine") + flt("LP 12", 6000, 0, 0, 70) + amp(1, 500, 100, 80, 20))),
        R("Acid Line", "BASS", N().mono(60).mod(1, "Velocity", "Cutoff", 20).drv(40, 60).dly("1/16", 25, 12)
            .L(1, saw() + flt("LP 24", 600, 80, 75, 50) + fenv(0.5, 180, 0, 120) + amp(1, 250, 80, 80, 70))),
        // ULTRA (2026-10-10, in the place of Wobble Monster): the classic wobble: detuned saws and a half-square table into resonant 24 dB
        // filters (not following the key) that a 1/8 triangle opens and shuts over 7 octaves, a sine sub under them (its filter stays open),
        // Drive; the mod wheel doubles the wobble (1/16)
        R("Ultra Wobble", "BASS", N().mono().drv(75, 100).eq(0, 3, 6)
            .g({{"fx.limit.gain", 3}})
            .lfoSync(1, "Triangle", "1/8", true)
            .mod(1, "LFO 1", "Cutoff", 70).mod(2, "LFO 1", "Resonance", 15).mod(3, "Mod wheel", "LFO 1 rate", 25)
            .L(1, saw(0, 3, 15, 15) + flt("LP 24", 700, 55, 0, 80, 0) + amp(1, 300, 100, 60, 10))
            .L(2, wt("Classic", 50, 0, 2, 12, 25) + flt("LP 24", 800, 40, 0, 70, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))
            .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 60, 10) + lvl(-5))),
        R("Boom Sub", "BASS", N().mono(50).mod(1, "Env 2", "Pitch", 50).drv(20, 50, 50)
            .L(1, wave("Sine") + flt("LP 12", 20000, 0, 0, 30) + fenv(0.5, 120, 0, 100) + amp(0.5, 1500, 0, 200))),
        R("Pulse Bass", "BASS", N().mono()
            .L(1, pulse(25) + flt("LP 24", 1200, 20, 40) + fenv(0.5, 200, 20, 100) + amp(1, 300, 90, 80))
            .L(2, wave("Sine", -1) + open() + amp(1, 300, 90, 80) + lvl(-6))),
        R("Saw Bass", "BASS", N().mono(20)
            .L(1, saw(0, 2, 8, 20) + flt("LP 24", 800, 25, 50, 25) + fenv(0.5, 250, 20, 100) + amp(1, 400, 90, 100))
            .L(2, pulse(50, -1) + flt("LP 24", 500) + amp(1, 400, 90, 100) + lvl(-8))),
        R("Pluck Bass", "BASS", N().mono().rev(0.8, 6)
            .L(1, saw(0, 2, 10, 30) + flt("LP 24", 300, 30, 80) + fenv(0.5, 150, 0, 100) + amp(0.5, 350, 40, 100, 60))
            .L(2, wave("Sine") + open() + amp(0.5, 400, 60, 100, 60) + lvl(-6))),
        // ULTRA (2026-10-10, in the place of Growl Fold, a near duplicate of Fold Bass): a heavy growl: a sine sub (one voice, centre) and
        // an FM growl with feedback (one voice, centre) carry the low end, so it stays mono for a club; a folded table (3 voices, wide) and a
        // formant table an octave up (the vowel) are band-passed, so their width is in the mids and the top only. The filters do not follow
        // the key (a low note stays bright). One LFO at 1/8 (from the note) sweeps the filters, the tables and the FM index together, the
        // mod wheel doubles it; Drive bright, an EQ that lifts the mids and the top, the limiter. Legato with a short glide (the riff talks).
        R("Ultra Bass", "BASS", N().legato(35).drv(85, 100).eq(0, 4, 7)
            .g({{"fx.limit.gain", 4}})
            .lfoSync(1, "Triangle", "1/8", true).lfoSync(2, "Random", "1/16")
            .mod(1, "LFO 1", "Cutoff", 50).mod(2, "LFO 1", "WT position", 45).mod(3, "LFO 1", "FM index", 35)
            .mod(4, "Mod wheel", "LFO 1 rate", 25).mod(5, "LFO 2", "Resonance", 15)
            .L(1, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 10, 0) + amp(1, 300, 100, 60, 10) + lvl(-6))
            .L(2, fm("1", 80, 10000, 60, 0, 1) + flt("LP 12", 3000, 35, 0, 90, 0) + amp(1, 300, 100, 60, 10))
            .L(3, wt("Fold", 70, 0, 3, 22, 90) + flt("BP 12", 1500, 15, 0, 80, 0) + amp(1, 300, 100, 60, 10) + lvl(-2))
            .L(4, wt("Formant", 30, 1, 2, 15, 80) + flt("BP 12", 2000, 45, 0, 50, 0) + amp(1, 300, 100, 60, 10) + lvl(-3))),
        R("Hollow FM Bass", "BASS", N().mono()
            .L(1, fm("0.5", 35, 300, 10) + flt("LP 12", 4000) + amp(1, 500, 70, 100))),
        R("Octave Funk", "BASS", N().mono(25)
            .L(1, saw() + flt("LP 24", 1500, 30, 60) + fenv(0.5, 200, 10, 100) + amp(1, 300, 70, 100, 70))
            .L(2, saw(-1) + flt("LP 24", 1500, 30, 60) + fenv(0.5, 200, 10, 100) + amp(1, 300, 70, 100, 70))),
        R("Grit Engine", "BASS", N().mono().drv(60, 50).eq(2, -2, 2)
            .L(1, saw(0, 3, 15, 40) + flt("LP 24", 2000, 20, 0, 70) + amp(1, 400, 100, 100))
            .L(2, smp("Grit") + flt("LP 24", 1500) + amp(1, 400, 100, 100) + lvl(-16))),
        // ULTRA (2026-10-10, in the place of Square Depth, a near duplicate of Pulse Bass): riddim: a square and a hard FM pair, chopped by
        // the trance gate on the 1/16 (a riddim pattern, on the host's beat), the filter alternating every 1/16 (a square LFO) and a new FM
        // brightness every 1/8 (random); the sub under it, Drive to the edge
        R("Ultra Riddim", "BASS", N().mono().drv(90, 80).eq(0, 2, 4)
            .g({{"fx.limit.gain", 3}})
            .gate("1/16", 95, {1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1})
            .lfoSync(1, "Square", "1/16", true).lfoSync(2, "Random", "1/8", true)
            .mod(1, "LFO 1", "Cutoff", 35).mod(2, "LFO 2", "FM index", 40).mod(3, "Mod wheel", "Drive", 30)
            .L(1, pulse(50, 0, 2, 8, 20) + flt("LP 24", 3000, 25, 0, 95, 0) + amp(1, 300, 100, 50, 10))
            .L(2, fm("2", 60, 10000, 70, 0, 1) + flt("LP 24", 4000, 20, 0, 70, 0) + amp(1, 300, 100, 50, 10) + lvl(-4))
            .L(3, wave("Sine", 0, 1, 0, 0) + flt("LP 12", 20000, 0, 0, 0, 0) + amp(1, 300, 100, 50, 10) + lvl(-5))),
        R("Stab Bass", "BASS", N().mono().rev(1.2, 10)
            .L(1, saw(0, 4, 20, 60) + flt("LP 24", 2000, 15, 30) + fenv(0.5, 150, 0, 80) + amp(0.5, 300, 35, 80))
            .L(2, wave("Sine", -1) + open() + amp(0.5, 300, 50, 80) + lvl(-6))),
        R("Liquid Bass", "BASS", N().legato(90).cho(0.2, 20, 15)
            .L(1, wave("Triangle") + flt("LP 12", 900, 10) + amp(2, 600, 90, 150))
            .L(2, wave("Sine", -1) + open() + amp(2, 600, 90, 150) + lvl(-4))),

        // ================= PLUCK
        R("Deep House Pluck", "PLUCK", N().dly("1/8 D", 45, 30).rev(2, 25)   // a rounder chord pluck with a sub body (Pizz Saw is the bright one)
            .L(1, wt("Classic", 50, 0, 2, 10, 50) + flt("LP 24", 1200, 35, 50) + fenv(0.5, 300, 0, 200) + amp(0.5, 600, 0, 400))
            .L(2, wave("Sine", -1) + open() + amp(0.5, 500, 0, 300) + lvl(-8))),
        R("Marimba Orbit", "PLUCK", N().rev(1.2, 15)
            .L(1, fm("4", 20, 80) + flt("LP 12", 10000) + amp(0.5, 500, 0, 300, 60))
            .L(2, wave("Sine") + open() + amp(0.5, 700, 0, 400, 60) + lvl(-6))),
        R("Harp Light", "PLUCK", N().cho(0.3, 20, 20).rev(2.5, 30)
            .L(1, wave("Triangle") + flt("LP 12", 6000, 0, 30) + fenv(0.5, 400, 0, 300) + amp(0.5, 1500, 0, 1200, 60))
            .L(2, fm("2", 10, 150) + open() + amp(0.5, 800, 0, 600) + lvl(-10))),
        R("Music Box", "PLUCK", N().dly("1/4", 20, 12).rev(2, 30)
            .L(1, wave("Sine", 1) + open() + amp(0.5, 1200, 0, 800))
            .L(2, fm("5", 15, 100, 0, 1) + open() + amp(0.5, 600, 0, 500) + lvl(-8))),
        R("Steel Drop", "PLUCK", N().rev(1.6, 22)
            .L(1, fm("1.5", 25, 200) + open() + amp(0.5, 900, 0, 500))
            .L(2, wave("Sine") + open() + amp(0.5, 1000, 0, 500) + lvl(-6))),
        R("Koto Wind", "PLUCK", N().rev(1.8, 20)
            .L(1, saw() + flt("LP 24", 3000, 15, 50) + fenv(0.5, 250, 0, 200) + amp(0.5, 1200, 0, 600, 70))
            .L(2, smp("Click") + open() + amp(0.5, 100, 0, 100) + lvl(-16))),
        R("Water Drop", "PLUCK", N().mod(1, "Env 2", "Pitch", -30).dly("1/8 D", 40, 30).rev(2.5, 30)
            .L(1, wave("Sine") + open() + fenv(0.5, 60, 0, 50) + amp(0.5, 300, 0, 200))),
        R("Lo-Fi Pluck", "PLUCK", N().drv(20, 30, 60).eq(0, 0, -5).rev(1, 15)
            .L(1, wave("Triangle", 0, 2, 10, 50) + flt("LP 12", 2500, 0, 0, 20) + amp(0.5, 600, 0, 400))
            .L(2, smp("Static") + open() + amp(0.5, 600, 0, 400) + lvl(-22))),
        R("Glass Mallet", "PLUCK", N().dly("1/4 D", 25, 15).rev(2.2, 25)
            .L(1, wt("Glass", 90) + open() + amp(0.5, 800, 0, 600))
            .L(2, fm("9", 15, 60) + open() + amp(0.5, 300, 0, 300) + lvl(-12))),
        R("Short Stab", "PLUCK", N().dly("1/8 D", 30, 15).rev(1.5, 20)
            .L(1, saw(0, 6, 25, 90) + flt("LP 24", 2500, 10, 30) + fenv(0.5, 150, 0, 120) + amp(0.5, 250, 0, 200))
            .L(2, pulse(50, -1) + flt("LP 24", 2500, 10, 30) + fenv(0.5, 150, 0, 120) + amp(0.5, 250, 0, 200) + lvl(-8))),
        R("Orbit Pluck", "PLUCK", N().lfoSync(1, "Orbit", "1/4", false, 70).mod(1, "LFO 1", "WT position", 40).dly("1/8 D", 35, 22).rev(1.8, 20)
            .L(1, wt("Bright", 50, 0, 3, 15, 60) + flt("LP 12", 8000) + amp(0.5, 700, 0, 500))),
        R("Sync Pluck", "PLUCK", N().mod(1, "Env 2", "WT position", 60).dly("1/16", 30, 15).rev(1.2, 15)   // bright at the pluck, settling to the soft end
            .L(1, wt("Sync", 20) + flt("LP 12", 12000) + fenv(0.5, 200, 0, 150) + amp(0.5, 600, 0, 400))),
        R("Bell Pluck", "PLUCK", N().rev(3, 30)
            .L(1, fm("3", 30, 400) + open() + amp(0.5, 1800, 0, 1500))
            .L(2, fm("7", 10, 200) + open() + amp(0.5, 1000, 0, 800) + lvl(-12))),
        R("Nylon Moon", "PLUCK", N().rev(1.4, 15)
            .L(1, wave("Triangle") + flt("LP 12", 1800, 0, 40) + fenv(0.5, 300, 0, 200) + amp(0.5, 1800, 0, 700, 70))
            .L(2, saw() + flt("LP 12", 1200) + amp(0.5, 600, 0, 400) + lvl(-16))
            .L(3, smp("Knock", 2) + open() + amp(0.5, 80, 0, 80) + lvl(-24))),

        // ================= KEYS
        R("Tine Piano", "KEYS", N().mod(1, "Velocity", "FM index", 45).lfo(1, "Triangle", 4.0).mod(2, "LFO 1", "Pan", 40).drv(15, 70, 50).rev(1.5, 15)   // the bright, bell-edged EP (Velvet Keys is the soft one)
            .L(1, fm("1", 40, 1200, 5) + open() + amp(1, 3500, 15, 600, 80))
            .L(2, fm("14", 15, 60) + open() + amp(0.5, 200, 0, 150, 80) + lvl(-10))
            .L(3, wave("Sine") + open() + amp(1, 3000, 20, 600) + lvl(-12))),
        R("Bark EP", "KEYS", N().mod(1, "Velocity", "Drive", 40).lfo(1, "Triangle", 5.5).mod(2, "LFO 1", "Level", 15).drv(20, 50, 50).rev(1.2, 12)
            .L(1, wave("Triangle") + flt("LP 12", 3000, 0, 30, 40) + fenv(0.5, 400, 20, 300) + amp(1, 2500, 30, 400, 80))),
        R("Funk Keys", "KEYS", N().drv(15)
            .L(1, pulse(20) + flt("BP 12", 1500, 30, 50) + fenv(0.5, 180, 0, 120) + amp(0.5, 600, 0, 120, 80))
            .L(2, pulse(50, 1) + flt("HP 12", 800) + amp(0.5, 300, 0, 100) + lvl(-10))),
        R("Jazz Organ", "KEYS", N().cho(1.2, 50, 40).drv(15, 60, 40).rev(1.2, 15)
            .L(1, wt("Organ", 50) + open() + amp(2, 50, 100, 50, 0))
            .L(2, wave("Sine", 1) + open() + amp(1, 300, 0, 50, 0) + lvl(-12))
            .L(3, smp("Click") + open() + amp(0.5, 30, 0, 30, 0) + lvl(-22))),
        R("Church Organ", "KEYS", N().rev(6, 40)
            .L(1, wt("Organ", 100, 0, 2, 5, 60) + open() + amp(8, 100, 100, 600, 0))
            .L(2, wt("Organ", 60, -1) + open() + amp(8, 100, 100, 600, 0) + lvl(-6))),
        R("Vibra Moon", "KEYS", N().lfo(1, "Triangle", 5.0).mod(1, "LFO 1", "Level", 25).rev(2, 20)
            .L(1, fm("4", 15, 800) + open() + amp(0.5, 3000, 0, 1500))
            .L(2, wave("Sine") + open() + amp(0.5, 3000, 0, 1500) + lvl(-4))),
        R("Celesta Dust", "KEYS", N().dly("1/8 D", 20, 12).rev(2.5, 30)
            .L(1, fm("4", 10, 200, 0, 1) + open() + amp(0.5, 1500, 0, 800))
            .L(2, wave("Sine", 1) + open() + amp(0.5, 1500, 0, 800) + lvl(-4))),
        R("Harpsichord Comet", "KEYS", N().rev(1.5, 20)
            .L(1, pulse(15) + flt("HP 12", 300) + amp(0.5, 1200, 0, 300, 30))
            .L(2, saw(1) + flt("LP 12", 8000) + amp(0.5, 1200, 0, 300, 30) + lvl(-6))),
        R("Toy Piano", "KEYS", N().rev(1, 15)
            .L(1, fm("5", 20, 100) + open() + amp(0.5, 1000, 0, 400))
            .L(2, wave("Triangle") + open() + amp(0.5, 800, 0, 400) + lvl(-8))),
        R("Grand Synth", "KEYS", N().rev(2, 20)
            .L(1, fm("1", 25, 600, 0, 0, 2, 4, 40) + open() + amp(1, 4000, 0, 800, 80))
            .L(2, saw(0, 2, 6, 40) + flt("LP 24", 2500, 0, 40) + fenv(0.5, 800, 0, 500) + amp(1, 4000, 0, 800, 80) + lvl(-10))
            .L(3, smp("Knock", 2) + open() + amp(0.5, 100, 0, 100) + lvl(-26))),
        R("Glass EP", "KEYS", N().lfo(1, "Triangle", 3.0).mod(1, "LFO 1", "Pan", 30).cho(0.3, 30, 25).rev(2, 20)
            .L(1, wt("Glass", 50) + open() + amp(1, 3000, 20, 600, 70))
            .L(2, fm("1", 15, 1200) + open() + amp(1, 3000, 20, 600, 70) + lvl(-6))),
        R("Choir Keys", "KEYS", N().cho(0.3, 30, 25).rev(3, 30)
            .L(1, wt("Formant", 60, 0, 3, 10, 60) + flt("LP 12", 5000) + amp(5, 1500, 60, 800))
            .L(2, fm("2", 10, 300) + open() + amp(1, 400, 0, 300) + lvl(-12))),
        R("Stage Organ Drive", "KEYS", N().mod(1, "Mod wheel", "Drive", 40).drv(45, 60).cho(0.8, 50, 35).rev(1, 10)
            .L(1, wt("Organ", 85) + flt("LP 12", 20000, 0, 0, 30) + amp(2, 50, 100, 60, 0))),
        R("Felt Keys", "KEYS", N().eq(0, 0, -3).rev(1.4, 18)
            .L(1, wave("Triangle") + flt("LP 12", 1200, 0, 20) + fenv(1, 600, 0, 300) + amp(2, 2500, 0, 600, 70))
            .L(2, wave("Sine", 1) + open() + amp(2, 1500, 0, 500, 70) + lvl(-14))),

        // ================= SEQ
        // the trance gate itself (2026-10-10; before, a square LFO on the level): a held chord chopped on the 1/16, on the host's beat
        R("Trance Gate", "SEQ", N().gate("1/16", 100, {1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1}).dly("1/8 D", 30, 20).rev(2, 20)
            .L(1, saw(0, 8, 25, 100) + flt("LP 24", 3000, 10) + amp(1, 300, 100, 150))),
        // the arpeggiator in the order played, a little swing, octave jumps on the pitch row and accents on the velocity row (the velocity
        // opens the filter) (2026-10-10)
        R("Acid Seq", "SEQ", N().mod(1, "Velocity", "Cutoff", 25).lfoSync(1, "Triangle", "2 bars").mod(2, "LFO 1", "Cutoff", 30).drv(30, 60).dly("1/16", 30, 15)
            .arp("Order", "1/16", 1, 60, 15)
            .arpVel({100, 55, 70, 100, 55, 70, 100, 55, 100, 55, 70, 55, 100, 70, 55, 85})
            .arpPitch({0, 0, 12, 0, 0, -12, 0, 12, 0, 0, 7, 0, 12, 0, -12, 0})
            .L(1, saw() + flt("LP 24", 500, 75, 70, 40) + fenv(0.5, 150, 0, 100) + amp(1, 200, 60, 100, 70))),
        R("FM Ticker", "SEQ", N().dly("1/8 D", 35, 22).rev(1.2, 12)
            .L(1, fm("7", 30, 60) + open() + amp(0.5, 120, 0, 80))
            .L(2, wave("Sine", -1) + open() + amp(0.5, 150, 0, 80) + lvl(-8))),
        R("Pump Chords", "SEQ", N().lfoSync(1, "Saw", "1/4", true).mod(1, "LFO 1", "Level", 100).rev(1.5, 15)
            .L(1, saw(0, 6, 20, 90) + flt("LP 24", 2500) + amp(1, 300, 100, 200))
            .L(2, pulse(50, -1) + flt("LP 24", 1500) + amp(1, 300, 100, 200) + lvl(-8))),
        // the arpeggiator up and down, the pitch row bouncing an octave every fourth step (2026-10-10)
        R("Bounce Seq", "SEQ", N().dly("1/16", 20, 10).arp("Up-Down", "1/16", 1, 55).arpPitch({0, 0, 12, 0, 0, 0, 12, 0, 0, 7, 12, 0, 0, 0, 12, 7})
            .L(1, saw() + flt("LP 24", 800, 40, 60) + fenv(0.5, 120, 0, 80) + amp(0.5, 180, 0, 80))
            .L(2, wave("Sine", -1) + open() + amp(0.5, 200, 0, 80) + lvl(-4))),
        // the arpeggiator at random over two octaves, rests on the velocity row (2026-10-10)
        R("Glass Steps", "SEQ", N().lfoSync(1, "Random", "1/16").mod(1, "LFO 1", "WT position", 40).dly("1/8 D", 40, 25).rev(2, 20)
            .arp("Random", "1/16", 2, 60).arpVel({100, 0, 70, 85, 100, 60, 0, 80, 100, 0, 75, 60, 100, 70, 0, 85})
            .L(1, wt("Glass", 70, 1) + open() + amp(0.5, 250, 0, 150))),
        R("Formant Seq", "SEQ", N().lfoSync(1, "Square", "1/8").mod(1, "LFO 1", "WT position", 40).dly("1/8", 30, 15)
            .L(1, wt("Formant", 50, 0, 2, 10, 50) + flt("LP 12", 5000) + amp(1, 200, 80, 120))),
        R("Echo Orbit", "SEQ", N().lfoSync(1, "Orbit", "1 bar", false, 70).mod(1, "LFO 1", "Pan", 60).dly("1/8 D", 60, 35).rev(2.5, 25)
            .L(1, pulse(40) + flt("LP 24", 2000, 30, 40) + fenv(0.5, 150, 0, 100) + amp(0.5, 150, 0, 100))),
        R("Digital Rain Seq", "SEQ", N().lfoSync(1, "Random", "1/16").mod(1, "LFO 1", "Pan", 80).dly("1/16", 40, 25).rev(1.5, 20)
            .L(1, smp("Tick") + open() + amp(0.5, 200, 0, 80))
            .L(2, fm("11", 20, 80) + open() + amp(0.5, 250, 20, 80) + lvl(-6))
            .L(3, wave("Sine", 1) + open() + amp(0.5, 250, 20, 80) + lvl(-10))),
        // the trance gate on the 1/32, speeding up into the half bar (2026-10-10; before, a square LFO on the level)
        R("Stutter Saw", "SEQ", N().gate("1/32", 100, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1}).rev(1.2, 12)
            .L(1, saw(0, 4, 15, 70) + flt("LP 24", 4000) + amp(1, 200, 100, 80))),
        // the arpeggiator up over two octaves, an accent on every beat (2026-10-10)
        R("Arp Pulse", "SEQ", N().dly("1/16", 35, 20).rev(1, 12).arp("Up", "1/16", 2, 50).arpVel({100, 55, 75, 55, 100, 55, 75, 55, 100, 55, 75, 55, 100, 55, 75, 55})
            .L(1, pulse(30, 0, 2, 8, 40) + flt("LP 24", 1500, 25, 50) + fenv(0.5, 120, 20, 60) + amp(0.5, 200, 30, 60))),
        R("Sidechain Bass", "SEQ", N().lfoSync(1, "Saw", "1/4", true).mod(1, "LFO 1", "Level", 100)
            .L(1, saw(0, 2, 10, 30) + flt("LP 24", 600, 20) + amp(1, 300, 100, 100))
            .L(2, wave("Sine", -1) + open() + amp(1, 300, 100, 100) + lvl(-4))),
        R("Click Grid", "SEQ", N().dly("1/16", 25, 15)
            .L(1, smp("Click") + open() + amp(0.5, 100, 0, 60))
            .L(2, smp("Knock", 1) + open() + amp(0.5, 150, 0, 80) + lvl(-6))
            .L(3, wave("Sine", -1) + open() + amp(0.5, 250, 30, 80) + lvl(-4))),
        R("Swing Chop", "SEQ", N().lfoSync(1, "Square", "1/8 T", true).mod(1, "LFO 1", "Level", 100).dly("1/8 D", 30, 18).rev(1.6, 18)
            .L(1, wt("Classic", 70, 0, 4, 20, 80) + flt("LP 24", 3500, 15) + amp(1, 250, 100, 150))),

        // ================= FX
        R("Riser Noise", "FX", N().mod(1, "Env 2", "Pitch", 10).dly("1/8 D", 40, 20).rev(4, 40)
            .L(1, smp("Air", 0, 2, 100) + flt("BP 12", 300, 40, 100) + fenv(4000, 10, 100, 1000) + amp(2000, 100, 100, 1500))),
        R("Downlifter", "FX", N().mod(1, "Env 2", "Pitch", 30).mod(2, "Env 2", "Cutoff", 30).rev(4, 40)
            .L(1, smp("Air", 0, 2, 100) + flt("BP 12", 2000, 30) + fenv(0.5, 3000, 0, 1000) + amp(5, 3000, 0, 1000))
            .L(2, saw(0, 4, 40, 100) + flt("LP 24", 3000, 10) + fenv(0.5, 3000, 0, 1000) + amp(5, 3000, 0, 1000) + lvl(-10))),
        R("Impact Moon", "FX", N().mod(1, "Env 2", "Pitch", 60).drv(30, 50).rev(6, 35)
            .L(1, smp("Knock", -2) + open() + fenv(0.5, 300, 0, 200) + amp(0.5, 2000, 0, 1500))   // Env 2 drives the pitch: every layer falls
            .L(2, wave("Sine", -1) + open() + fenv(0.5, 200, 0, 200) + amp(0.5, 1500, 0, 1000))
            .L(3, smp("Static") + open() + fenv(0.5, 300, 0, 200) + amp(0.5, 500, 0, 400) + lvl(-14))),
        R("Wind Tunnel", "FX", N().lfo(1, "Random", 0.4).mod(1, "LFO 1", "Cutoff", 40).rev(5, 40)
            .L(1, smp("Air", 0, 2, 100) + flt("BP 12", 1200, 30) + amp(800, 1000, 100, 2000))
            .L(2, smp("Breath") + flt("LP 12", 3000) + amp(800, 1000, 100, 2000) + lvl(-8))),
        R("Siren Orbit", "FX", N().lfo(1, "Orbit", 0.5, 60).mod(1, "LFO 1", "Pitch", 50).dly("1/4", 30, 20).rev(3, 30)
            .L(1, saw(0, 2, 10, 60) + flt("LP 24", 3000) + amp(10, 1000, 100, 600))),
        R("Laser Zap", "FX", N().mod(1, "Env 2", "Pitch", 100).dly("1/8 D", 60, 35).rev(2, 25)
            .L(1, fm("3", 40, 150) + open() + fenv(0.5, 250, 0, 100) + amp(0.5, 700, 0, 300))),
        R("Underwater", "FX", N().lfo(1, "Triangle", 0.3).mod(1, "LFO 1", "WT position", 50).lfo(2, "Random", 3.0).mod(2, "LFO 2", "Cutoff", 20)
            .cho(0.15, 60, 15).rev(6, 35)
            .L(1, wt("Formant", 30, 0, 4, 20, 40) + flt("LP 24", 600, 50) + amp(500, 1000, 100, 2000))
            .L(2, smp("Rain") + flt("LP 12", 800) + amp(500, 1000, 100, 2000) + lvl(-10))),
        R("Glitch Storm", "FX", N().lfoSync(1, "Random", "1/16").mod(1, "LFO 1", "Pitch", 40).lfoSync(2, "Square", "1/16 T").mod(2, "LFO 2", "Level", 100)
            .drv(30, 60).dly("1/16", 40, 25)
            .L(1, wt("Sync", 50, 0, 2, 10, 60) + open() + amp(1, 1000, 100, 400))),
        R("Space Drone", "FX", N().lfo(1, "Orbit", 0.08, 80).mod(1, "LFO 1", "Gravity", 100).lfo(2, "Triangle", 0.05).mod(2, "LFO 2", "WT position", 40)
            .dly("1/2", 50, 20).rev(12, 55)
            .L(1, saw(-1, 8, 50, 100) + grav(0) + flt("LP 24", 1500, 30) + amp(3000, 1000, 100, 5000))
            .L(2, wt("Bright", 40, 0, 4, 20, 100) + flt("LP 24", 1500, 30) + amp(3000, 1000, 100, 5000) + lvl(-10))),
        R("Scanner Beam", "FX", N().lfo(1, "Saw", 0.7).mod(1, "LFO 1", "WT position", 100).lfo(2, "Triangle", 0.2).mod(2, "LFO 2", "Pan", 80)
            .dly("1/8 D", 40, 25).rev(2.5, 25)
            .L(1, wt("Bright", 0) + flt("BP 12", 2000, 50) + amp(50, 1000, 100, 800))),
        R("Comms Beep", "FX", N().lfo(1, "Square", 6.0).mod(1, "LFO 1", "Level", 100).dly("1/8", 30, 15).rev(1, 12)
            .L(1, wave("Sine", 2) + open() + amp(1, 1000, 100, 200))
            .L(2, smp("Static") + open() + amp(1, 1000, 100, 200) + lvl(-20))),
        R("Flyby Jet", "FX", N().fly("Pass", 100, 3, 80).rev(4, 30)
            .L(1, smp("Air", 0, 2, 100) + flt("LP 12", 8000) + amp(20, 1000, 100, 1500))
            .L(2, saw(0, 8, 60, 100) + flt("LP 24", 2000) + amp(20, 1000, 100, 1500) + lvl(-6))),
        R("Engine Room", "FX", N().lfo(1, "Random", 1.5).mod(1, "LFO 1", "Pitch", 2).drv(35, 40).rev(3, 25)   // a low mechanical rumble
            .L(1, saw(-2, 4, 10, 60) + flt("LP 24", 400, 30, 0, 40) + amp(200, 1000, 100, 1500))
            .L(2, smp("Grit", -1) + flt("LP 24", 1200) + amp(200, 1000, 100, 1500) + lvl(-8))),
        R("Cosmic Bell Swell", "FX", N().lfo(1, "Orbit", 0.2, 50).mod(1, "LFO 1", "Pan", 25).dly("1/4 D", 45, 22).rev(8, 35)
            .L(1, fm("3", 20) + open() + amp(2500, 1000, 100, 2000))
            .L(2, wt("Glass", 70, 0, 4, 10, 70) + open() + amp(3000, 1000, 100, 2500) + lvl(-8))),
    };
    return t;
}

}  // namespace sw::in07::dsl
