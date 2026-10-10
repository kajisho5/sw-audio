#include "in07/presets.hpp"
#include "in07/preset_dsl.hpp"
#include "sw/loudness.hpp"
#include <algorithm>
#include <map>
#include <string_view>

namespace sw::in07 {

namespace {
using namespace dsl;

// sound design: values chosen to the categories' usual shapes (pads swell and ring out, plucks and keys start at once, basses are mono,
// sequences stay tight); the loudness is not set here (preset_levels.inc). FX left unnamed keep their defaults (Drive, Chorus, Delay, Reverb on).
const std::vector<Raw>& firstTable() {
    static const std::vector<Raw> t = {
        // ---- the PLAY screen's ten
        // a hard supersaw (the client asked for a heavier one, 2026-10-08): three saw stacks (x8, x8 an octave up, x6 an octave down), each
        // saturated before its filter, the filters open, no sine sub (it muddied the chords); Drive, then a presence EQ (lows down, highs up),
        // then a short delay and room; the limiter's gain set to shave the peaks (density)
        {"Anthem Supersaw", "LEAD", B()
            .g({{"fx.slot1", "Drive"}, {"fx.slot2", "EQ"}, {"fx.slot3", "Delay"}, {"fx.slot4", "Reverb"}, {"fx.slot5", "Chorus"}, {"fx.slot6", "Limit"},
                {"fx.drive.amount", 60}, {"fx.drive.tone", 100}, {"fx.drive.mix", 100}, {"fx.chorus.on", 0},
                {"fx.eq.on", 1}, {"fx.eq.low", -4}, {"fx.eq.mid", 2}, {"fx.eq.high", 6},
                {"fx.delay.time", "1/8 D"}, {"fx.delay.feedback", 25}, {"fx.delay.mix", 10}, {"fx.reverb.size", 1.6}, {"fx.reverb.damp", 50}, {"fx.reverb.mix", 12},
                {"fx.limit.gain", 4}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Classic"}, {"wt.pos", 0}, {"osc.unison", 8}, {"osc.detune", 50}, {"osc.spread", 100},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 16000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 50}, {"flt.key", 0},
                   {"amp.a", 1}, {"amp.d", 300}, {"amp.s", 100}, {"amp.r", 300}})
            .l(2, {{"on", 1}, {"osc.wave", "Saw"}, {"osc.octave", 1}, {"osc.unison", 8}, {"osc.detune", 45}, {"osc.spread", 100}, {"level", -3},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 18000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 35}, {"flt.key", 0},
                   {"amp.a", 1}, {"amp.d", 300}, {"amp.s", 100}, {"amp.r", 300}})
            .l(3, {{"on", 1}, {"osc.wave", "Saw"}, {"osc.octave", -1}, {"osc.unison", 6}, {"osc.detune", 25}, {"osc.spread", 60}, {"level", -6},
                   {"flt.type", "LP 24"}, {"flt.cutoff", 3000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 40}, {"flt.key", 0},
                   {"amp.a", 1}, {"amp.d", 300}, {"amp.s", 100}, {"amp.r", 300}})
            .l(4, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Air"}, {"osc.octave", 0}, {"level", -16}, {"flt.type", "HP 12"}, {"flt.cutoff", 5000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.s", 100}, {"amp.r", 300}})},
        {"Hoover Stab", "LEAD", B()
            .g({{"glide", 60}, {"fx.drive.on", 0}, {"fx.chorus.rate", 0.6}, {"fx.chorus.depth", 70}, {"fx.chorus.mix", 50}, {"fx.delay.on", 0}, {"fx.reverb.size", 1.8}, {"fx.reverb.mix", 20},
                {"mod1.src", "Env 2"}, {"mod1.dst", "Pitch"}, {"mod1.amount", -10}})   // the dive: starts 1.2 semitones low and rises with the filter envelope
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Pulse"}, {"wt.pos", 60}, {"osc.unison", 6}, {"osc.detune", 45}, {"osc.spread", 80},
                   {"flt.cutoff", 5200}, {"flt.res", 45}, {"flt.drive", 55}, {"flt.env", 30}, {"fenv.a", 0.5}, {"fenv.d", 250}, {"fenv.s", 0}, {"fenv.r", 200},
                   {"amp.a", 2}, {"amp.d", 300}, {"amp.s", 80}, {"amp.r", 180}})
            .l(2, {{"on", 1}, {"osc.wave", "Square"}, {"osc.pw", 30}, {"osc.octave", -1}, {"level", -6}, {"flt.cutoff", 3000}, {"flt.res", 10}, {"flt.env", 0},
                   {"fenv.a", 0.5}, {"fenv.d", 250}, {"fenv.s", 0}, {"amp.a", 2}, {"amp.s", 80}, {"amp.r", 180}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Grit"}, {"osc.octave", 0}, {"level", -20}, {"flt.type", "BP 12"}, {"flt.cutoff", 2500}, {"flt.res", 20}, {"flt.env", 0},
                   {"fenv.a", 0.5}, {"fenv.d", 250}, {"fenv.s", 0}, {"amp.a", 2}, {"amp.s", 80}, {"amp.r", 180}})},
        {"Detuned Lead", "LEAD", B()
            .g({{"mode", "Legato"}, {"glide", 80}, {"fx.drive.on", 0}, {"fx.chorus.mix", 20}, {"fx.delay.time", "1/8 D"}, {"fx.delay.mix", 24}, {"fx.reverb.size", 1.6}, {"fx.reverb.mix", 18},
                {"lfo1.shape", "Triangle"}, {"lfo1.rate", 5.5}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Pitch"}, {"mod1.amount", 1.5},
                {"mod2.src", "Mod wheel"}, {"mod2.dst", "Cutoff"}, {"mod2.amount", 30}})
            .l(1, {{"osc.wave", "Saw"}, {"osc.unison", 4}, {"osc.detune", 40}, {"osc.spread", 70}, {"flt.cutoff", 3600}, {"flt.res", 25}, {"flt.env", 25}, {"flt.drive", 25},
                   {"amp.a", 4}, {"amp.d", 500}, {"amp.s", 85}, {"amp.r", 260}})
            .l(2, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", 1}, {"level", -12}, {"flt.cutoff", 20000}, {"flt.drive", 0}, {"flt.env", 0}, {"amp.s", 90}, {"amp.r", 260}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Breath"}, {"osc.octave", 0}, {"level", -22}, {"flt.type", "BP 12"}, {"flt.cutoff", 2400}, {"flt.res", 0}, {"flt.env", 0}, {"amp.s", 90}, {"amp.r", 260}})
            .l(4, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "2"}, {"fm.index", 20}, {"fm.decay", 1500}, {"level", -20}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2}, {"amp.d", 600}, {"amp.s", 40}, {"amp.r", 260}})},
        {"Glass Horizon", "PAD", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 40}, {"fx.delay.time", "1/4 D"}, {"fx.delay.mix", 18}, {"fx.reverb.size", 6}, {"fx.reverb.damp", 30}, {"fx.reverb.mix", 45},
                {"lfo2.shape", "Orbit"}, {"lfo2.rate", 0.07}, {"lfo2.orbit", 50}, {"mod1.src", "LFO 2"}, {"mod1.dst", "WT position"}, {"mod1.amount", 25}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Formant"}, {"wt.pos", 30}, {"osc.unison", 6}, {"osc.detune", 25}, {"osc.spread", 100},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 1400}, {"flt.res", 20}, {"flt.env", 10}, {"flt.drive", 5}, {"fenv.a", 900}, {"fenv.d", 2000}, {"fenv.s", 50},
                   {"amp.a", 900}, {"amp.d", 1500}, {"amp.s", 85}, {"amp.r", 2600}})
            .l(2, {{"on", 1}, {"osc.wave", "Saw"}, {"osc.octave", 0}, {"osc.unison", 4}, {"osc.detune", 30}, {"level", -6}, {"flt.cutoff", 1200}, {"flt.res", 10}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1200}, {"amp.d", 1500}, {"amp.s", 85}, {"amp.r", 2600}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Rain"}, {"osc.octave", 0}, {"level", -22}, {"flt.type", "HP 12"}, {"flt.cutoff", 1500}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1500}, {"amp.s", 100}, {"amp.r", 2600}})
            .l(4, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "4"}, {"fm.index", 20}, {"fm.decay", 1200}, {"osc.octave", 1}, {"level", -16}, {"flt.cutoff", 10000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 5}, {"amp.d", 1500}, {"amp.s", 0}, {"amp.r", 2000}})},
        {"Solar Wind", "PAD", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 30}, {"fx.delay.on", 0}, {"fx.reverb.size", 8}, {"fx.reverb.damp", 45}, {"fx.reverb.mix", 50},
                {"lfo1.shape", "Orbit"}, {"lfo1.rate", 0.15}, {"lfo1.orbit", 70}, {"mod1.src", "LFO 1"}, {"mod1.dst", "WT position"}, {"mod1.amount", 40},
                {"lfo2.shape", "Triangle"}, {"lfo2.rate", 0.11}, {"mod2.src", "LFO 2"}, {"mod2.dst", "Cutoff"}, {"mod2.amount", 15}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Bright"}, {"wt.pos", 30}, {"osc.unison", 8}, {"osc.detune", 45}, {"osc.spread", 100}, {"osc.gravity", 50},
                   {"flt.cutoff", 900}, {"flt.res", 55}, {"flt.env", 10}, {"flt.drive", 10}, {"fenv.a", 1500}, {"fenv.d", 3000}, {"fenv.s", 40},
                   {"amp.a", 1500}, {"amp.d", 2000}, {"amp.s", 90}, {"amp.r", 3500}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Air"}, {"osc.octave", 0}, {"level", -10}, {"flt.type", "BP 12"}, {"flt.cutoff", 2500}, {"flt.res", 15}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2000}, {"amp.s", 100}, {"amp.r", 3500}})
            .l(3, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -1}, {"level", -10}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1500}, {"amp.s", 100}, {"amp.r", 3500}})
            .l(4, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "1.5"}, {"fm.index", 15}, {"osc.octave", 1}, {"level", -18}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2500}, {"amp.s", 100}, {"amp.r", 3500}})},
        {"Night Pulse", "SEQ", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.time", "1/8 D"}, {"fx.delay.feedback", 45}, {"fx.delay.mix", 30}, {"fx.reverb.mix", 20},
                {"lfo1.shape", "Orbit"}, {"lfo1.sync", "1 bar"}, {"lfo1.orbit", 40}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Cutoff"}, {"mod1.amount", 20}})
            .l(1, {{"osc.wave", "Square"}, {"osc.pw", 35}, {"flt.cutoff", 1800}, {"flt.res", 60}, {"flt.env", 50}, {"flt.drive", 30}, {"fenv.a", 0.5}, {"fenv.d", 180}, {"fenv.s", 0}, {"fenv.r", 120},
                   {"amp.a", 1}, {"amp.d", 250}, {"amp.s", 0}, {"amp.r", 120}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Tick"}, {"osc.octave", 0}, {"level", -14}, {"flt.cutoff", 20000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 150}, {"amp.s", 0}, {"amp.r", 100}})
            .l(3, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Formant"}, {"wt.pos", 50}, {"osc.octave", 0}, {"level", -14}, {"flt.cutoff", 3000}, {"flt.res", 10}, {"flt.env", 30},
                   {"fenv.d", 200}, {"fenv.s", 0}, {"amp.a", 1}, {"amp.d", 250}, {"amp.s", 0}, {"amp.r", 120}})
            .l(4, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "4"}, {"fm.index", 20}, {"fm.decay", 150}, {"osc.octave", 1}, {"level", -20}, {"flt.cutoff", 10000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 300}, {"amp.s", 0}, {"amp.r", 150}})},
        {"Polar Bass", "BASS", B()
            .g({{"mode", "Mono"}, {"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.on", 0}})
            .l(1, {{"osc.wave", "Square"}, {"flt.cutoff", 300}, {"flt.res", 40}, {"flt.env", 60}, {"flt.drive", 45}, {"flt.key", 60}, {"fenv.a", 0.5}, {"fenv.d", 250}, {"fenv.s", 10}, {"fenv.r", 90},
                   {"amp.a", 1}, {"amp.d", 400}, {"amp.s", 80}, {"amp.r", 90}, {"vel", 40}})
            .l(2, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Fold"}, {"wt.pos", 40}, {"osc.octave", 0}, {"level", -8}, {"flt.type", "LP 12"}, {"flt.cutoff", 800}, {"flt.res", 10}, {"flt.env", 30},
                   {"fenv.a", 0.5}, {"fenv.d", 250}, {"fenv.s", 10}, {"amp.a", 1}, {"amp.s", 80}, {"amp.r", 90}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Knock"}, {"osc.octave", 1}, {"level", -16}, {"flt.cutoff", 6000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 200}, {"amp.s", 0}, {"amp.r", 90}})},
        {"Quiet Comet", "PLUCK", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 25}, {"fx.delay.time", "1/8 D"}, {"fx.delay.mix", 24}, {"fx.reverb.size", 2.5}, {"fx.reverb.mix", 30}})
            .l(1, {{"osc.type", "FM"}, {"fm.ratio", "4"}, {"fm.index", 35}, {"fm.decay", 250}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 900}, {"amp.s", 0}, {"amp.r", 600}, {"vel", 60}})
            .l(2, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Bright"}, {"wt.pos", 40}, {"osc.octave", 0}, {"level", -6}, {"flt.cutoff", 3000}, {"flt.res", 35}, {"flt.env", 50},
                   {"fenv.a", 0.5}, {"fenv.d", 300}, {"fenv.s", 0}, {"amp.a", 1}, {"amp.d", 700}, {"amp.s", 0}, {"amp.r", 500}, {"vel", 60}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Click"}, {"osc.octave", 0}, {"level", -18}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 100}, {"amp.s", 0}, {"amp.r", 100}})
            .l(4, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", 0}, {"level", -14}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1}, {"amp.d", 1200}, {"amp.s", 0}, {"amp.r", 600}})},
        {"Velvet Keys", "KEYS", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.rate", 0.4}, {"fx.chorus.depth", 30}, {"fx.chorus.mix", 30}, {"fx.delay.on", 0}, {"fx.reverb.size", 1.8}, {"fx.reverb.mix", 18},
                {"mod1.src", "Velocity"}, {"mod1.dst", "FM index"}, {"mod1.amount", 30}})
            .l(1, {{"osc.type", "FM"}, {"fm.ratio", "1"}, {"fm.index", 15}, {"fm.decay", 900}, {"fm.feedback", 10}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 2500}, {"amp.s", 30}, {"amp.r", 900}, {"vel", 70}})
            .l(2, {{"on", 1}, {"osc.wave", "Triangle"}, {"osc.octave", 0}, {"level", -6}, {"flt.type", "LP 12"}, {"flt.cutoff", 2000}, {"flt.res", 0}, {"flt.env", 20}, {"flt.drive", 0},
                   {"amp.a", 2}, {"amp.d", 3000}, {"amp.s", 25}, {"amp.r", 900}, {"vel", 70}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Click"}, {"osc.octave", 0}, {"level", -22}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 60}, {"amp.s", 0}, {"amp.r", 60}})
            .l(4, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Glass"}, {"wt.pos", 20}, {"osc.octave", 1}, {"level", -20}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2}, {"amp.d", 2000}, {"amp.s", 20}, {"amp.r", 900}})},
        {"Radio Static", "FX", B()
            .g({{"fx.drive.amount", 40}, {"fx.chorus.on", 0}, {"fx.delay.time", "1/4"}, {"fx.delay.mix", 20}, {"fx.reverb.size", 1.8}, {"fx.reverb.mix", 30},
                {"lfo1.shape", "Random"}, {"lfo1.rate", 8}, {"mod1.src", "LFO 1"}, {"mod1.dst", "WT position"}, {"mod1.amount", 50},
                {"lfo2.shape", "Triangle"}, {"lfo2.rate", 0.3}, {"mod2.src", "LFO 2"}, {"mod2.dst", "Pitch"}, {"mod2.amount", 8}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Bright"}, {"wt.pos", 50}, {"flt.type", "BP 12"}, {"flt.cutoff", 2500}, {"flt.res", 70}, {"flt.env", 0}, {"flt.drive", 70},
                   {"amp.a", 20}, {"amp.s", 100}, {"amp.r", 1800}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Static"}, {"osc.octave", 0}, {"level", -4}, {"flt.type", "HP 12"}, {"flt.cutoff", 400}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 5}, {"amp.s", 100}, {"amp.r", 1800}})
            .l(3, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "11"}, {"fm.index", 20}, {"osc.octave", 1}, {"level", -18}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 300}, {"amp.s", 100}, {"amp.r", 1800}})
            .l(4, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -2}, {"level", -16}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 20}, {"amp.a", 50}, {"amp.s", 100}, {"amp.r", 1800}})},

        // ---- more leads
        {"Sync Runner", "LEAD", B()
            .g({{"mode", "Mono"}, {"glide", 40}, {"fx.drive.on", 0}, {"fx.chorus.mix", 20}, {"fx.delay.time", "1/16"}, {"fx.delay.mix", 15}, {"fx.reverb.mix", 18},
                {"mod1.src", "Env 2"}, {"mod1.dst", "WT position"}, {"mod1.amount", 60}, {"mod2.src", "Mod wheel"}, {"mod2.dst", "WT position"}, {"mod2.amount", 40}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Sync"}, {"wt.pos", 20}, {"osc.unison", 2}, {"osc.detune", 15}, {"osc.spread", 60},
                   {"flt.cutoff", 8000}, {"flt.res", 10}, {"flt.env", 0}, {"flt.drive", 20}, {"fenv.a", 0.5}, {"fenv.d", 400}, {"fenv.s", 20},
                   {"amp.a", 2}, {"amp.d", 400}, {"amp.s", 85}, {"amp.r", 200}})
            .l(2, {{"on", 1}, {"osc.wave", "Square"}, {"osc.octave", -1}, {"level", -10}, {"flt.cutoff", 2500}, {"flt.res", 0}, {"flt.env", 0}, {"amp.a", 2}, {"amp.s", 85}, {"amp.r", 200}})},
        {"Comet Tail", "LEAD", B()
            .g({{"flyby.mode", "Leave"}, {"flyby.depth", 70}, {"flyby.time", 1.2}, {"flyby.near", 50}, {"fx.drive.on", 0}, {"fx.delay.time", "1/4"}, {"fx.delay.feedback", 40}, {"fx.delay.mix", 25},
                {"fx.reverb.size", 3}, {"fx.reverb.mix", 30}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Bright"}, {"wt.pos", 60}, {"osc.unison", 3}, {"osc.detune", 20}, {"osc.spread", 70},
                   {"flt.cutoff", 5000}, {"flt.res", 20}, {"flt.env", 15}, {"flt.drive", 10}, {"amp.a", 4}, {"amp.d", 600}, {"amp.s", 80}, {"amp.r", 1500}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "2"}, {"fm.index", 25}, {"fm.decay", 800}, {"osc.octave", 0}, {"level", -10}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2}, {"amp.d", 800}, {"amp.s", 50}, {"amp.r", 1500}})},

        // ---- more pads
        {"Gravity Choir", "PAD", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 30}, {"fx.delay.on", 0}, {"fx.reverb.size", 6}, {"fx.reverb.mix", 45},
                {"lfo2.shape", "Triangle"}, {"lfo2.rate", 0.07}, {"mod1.src", "LFO 2"}, {"mod1.dst", "WT position"}, {"mod1.amount", 30}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Formant"}, {"wt.pos", 60}, {"osc.unison", 8}, {"osc.detune", 30}, {"osc.spread", 100}, {"osc.gravity", 40},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 3000}, {"flt.res", 10}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1200}, {"amp.d", 2000}, {"amp.s", 90}, {"amp.r", 3000}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Breath"}, {"osc.octave", 0}, {"level", -16}, {"flt.type", "BP 12"}, {"flt.cutoff", 1800}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1500}, {"amp.s", 100}, {"amp.r", 3000}})},
        {"Aurora", "PAD", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 35}, {"fx.delay.time", "1/4 D"}, {"fx.delay.mix", 15}, {"fx.reverb.size", 5}, {"fx.reverb.mix", 40},
                {"lfo1.shape", "Orbit"}, {"lfo1.rate", 0.2}, {"lfo1.orbit", 60}, {"mod1.src", "LFO 1"}, {"mod1.dst", "L2 level"}, {"mod1.amount", 60}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Organ"}, {"wt.pos", 40}, {"osc.unison", 4}, {"osc.detune", 15}, {"osc.spread", 70},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 4000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 600}, {"amp.d", 2000}, {"amp.s", 90}, {"amp.r", 2500}})
            .l(2, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Glass"}, {"wt.pos", 60}, {"osc.octave", 1}, {"osc.unison", 2}, {"osc.detune", 10}, {"level", -10},
                   {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1500}, {"amp.s", 100}, {"amp.r", 2500}})},
        {"Eclipse Drone", "PAD", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 25}, {"fx.delay.on", 0}, {"fx.reverb.size", 10}, {"fx.reverb.damp", 50}, {"fx.reverb.mix", 50},
                {"lfo1.shape", "Orbit"}, {"lfo1.rate", 0.05}, {"lfo1.orbit", 85}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Cutoff"}, {"mod1.amount", 40}})   // a long dark swing out, a brief bright pass
            .l(1, {{"osc.wave", "Saw"}, {"osc.octave", -1}, {"osc.unison", 6}, {"osc.detune", 40}, {"osc.spread", 100},
                   {"flt.cutoff", 500}, {"flt.res", 50}, {"flt.env", 0}, {"flt.drive", 20}, {"amp.a", 2500}, {"amp.d", 3000}, {"amp.s", 90}, {"amp.r", 4000}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Grit"}, {"osc.octave", 0}, {"level", -20}, {"flt.type", "BP 12"}, {"flt.cutoff", 1200}, {"flt.res", 30}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 3000}, {"amp.s", 100}, {"amp.r", 4000}})},

        // ---- more sequences
        {"Orbit Gate", "SEQ", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 25}, {"fx.delay.time", "1/8 D"}, {"fx.delay.mix", 20}, {"fx.reverb.mix", 25},
                {"lfo1.shape", "Orbit"}, {"lfo1.sync", "1/16"}, {"lfo1.orbit", 80}, {"lfo1.trigger", "Note"},
                {"mod1.src", "LFO 1"}, {"mod1.dst", "Level"}, {"mod1.amount", 100}})   // the moon's close pass on each sixteenth opens the gate
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Classic"}, {"wt.pos", 50}, {"osc.unison", 4}, {"osc.detune", 25}, {"osc.spread", 80},
                   {"flt.cutoff", 2500}, {"flt.res", 30}, {"flt.env", 20}, {"flt.drive", 15}, {"amp.a", 2}, {"amp.d", 300}, {"amp.s", 100}, {"amp.r", 250}})},
        {"Binary Star", "SEQ", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.time", "1/8"}, {"fx.delay.feedback", 50}, {"fx.delay.mix", 30}, {"fx.reverb.mix", 20}})
            .l(1, {{"osc.type", "FM"}, {"fm.ratio", "2"}, {"fm.index", 25}, {"fm.decay", 150}, {"pan", -60}, {"flt.cutoff", 10000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 220}, {"amp.s", 0}, {"amp.r", 150}})
            .l(2, {{"on", 1}, {"osc.wave", "Saw"}, {"osc.octave", 1}, {"pan", 60}, {"level", -4}, {"flt.cutoff", 2200}, {"flt.res", 30}, {"flt.env", 60},
                   {"fenv.a", 0.5}, {"fenv.d", 150}, {"fenv.s", 0}, {"amp.a", 1}, {"amp.d", 200}, {"amp.s", 0}, {"amp.r", 150}})},
        {"Tick Machine", "SEQ", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.time", "1/16"}, {"fx.delay.feedback", 40}, {"fx.delay.mix", 25}, {"fx.reverb.mix", 15}})
            .l(1, {{"osc.type", "Sample"}, {"smp.id", "Tick"}, {"flt.cutoff", 20000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 0.5}, {"amp.d", 150}, {"amp.s", 0}, {"amp.r", 100}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "9"}, {"fm.index", 40}, {"fm.decay", 40}, {"osc.octave", 0}, {"level", -8}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 120}, {"amp.s", 0}, {"amp.r", 100}})
            .l(3, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -1}, {"level", -6}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 10}, {"amp.a", 0.5}, {"amp.d", 200}, {"amp.s", 0}, {"amp.r", 100}})},

        // ---- more basses
        {"Sub Orbit", "BASS", B()
            .g({{"mode", "Legato"}, {"glide", 60}, {"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.on", 0}, {"fx.eq.on", 1}, {"fx.eq.low", 2}})
            .l(1, {{"osc.wave", "Sine"}, {"flt.cutoff", 20000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 25}, {"amp.a", 2}, {"amp.d", 300}, {"amp.s", 90}, {"amp.r", 120}, {"vel", 30}})
            .l(2, {{"on", 1}, {"osc.wave", "Triangle"}, {"osc.octave", 1}, {"level", -14}, {"flt.cutoff", 4000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 2}, {"amp.s", 90}, {"amp.r", 120}})},
        {"Drift Bass", "BASS", B()
            .g({{"mode", "Mono"}, {"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.mix", 8},
                {"lfo1.shape", "Orbit"}, {"lfo1.rate", 0.3}, {"lfo1.orbit", 50}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Gravity"}, {"mod1.amount", 60}})   // the pair beats, then locks, then drifts apart
            .l(1, {{"osc.wave", "Saw"}, {"osc.unison", 2}, {"osc.detune", 18}, {"osc.spread", 40}, {"osc.gravity", 30},
                   {"flt.cutoff", 600}, {"flt.res", 25}, {"flt.env", 30}, {"flt.drive", 30}, {"fenv.a", 0.5}, {"fenv.d", 400}, {"fenv.s", 30},
                   {"amp.a", 2}, {"amp.d", 500}, {"amp.s", 90}, {"amp.r", 120}})
            .l(2, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -1}, {"level", -6}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 2}, {"amp.s", 90}, {"amp.r", 120}})},
        {"Moon Slap", "BASS", B()
            .g({{"mode", "Mono"}, {"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.on", 0},
                {"mod1.src", "Velocity"}, {"mod1.dst", "FM index"}, {"mod1.amount", 20}})
            .l(1, {{"osc.type", "FM"}, {"fm.ratio", "1"}, {"fm.index", 45}, {"fm.decay", 180}, {"fm.feedback", 20}, {"flt.type", "LP 12"}, {"flt.cutoff", 3000}, {"flt.res", 10}, {"flt.env", 30}, {"flt.drive", 10},
                   {"fenv.a", 0.5}, {"fenv.d", 200}, {"fenv.s", 0}, {"amp.a", 1}, {"amp.d", 600}, {"amp.s", 40}, {"amp.r", 80}, {"vel", 60}})
            .l(2, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -1}, {"level", -8}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1}, {"amp.d", 700}, {"amp.s", 60}, {"amp.r", 80}})},
        {"Fold Bass", "BASS", B()
            .g({{"mode", "Mono"}, {"fx.drive.amount", 25}, {"fx.drive.mix", 60}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.on", 0},
                {"mod1.src", "Env 2"}, {"mod1.dst", "WT position"}, {"mod1.amount", 50}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Fold"}, {"wt.pos", 20}, {"flt.cutoff", 1500}, {"flt.res", 20}, {"flt.env", 0}, {"flt.drive", 10},
                   {"fenv.a", 0.5}, {"fenv.d", 300}, {"fenv.s", 10}, {"amp.a", 1}, {"amp.d", 500}, {"amp.s", 85}, {"amp.r", 100}})
            .l(2, {{"on", 1}, {"osc.wave", "Sine"}, {"osc.octave", -1}, {"level", -6}, {"flt.cutoff", 20000}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 1}, {"amp.s", 90}, {"amp.r", 100}})},

        // ---- more plucks
        {"Glass Pluck", "PLUCK", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 25}, {"fx.delay.time", "1/4"}, {"fx.delay.mix", 20}, {"fx.reverb.size", 3}, {"fx.reverb.mix", 35}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Glass"}, {"wt.pos", 70}, {"osc.unison", 2}, {"osc.detune", 10}, {"osc.spread", 60},
                   {"flt.type", "LP 12"}, {"flt.cutoff", 6000}, {"flt.res", 10}, {"flt.env", 40}, {"flt.drive", 0}, {"fenv.a", 0.5}, {"fenv.d", 400}, {"fenv.s", 0},
                   {"amp.a", 1}, {"amp.d", 600}, {"amp.s", 0}, {"amp.r", 400}, {"vel", 60}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "7"}, {"fm.index", 15}, {"fm.decay", 120}, {"osc.octave", 0}, {"level", -14}, {"flt.cutoff", 14000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 300}, {"amp.s", 0}, {"amp.r", 300}})},
        {"Kalimba Moon", "PLUCK", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.time", "1/8 D"}, {"fx.delay.mix", 15}, {"fx.reverb.size", 1.5}, {"fx.reverb.mix", 20}})
            .l(1, {{"osc.wave", "Sine"}, {"flt.cutoff", 20000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0}, {"amp.a", 0.5}, {"amp.d", 800}, {"amp.s", 0}, {"amp.r", 500}, {"vel", 60}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "5"}, {"fm.index", 30}, {"fm.decay", 80}, {"osc.octave", 0}, {"level", -10}, {"flt.cutoff", 14000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 250}, {"amp.s", 0}, {"amp.r", 200}})
            .l(3, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Knock"}, {"osc.octave", 1}, {"level", -14}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 200}, {"amp.s", 0}, {"amp.r", 150}})},
        {"Pizz Saw", "PLUCK", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 25}, {"fx.delay.time", "1/8 D"}, {"fx.delay.mix", 18}, {"fx.reverb.size", 1.2}, {"fx.reverb.mix", 18}})
            .l(1, {{"osc.wave", "Saw"}, {"osc.unison", 3}, {"osc.detune", 18}, {"osc.spread", 70}, {"flt.cutoff", 900}, {"flt.res", 25}, {"flt.env", 70}, {"flt.drive", 15},
                   {"fenv.a", 0.5}, {"fenv.d", 160}, {"fenv.s", 0}, {"amp.a", 1}, {"amp.d", 350}, {"amp.s", 0}, {"amp.r", 200}, {"vel", 60}})},

        // ---- more keys
        {"Satellite EP", "KEYS", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.size", 1.5}, {"fx.reverb.mix", 15},
                {"lfo1.shape", "Triangle"}, {"lfo1.rate", 4.5}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Pan"}, {"mod1.amount", 30}})   // the autopan of an electric piano
            .l(1, {{"osc.type", "FM"}, {"fm.ratio", "1"}, {"fm.index", 20}, {"fm.decay", 1200}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 3000}, {"amp.s", 20}, {"amp.r", 600}, {"vel", 75}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "14"}, {"fm.index", 8}, {"fm.decay", 60}, {"osc.octave", 0}, {"level", -16}, {"flt.cutoff", 14000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 200}, {"amp.s", 0}, {"amp.r", 200}, {"vel", 90}})},
        {"Drawbar Station", "KEYS", B()
            .g({{"fx.drive.amount", 20}, {"fx.drive.mix", 50}, {"fx.chorus.rate", 0.8}, {"fx.chorus.depth", 50}, {"fx.chorus.mix", 40}, {"fx.delay.on", 0}, {"fx.reverb.size", 1.2}, {"fx.reverb.mix", 15},
                {"mod1.src", "Mod wheel"}, {"mod1.dst", "WT position"}, {"mod1.amount", 30}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Organ"}, {"wt.pos", 70}, {"flt.type", "LP 12"}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 3}, {"amp.d", 50}, {"amp.s", 100}, {"amp.r", 60}, {"vel", 0}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Click"}, {"osc.octave", 0}, {"level", -24}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 30}, {"amp.s", 0}, {"amp.r", 30}, {"vel", 0}})},
        {"Porcelain Keys", "KEYS", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.mix", 20}, {"fx.delay.on", 0}, {"fx.reverb.size", 2}, {"fx.reverb.mix", 22}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Glass"}, {"wt.pos", 40}, {"flt.type", "LP 12"}, {"flt.cutoff", 5000}, {"flt.res", 0}, {"flt.env", 30}, {"flt.drive", 0},
                   {"fenv.a", 0.5}, {"fenv.d", 1500}, {"fenv.s", 20}, {"amp.a", 1}, {"amp.d", 2000}, {"amp.s", 0}, {"amp.r", 800}, {"vel", 70}})
            .l(2, {{"on", 1}, {"osc.wave", "Triangle"}, {"osc.octave", 0}, {"level", -8}, {"flt.cutoff", 4000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 1}, {"amp.d", 3000}, {"amp.s", 10}, {"amp.r", 800}, {"vel", 70}})
            .l(3, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "4"}, {"fm.index", 15}, {"fm.decay", 300}, {"osc.octave", 0}, {"level", -18}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 0.5}, {"amp.d", 600}, {"amp.s", 0}, {"amp.r", 400}})},

        // ---- more effects
        {"Swing-by Pass", "FX", B()
            .g({{"flyby.mode", "Pass"}, {"flyby.depth", 90}, {"flyby.time", 2.5}, {"flyby.near", 70}, {"fx.drive.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.size", 4}, {"fx.reverb.mix", 40}})
            .l(1, {{"osc.type", "Wavetable"}, {"wt.table", "Classic"}, {"wt.pos", 30}, {"osc.unison", 6}, {"osc.detune", 30}, {"osc.spread", 80},
                   {"flt.cutoff", 4000}, {"flt.res", 20}, {"flt.env", 0}, {"flt.drive", 10}, {"amp.a", 20}, {"amp.s", 100}, {"amp.r", 1500}})
            .l(2, {{"on", 1}, {"osc.type", "Sample"}, {"smp.id", "Air"}, {"osc.octave", 0}, {"level", -10}, {"flt.type", "HP 12"}, {"flt.cutoff", 1000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 20}, {"amp.s", 100}, {"amp.r", 1500}})},
        {"Rain Planet", "FX", B()
            .g({{"fx.drive.on", 0}, {"fx.delay.time", "1/4 D"}, {"fx.delay.mix", 20}, {"fx.reverb.size", 9}, {"fx.reverb.mix", 55}})
            .l(1, {{"osc.type", "Sample"}, {"smp.id", "Rain"}, {"osc.unison", 2}, {"osc.spread", 100}, {"flt.cutoff", 12000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 200}, {"amp.s", 100}, {"amp.r", 2500}})
            .l(2, {{"on", 1}, {"osc.type", "FM"}, {"fm.ratio", "1.5"}, {"fm.index", 20}, {"fm.decay", 3000}, {"osc.octave", 0}, {"level", -12}, {"flt.cutoff", 8000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 5}, {"amp.d", 3000}, {"amp.s", 30}, {"amp.r", 2500}})
            .l(3, {{"on", 1}, {"osc.type", "Wavetable"}, {"wt.table", "Glass"}, {"wt.pos", 80}, {"osc.octave", 1}, {"level", -14}, {"flt.cutoff", 9000}, {"flt.res", 0}, {"flt.env", 0}, {"flt.drive", 0},
                   {"amp.a", 2000}, {"amp.s", 100}, {"amp.r", 2500}})},
        {"Gravity Well", "FX", B()
            .g({{"fx.drive.on", 0}, {"fx.chorus.on", 0}, {"fx.delay.on", 0}, {"fx.reverb.size", 4}, {"fx.reverb.mix", 30},
                {"lfo1.shape", "Orbit"}, {"lfo1.rate", 0.25}, {"lfo1.orbit", 60}, {"mod1.src", "LFO 1"}, {"mod1.dst", "Gravity"}, {"mod1.amount", 100}})   // the stack locks and lets go
            .l(1, {{"osc.wave", "Saw"}, {"osc.unison", 8}, {"osc.detune", 60}, {"osc.spread", 100}, {"osc.gravity", 0},
                   {"flt.cutoff", 3000}, {"flt.res", 20}, {"flt.env", 0}, {"flt.drive", 10}, {"amp.a", 50}, {"amp.s", 100}, {"amp.r", 1500}})},
    };
    return t;
}

// every factory preset: the PLAY screen's ten first (in its order), then the rest grouped by the screen's category order
const std::vector<Raw>& rawTable() {
    static const std::vector<Raw> t = [] {
        std::vector<Raw> all = firstTable();
        for (const Raw& r : morePresets()) all.push_back(r);
        static const char* const order[] = {"LEAD", "PAD", "BASS", "PLUCK", "KEYS", "SEQ", "FX"};
        auto rank = [](const char* c) { for (int i = 0; i < 7; ++i) if (std::string_view(c) == order[i]) return i; return 7; };
        std::stable_sort(all.begin() + 10, all.end(), [&](const Raw& a, const Raw& b) { return rank(a.cat) < rank(b.cat); });
        return all;
    }();
    return t;
}

struct LevelRow { const char* name; double trim, level, boost; };
const LevelRow kLevels[] = {
#include "in07/preset_levels.inc"
    {nullptr, 0.0, 0.0, 0.0}
};

struct Built { std::vector<Preset> presets; std::vector<std::string> errors; };
const Built& built() {
    static const Built b = [] {
        Built r;
        const auto& s = specs();
        std::map<std::string, int> ids;
        for (int i = 0; i < kNumParams; ++i) ids[s[static_cast<size_t>(i)].id] = i;
        for (const Raw& raw : rawTable()) {
            Preset p;
            p.name = raw.name; p.category = raw.cat;
            for (const V& e : raw.b.v) {
                const std::string id = e.layer > 0 ? "in07.l" + std::to_string(e.layer) + "." + e.id : std::string("in07.") + e.id;
                const auto it = ids.find(id);
                if (it == ids.end()) { r.errors.push_back(p.name + ": no parameter " + id); continue; }
                const ParamSpec& sp = s[static_cast<size_t>(it->second)];
                double v = e.v;
                if (!e.label.empty()) {
                    const auto f = std::find(sp.labels.begin(), sp.labels.end(), std::string(e.label));
                    if (f == sp.labels.end()) { r.errors.push_back(p.name + ": " + id + " has no step \"" + std::string(e.label) + "\""); continue; }
                    v = sp.steps[static_cast<size_t>(f - sp.labels.begin())];
                } else if (sp.curve == Curve::Step) {
                    if (std::find(sp.steps.begin(), sp.steps.end(), v) == sp.steps.end()) { r.errors.push_back(p.name + ": " + id + " has no step " + std::to_string(v)); continue; }
                } else if (v < sp.min || v > sp.max) {
                    r.errors.push_back(p.name + ": " + id + " = " + std::to_string(v) + " is out of range"); continue;
                }
                bool dup = false;
                for (auto& x : p.values) if (x.first == it->second) { r.errors.push_back(p.name + ": " + id + " twice"); dup = true; }
                if (!dup) p.values.push_back({it->second, v});
            }
            for (const auto& t : kLevels) if (t.name && p.name == t.name) { p.trim = t.trim; p.level = t.level; p.boost = t.boost; }
            r.presets.push_back(std::move(p));
        }
        return r;
    }();
    return b;
}
}  // namespace

const std::vector<Preset>& factoryPresets() { return built().presets; }
const std::vector<std::string>& presetNames() {
    static const std::vector<std::string> n = [] { std::vector<std::string> v; for (const Raw& r : rawTable()) v.push_back(r.name); return v; }();
    return n;
}

void applyInit(Processor& p) {
    const auto& s = specs();
    for (int i = 0; i < kNumParams; ++i) if (presetPart(i)) p.setParam(i, s[static_cast<size_t>(i)].def);
}
const std::vector<std::string>& presetErrors() { return built().errors; }

void presetValues(int index, const PresetLevels& lv, std::vector<double>& plain) {
    const auto& s = specs();
    plain.assign(static_cast<size_t>(kNumParams), 0.0);
    for (int i = 0; i < kNumParams; ++i) plain[static_cast<size_t>(i)] = s[static_cast<size_t>(i)].def;
    const auto& P = factoryPresets();
    if (index < 0 || index >= static_cast<int>(P.size())) return;
    const Preset& pr = P[static_cast<size_t>(index)];
    auto at = [&](int id) -> double& { return plain[static_cast<size_t>(id)]; };
    for (const auto& v : pr.values) at(v.first) = v.second;
    at(Level) = std::clamp(lv.level, -40.0, 0.0);
    at(FxLimitGain) = std::clamp(at(FxLimitGain) + std::max(0.0, lv.boost), 0.0, 12.0);
    for (int l = 0; l < kLayers; ++l)
        if (at(lp(l, On)) > 0.5) at(lp(l, LayerLevel)) = std::clamp(at(lp(l, LayerLevel)) + lv.trim, -60.0, 6.0);
    at(PresetSelect) = s[static_cast<size_t>(PresetSelect)].def;
}
void presetValues(int index, std::vector<double>& plain) {
    PresetLevels lv;
    if (index >= 0 && index < static_cast<int>(factoryPresets().size())) {
        const Preset& pr = factoryPresets()[static_cast<size_t>(index)];
        lv = PresetLevels{pr.trim, pr.level, pr.boost};
    }
    presetValues(index, lv, plain);
}

void applyPreset(Processor& p, int index, const PresetLevels& lv) {
    if (index < 0 || index >= static_cast<int>(factoryPresets().size())) return;
    std::vector<double> plain;
    presetValues(index, lv, plain);
    for (int i = 0; i < kNumParams; ++i) if (presetPart(i)) p.setParam(i, plain[static_cast<size_t>(i)]);
}

// every factory preset as plain values, built once (the plug-in builds it on the main thread when it activates): loading a preset on
// the audio thread then allocates nothing
const std::vector<double>& presetPlain(int index) {
    static const std::vector<std::vector<double>> table = [] {
        std::vector<std::vector<double>> t(factoryPresets().size());
        for (size_t i = 0; i < t.size(); ++i) presetValues(static_cast<int>(i), t[i]);
        return t;
    }();
    static const std::vector<double> none;
    if (index < 0 || index >= static_cast<int>(table.size())) return none;
    return table[static_cast<size_t>(index)];
}

void applyPreset(Processor& p, int index) {
    const auto& plain = presetPlain(index);
    if (plain.size() != static_cast<size_t>(kNumParams)) return;
    for (int i = 0; i < kNumParams; ++i) if (presetPart(i)) p.setParam(i, plain[static_cast<size_t>(i)]);
}

// ---- user presets
bool isPresetCategory(std::string_view c) {
    for (const char* k : {"LEAD", "PAD", "BASS", "PLUCK", "KEYS", "SEQ", "FX"}) if (c == k) return true;
    return false;
}
std::string userPresetText(const std::vector<double>& plain, const presetfile::Meta& meta) {
    presetfile::Meta m = meta;
    m.category = presetfile::cleanText(m.category, presetfile::kMaxTextChars);
    if (!isPresetCategory(m.category)) m.category.clear();
    static_assert(MorphD == kNumParams - 1, "the morph's parameters are the last ones: a preset file stops before them");
    const std::vector<double> part(plain.begin(), plain.begin() + std::min<size_t>(plain.size(), static_cast<size_t>(MorphOn)));
    return presetfile::write(kPresetProduct, m, specs(), part, PresetSelect);
}
std::string userPresetText(const Processor& p, const presetfile::Meta& meta) {
    std::vector<double> plain(static_cast<size_t>(kNumParams));
    for (int i = 0; i < kNumParams; ++i) plain[static_cast<size_t>(i)] = p.param(i);
    return userPresetText(plain, meta);
}
bool userPresetValues(std::string_view text, std::vector<double>& plain, presetfile::Meta& meta, std::string& error) {
    presetfile::Parsed parsed;
    if (!presetfile::read(text, kPresetProduct, specs(), parsed, error, PresetSelect)) return false;
    presetValues(-1, plain);   // Init
    for (const auto& v : parsed.values) plain[static_cast<size_t>(v.first)] = v.second;
    meta = parsed.meta;
    if (!isPresetCategory(meta.category)) meta.category.clear();
    return true;
}
bool loadUserPreset(Processor& p, std::string_view text, presetfile::Meta& meta, std::string& error) {
    std::vector<double> plain;
    if (!userPresetValues(text, plain, meta, error)) return false;
    for (int i = 0; i < kNumParams; ++i) if (presetPart(i)) p.setParam(i, plain[static_cast<size_t>(i)]);
    return true;
}

std::vector<AuditionNote> audition(const std::string& c, double& total) {
    std::vector<AuditionNote> n;
    if (c == "LEAD") { const int k[4] = {72, 74, 76, 79}; for (int i = 0; i < 4; ++i) n.push_back({0.05 + 0.5 * i, 0.45, k[i]}); total = 3.2; }
    else if (c == "BASS") { const int k[4] = {36, 36, 43, 41}; for (int i = 0; i < 4; ++i) n.push_back({0.05 + 0.4 * i, 0.33, k[i]}); total = 2.2; }
    else if (c == "PAD") { for (int k : {60, 64, 67}) n.push_back({0.05, 3.0, k}); total = 4.6; }
    else if (c == "KEYS") { for (int k : {60, 64, 67, 72}) n.push_back({0.05, 1.2, k}); for (int k : {62, 65, 69, 74}) n.push_back({1.3, 1.2, k}); total = 3.5; }
    else if (c == "PLUCK") { const int k[8] = {60, 64, 67, 72, 76, 72, 67, 64}; for (int i = 0; i < 8; ++i) n.push_back({0.05 + 0.25 * i, 0.2, k[i]}); total = 3.1; }
    else if (c == "SEQ") { const int k[4] = {48, 60, 55, 60}; for (int i = 0; i < 16; ++i) n.push_back({0.05 + 0.125 * i, 0.09, k[i % 4]}); total = 2.9; }
    else { n.push_back({0.05, 3.0, 60}); total = 4.5; }   // FX
    return n;
}

std::vector<AuditionNote> presetAudition(int index, double& total) {
    const auto& P = factoryPresets();
    if (index < 0 || index >= static_cast<int>(P.size())) { total = 0.0; return {}; }
    const Preset& pr = P[static_cast<size_t>(index)];
    bool held = false;
    for (const auto& v : pr.values) held = held || ((v.first == ArpOn || v.first == GateOn) && v.second > 0.5);
    if (!held) return audition(pr.category, total);
    // the arp plays the held keys, the gate cuts them: a bass holds C2, the others C minor from C3 (C3 G3 C4 Eb4), 3 s
    std::vector<AuditionNote> n;
    if (pr.category == "BASS") { n.push_back({0.05, 2.2, 36}); total = 2.6; }
    else { for (int k : {48, 55, 60, 63}) n.push_back({0.05, 3.0, k}); total = 3.5; }
    return n;
}

PresetMeasure measurePreset(int index, double fs, const PresetLevels* lv, bool preFx) {
    PresetMeasure m;
    const auto& P = factoryPresets();
    if (index < 0 || index >= static_cast<int>(P.size())) return m;
    Processor p;
    if (lv) applyPreset(p, index, *lv); else applyPreset(p, index);
    if (preFx) { for (int f = 0; f < kFx; ++f) p.setParam(fxOnId(f), 0); p.setParam(Level, 0); }
    p.prepare(fs, 256);   // after the values: the level and the effects start where the preset puts them
    p.setTempo(120.0);
    double total = 0.0;
    const auto notes = presetAudition(index, total);
    struct Ev { long long at; int key; bool on; };
    std::vector<Ev> ev;
    for (const auto& n : notes) {
        ev.push_back({std::llround(n.start * fs), n.key, true});
        ev.push_back({std::llround((n.start + n.length) * fs), n.key, false});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at || (a.at == b.at && !a.on && b.on); });
    const long long end = std::llround(total * fs);
    IntegratedLoudness st, mo;
    st.setup(fs, 2, 0.0); mo.setup(fs, 2, 0.0);
    std::vector<float> l(256), r(256), ml(256);
    double peak = 0.0;
    size_t e = 0;
    for (long long pos = 0; pos < end;) {
        while (e < ev.size() && ev[e].at <= pos) { if (ev[e].on) p.noteOn(ev[e].key, 0.8); else p.noteOff(ev[e].key); ++e; }
        long long n = std::min<long long>(256, end - pos);
        if (e < ev.size()) n = std::min(n, ev[e].at - pos);
        const int k = static_cast<int>(n);
        float* c[2] = {l.data(), r.data()};
        p.process(c, 2, k);
        for (int i = 0; i < k; ++i) {
            peak = std::max({peak, static_cast<double>(std::abs(l[static_cast<size_t>(i)])), static_cast<double>(std::abs(r[static_cast<size_t>(i)]))});
            ml[static_cast<size_t>(i)] = 0.5f * (l[static_cast<size_t>(i)] + r[static_cast<size_t>(i)]);
        }
        const float* cs[2] = {l.data(), r.data()};
        const float* cm[2] = {ml.data(), ml.data()};
        st.process(cs, 2, k); mo.process(cm, 2, k);
        pos += n;
    }
    m.lufs = st.integrated(); m.monoLufs = mo.integrated();
    m.peakDb = 20.0 * std::log10(peak + 1e-12);
    m.rawPeakDb = 20.0 * std::log10(p.limiterPeak() + 1e-12);
    return m;
}

}  // namespace sw::in07
