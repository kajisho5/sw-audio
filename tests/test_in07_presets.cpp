// SW IN07 SWINGBY factory presets: the table resolves, a preset sets every parameter, every preset plays at the same loudness
// (BS.1770, its category's audition phrase), never past the ceiling, holds up in mono, and follows its category's conventions
#include "doctest.h"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
double val(const Preset& pr, const char* id) {   // the preset's value for an id, or the default
    const auto& s = specs();
    for (int i = 0; i < kNumParams; ++i)
        if (std::string(s[static_cast<size_t>(i)].id) == id) {
            for (const auto& v : pr.values) if (v.first == i) return v.second;
            return s[static_cast<size_t>(i)].def;
        }
    return NAN;
}
double layerVal(const Preset& pr, int l, const char* key) { return val(pr, ("in07.l" + std::to_string(l + 1) + "." + key).c_str()); }
}  // namespace

TEST_CASE("IN07 PRESETS: the table") {
    const auto& P = factoryPresets();
    CHECK(presetErrors().empty());
    for (const auto& e : presetErrors()) MESSAGE("preset table: " << e);
    REQUIRE(P.size() >= 28);
    // the PLAY screen's ten come first, in its order
    const char* screen[10] = {"Anthem Supersaw", "Hoover Stab", "Detuned Lead", "Glass Horizon", "Solar Wind", "Night Pulse", "Polar Bass", "Quiet Comet", "Velvet Keys", "Radio Static"};
    for (int i = 0; i < 10; ++i) CHECK(P[static_cast<size_t>(i)].name == screen[i]);
    std::set<std::string> names;
    for (const auto& p : P) names.insert(p.name);
    CHECK(names.size() == P.size());
    const std::set<std::string> cats = {"LEAD", "PAD", "SEQ", "BASS", "PLUCK", "KEYS", "FX"};
    for (const auto& c : cats) {
        int n = 0;
        for (const auto& p : P) n += p.category == c;
        CHECK_MESSAGE(n >= 4, c);
    }
    for (const auto& p : P) CHECK_MESSAGE(cats.count(p.category) == 1, p.name);
    // the trim keeps every layer in its range (a layer pushed past +6 dB would be clamped and the preset would miss its loudness)
    for (const auto& p : P)
        for (int l = 0; l < kLayers; ++l) {
            if (layerVal(p, l, "on") < 0.5) continue;
            CHECK_MESSAGE(layerVal(p, l, "level") + p.trim <= 6.0, p.name);
            CHECK_MESSAGE(layerVal(p, l, "level") + p.trim > -60.0, p.name);
        }
    // the output side: the master Level only turns down (it sits after the limiter); what has to come up goes into the limiter's gain
    for (const auto& p : P) {
        CHECK_MESSAGE(p.level <= 0.0, p.name);
        CHECK_MESSAGE(p.level >= -40.0, p.name);
        CHECK_MESSAGE(p.boost >= 0.0, p.name);
        CHECK_MESSAGE(val(p, "in07.fx.limit.gain") + p.boost <= 12.0, p.name);
        CHECK_MESSAGE((p.level == 0.0 || p.boost == 0.0), p.name);   // one or the other
    }
}

TEST_CASE("IN07 PRESETS: 128 presets, the counts the sales page and the manual give (LEAD 18, PAD 19, BASS 19, PLUCK 18, KEYS 18, SEQ 18, FX 18)") {
    const auto& P = factoryPresets();
    CHECK(P.size() == 128);   // program change 0..127
    const std::pair<const char*, int> want[] = {{"LEAD", 18}, {"PAD", 19}, {"BASS", 19}, {"PLUCK", 18}, {"KEYS", 18}, {"SEQ", 18}, {"FX", 18}};
    for (const auto& w : want) {
        int n = 0;
        for (const auto& p : P) n += p.category == w.first;
        CHECK_MESSAGE(n == w.second, w.first);
    }
}

TEST_CASE("IN07 PRESETS: the ULTRA sounds are factory presets (2026-10-10), in the places of the near duplicates they replace") {
    const auto& P = factoryPresets();
    const std::pair<const char*, const char*> ultra[] = {{"Ultra Saw", "LEAD"}, {"Ultra Bass", "BASS"}, {"Ultra Wobble", "BASS"}, {"Ultra Riddim", "BASS"}};
    for (const auto& u : ultra) {
        int found = -1;
        for (size_t i = 0; i < P.size(); ++i) if (P[i].name == u.first) found = static_cast<int>(i);
        REQUIRE_MESSAGE(found >= 0, u.first);
        CHECK(P[static_cast<size_t>(found)].category == u.second);
    }
    for (const char* gone : {"Wide Saw Lead", "Wobble Monster", "Growl Fold", "Square Depth"})
        for (const auto& p : P) CHECK_MESSAGE(p.name != gone, gone);
}

TEST_CASE("IN07 PRESETS: factory presets play the arpeggiator and the trance gate; their audition is a held chord") {
    const auto& P = factoryPresets();
    auto uses = [&](size_t i, int id) { for (const auto& v : P[i].values) if (v.first == id) return v.second > 0.5; return false; };
    std::set<std::string> arp, gate;
    for (size_t i = 0; i < P.size(); ++i) { if (uses(i, ArpOn)) arp.insert(P[i].name); if (uses(i, GateOn)) gate.insert(P[i].name); }
    for (const char* n : {"Arp Pulse", "Bounce Seq", "Glass Steps", "Acid Seq"}) CHECK_MESSAGE(arp.count(n) == 1, n);
    for (const char* n : {"Trance Gate", "Stutter Saw", "Ultra Riddim"}) CHECK_MESSAGE(gate.count(n) == 1, n);
    // the audition: an arp or gate preset holds its notes (the arp plays them, the gate cuts them); the others play their category's phrase
    for (size_t i = 0; i < P.size(); ++i) {
        INFO(P[i].name);
        double total = 0.0, catTotal = 0.0;
        const auto a = presetAudition(static_cast<int>(i), total);
        const auto c = audition(P[i].category, catTotal);
        REQUIRE_FALSE(a.empty());
        if (uses(i, ArpOn) || uses(i, GateOn)) {
            for (const auto& n : a) { CHECK(n.start == doctest::Approx(a[0].start)); CHECK(n.length >= 2.0); }
            CHECK(total >= a[0].start + a[0].length);
        } else {
            REQUIRE(a.size() == c.size());
            for (size_t k = 0; k < a.size(); ++k) { CHECK(a[k].start == c[k].start); CHECK(a[k].length == c[k].length); CHECK(a[k].key == c[k].key); }
            CHECK(total == catTotal);
        }
    }
    // held, with the host stopped (the arp and the gate start with the first key): the sound moves on the steps. The loudness in 1/64 windows
    // over two seconds swings by several dB (a held pad barely moves)
    auto swing = [](int index) {
        Processor p; applyPreset(p, index);
        p.prepare(48000, 256); p.setTempo(120);
        double total = 0.0;
        for (const auto& n : presetAudition(index, total)) p.noteOn(n.key, 0.8);
        const int win = 48000 * 60 / 120 / 16;   // a 1/64 at 120 bpm: 375 samples
        std::vector<float> l(256), r(256);
        std::vector<double> e;
        double acc = 0.0; int in = 0;
        const int n = 48000 * 3;
        for (int off = 0; off < n; off += 256) {
            const int k = std::min(256, n - off);
            float* c[2] = {l.data(), r.data()};
            p.process(c, 2, k);
            for (int i = 0; i < k; ++i) {
                acc += l[static_cast<size_t>(i)] * l[static_cast<size_t>(i)] + r[static_cast<size_t>(i)] * r[static_cast<size_t>(i)];
                if (++in == win) { if (off + i >= 48000) e.push_back(10.0 * std::log10(acc / win + 1e-20)); acc = 0.0; in = 0; }
            }
        }
        std::sort(e.begin(), e.end());
        return e[e.size() * 9 / 10] - e[e.size() / 10];   // the loud steps over the quiet ones (90th over 10th percentile, dB)
    };
    for (size_t i = 0; i < P.size(); ++i)
        if (uses(i, ArpOn) || uses(i, GateOn)) {
            const double s = swing(static_cast<int>(i));
            MESSAGE(P[i].name << ": the steps swing " << s << " dB");
            CHECK_MESSAGE(s >= 6.0, P[i].name);
        }
    int pad = -1;
    for (size_t i = 0; i < P.size(); ++i) if (P[i].name == "Glass Horizon") pad = static_cast<int>(i);
    REQUIRE(pad >= 0);
    CHECK(swing(pad) < 3.0);
}

TEST_CASE("IN07 PRESETS: a preset sets every parameter (the defaults first, then its own values, then its staging and output level)") {
    Processor a, b;
    a.prepare(48000, 256); b.prepare(48000, 256);
    applyPreset(a, 5); applyPreset(a, 0);
    applyPreset(b, 0);
    int diff = 0;
    for (int i = 0; i < kNumParams; ++i) diff += a.param(i) != b.param(i);
    CHECK(diff == 0);
    const Preset& p0 = factoryPresets()[0];
    CHECK(b.param(Level) == doctest::Approx(p0.level).epsilon(1e-9).scale(1.0));   // through the normalised round trip (not exact on every platform)
    CHECK(b.param(FxLimitGain) == doctest::Approx(val(p0, "in07.fx.limit.gain") + p0.boost).epsilon(1e-9).scale(1.0));
    for (int l = 0; l < kLayers; ++l)   // the trim moves every layer that is on
        if (b.param(lp(l, On)) > 0.5) CHECK(b.param(lp(l, LayerLevel)) == doctest::Approx(layerVal(p0, l, "level") + p0.trim));
    // a parameter the preset does not name is at its default
    const auto& s = specs();
    const auto& v = p0.values;
    for (int i = 0; i < kNumParams; ++i) {
        if (i == Level || i == FxLimitGain || (i >= kNumGlobal && i < kFxBase && (i - kNumGlobal) % kLayerParams == LayerLevel)) continue;
        bool named = false;
        for (const auto& x : v) named = named || x.first == i;
        if (!named) CHECK_MESSAGE(b.param(i) == doctest::Approx(s[static_cast<size_t>(i)].def).epsilon(1e-9), std::string(s[static_cast<size_t>(i)].id));   // log curves round-trip within 1e-9
    }
}

TEST_CASE("IN07 PRESETS: every preset plays at the target loudness, under the ceiling, and holds up in mono") {
    const auto& P = factoryPresets();
    for (int i = 0; i < static_cast<int>(P.size()); ++i) {
        const PresetMeasure m = measurePreset(i);
        MESSAGE(P[static_cast<size_t>(i)].category << " " << P[static_cast<size_t>(i)].name << ": " << m.lufs << " LUFS, mono " << m.monoLufs << ", peak " << m.peakDb << " dBFS, before the limiter " << m.rawPeakDb << " dBFS");
        CHECK_MESSAGE(std::abs(m.lufs - kPresetTargetLufs) <= 1.0, P[static_cast<size_t>(i)].name);
        CHECK_MESSAGE(m.peakDb <= -0.99, P[static_cast<size_t>(i)].name);          // the limiter's ceiling (-1 dBFS)
        CHECK_MESSAGE(m.rawPeakDb <= 3.0, P[static_cast<size_t>(i)].name);         // the limiter only trims (at most 4 dB off the peaks)
        CHECK_MESSAGE(m.lufs - m.monoLufs <= 3.0, P[static_cast<size_t>(i)].name); // a wide sound does not collapse in mono
        CHECK_MESSAGE(std::isfinite(m.lufs), P[static_cast<size_t>(i)].name);
        // the gain staging: the voices' sum, before the effects, sits at the same loudness in every preset (so a Drive or a limiter
        // works on the level it was set for)
        const PresetMeasure pre = measurePreset(i, 48000.0, nullptr, true);
        CHECK_MESSAGE(std::abs(pre.lufs - kPresetStageLufs) <= 0.5, P[static_cast<size_t>(i)].name);
    }
}

TEST_CASE("IN07 PRESETS: categories follow their conventions") {
    for (const auto& p : factoryPresets()) {
        INFO(p.name);
        int unisonSum = 0;
        for (int l = 0; l < kLayers; ++l) {
            if (layerVal(p, l, "on") < 0.5) continue;
            unisonSum += static_cast<int>(layerVal(p, l, "osc.unison"));
            if (p.category == "PAD") { CHECK(layerVal(p, l, "amp.r") >= 1500.0); }
            if (p.category == "PLUCK") { CHECK(layerVal(p, l, "amp.s") <= 5.0); CHECK(layerVal(p, l, "amp.d") <= 2000.0); }
            if (p.category == "SEQ") { CHECK(layerVal(p, l, "amp.r") <= 400.0); }
            if (p.category == "KEYS" || p.category == "PLUCK" || p.category == "BASS" || p.category == "SEQ") { CHECK(layerVal(p, l, "amp.a") <= 10.0); }
        }
        if (p.category == "PAD") {   // the main layer swells in
            CHECK(layerVal(p, 0, "amp.a") >= 100.0);
        }
        if (p.category == "BASS") { CHECK(val(p, "in07.mode") != Poly); }
        CHECK(unisonSum <= 24);   // light by construction: at most 24 oscillator copies per note
        CHECK(val(p, "in07.fx.limit.on") == 1.0);
    }
}

TEST_CASE("IN07 PRESETS: the preset selector (after the matrix; the arp and gate were appended after it; the plug-in layer loads the preset, the engine only keeps the value)") {
    const auto& s = specs();
    REQUIRE(ArpOn == PresetSelect + 1);
    const sw::ParamSpec& ps = s[static_cast<size_t>(PresetSelect)];
    CHECK(std::string(ps.id) == "in07.preset");
    CHECK_FALSE(ps.automatable);
    REQUIRE(ps.numSteps() == static_cast<int>(factoryPresets().size()) + 1);
    CHECK(ps.labels[0] == "Init");
    for (size_t i = 0; i < factoryPresets().size(); ++i) CHECK(ps.labels[i + 1] == factoryPresets()[i].name);
    CHECK(ps.def == 0.0);
    // the engine keeps the value and changes nothing else
    Processor p; p.prepare(48000, 256);
    applyPreset(p, 6);
    std::vector<double> before(static_cast<size_t>(kNumParams));
    for (int i = 0; i < kNumParams; ++i) before[static_cast<size_t>(i)] = p.param(i);
    p.setParam(PresetSelect, 3);
    CHECK(p.param(PresetSelect) == 3.0);
    for (int i = 0; i < kNumParams; ++i) if (i != PresetSelect) CHECK(p.param(i) == before[static_cast<size_t>(i)]);
    // a preset leaves the selector alone (the plug-in layer owns it); Init puts every other parameter back to its default
    p.setParam(PresetSelect, 7);
    applyPreset(p, 0);
    CHECK(p.param(PresetSelect) == 7.0);
    applyInit(p);
    for (int i = 0; i < kNumParams; ++i)
        if (i != PresetSelect) CHECK(p.param(i) == doctest::Approx(s[static_cast<size_t>(i)].def).epsilon(1e-9));
}

TEST_CASE("IN07 PRESETS: a factory preset as plain values (for the plug-in layer's main thread) is what applyPreset sets") {
    const auto& s = specs();
    for (int i = 0; i < static_cast<int>(factoryPresets().size()); i += 7) {
        Processor p; applyPreset(p, i);
        std::vector<double> v;
        presetValues(i, v);
        REQUIRE(v.size() == static_cast<size_t>(kNumParams));
        CHECK(v[static_cast<size_t>(PresetSelect)] == 0.0);
        for (int j = 0; j < kNumParams; ++j) {
            if (j == PresetSelect) continue;
            CHECK_MESSAGE(v[static_cast<size_t>(j)] == doctest::Approx(p.param(j)).epsilon(1e-9).scale(1.0), factoryPresets()[static_cast<size_t>(i)].name << " " << s[static_cast<size_t>(j)].id);
        }
    }
    std::vector<double> v;
    presetValues(-1, v);   // out of range: Init
    REQUIRE(v.size() == static_cast<size_t>(kNumParams));
    for (int j = 0; j < kNumParams; ++j) CHECK(v[static_cast<size_t>(j)] == s[static_cast<size_t>(j)].def);
}

TEST_CASE("IN07 USER PRESETS: a sound saved from the engine loads back the same in another engine; the selector is not part of it") {
    Processor a; a.prepare(48000, 256);
    applyPreset(a, 17);
    a.setParam(lp(0, Position), 37.5);   // a few hand edits
    a.setParam(FxReverbMix, 33.0);
    a.setParam(Mode, Legato);
    a.setParam(PresetSelect, 18);
    sw::presetfile::Meta meta{"My \tLead\n", "LEAD", "me", ""};
    const std::string text = userPresetText(a, meta);
    CHECK(text.find("product=in07") != std::string::npos);
    CHECK(text.find("name=My Lead\n") != std::string::npos);   // the name is cleaned when it is written
    CHECK(text.find("in07.preset") == std::string::npos);
    Processor b; b.prepare(48000, 256);
    applyPreset(b, 3);
    b.setParam(PresetSelect, 4);
    sw::presetfile::Meta got; std::string err;
    REQUIRE(loadUserPreset(b, text, got, err));
    CHECK(got.name == "My Lead");
    CHECK(got.category == "LEAD");
    CHECK(b.param(PresetSelect) == 4.0);
    const auto& s = specs();
    for (int j = 0; j < kNumParams; ++j)
        if (j != PresetSelect) CHECK_MESSAGE(b.param(j) == doctest::Approx(a.param(j)).epsilon(1e-12).scale(1.0), s[static_cast<size_t>(j)].id);
    // the plain-value path (the plug-in layer works on its host values): the same text from a vector
    std::vector<double> plain(static_cast<size_t>(kNumParams));
    for (int j = 0; j < kNumParams; ++j) plain[static_cast<size_t>(j)] = a.param(j);
    CHECK(userPresetText(plain, meta) == text);
}

TEST_CASE("IN07 USER PRESETS: a file that names only some parameters starts from Init; an unknown category is dropped; a damaged file changes nothing") {
    Processor p; p.prepare(48000, 256);
    applyPreset(p, 0);
    sw::presetfile::Meta m; std::string err;
    REQUIRE(loadUserPreset(p, "SW-PRESET 1\nproduct=in07\nname=Tiny\ncategory=WOBBLE\nin07.l1.flt.cutoff=500\nin07.mode=Mono\n", m, err));
    CHECK(m.category.empty());
    const auto& s = specs();
    for (int j = 0; j < kNumParams; ++j) {
        if (j == PresetSelect) continue;
        const double want = j == lp(0, Cutoff) ? 500.0 : j == Mode ? static_cast<double>(Mono) : s[static_cast<size_t>(j)].def;
        CHECK_MESSAGE(p.param(j) == doctest::Approx(want).epsilon(1e-9), s[static_cast<size_t>(j)].id);
    }
    std::vector<double> before(static_cast<size_t>(kNumParams));
    for (int j = 0; j < kNumParams; ++j) before[static_cast<size_t>(j)] = p.param(j);
    CHECK_FALSE(loadUserPreset(p, "SW-PRESET 1\nproduct=dy01\nin07.level=-3\n", m, err));
    CHECK_FALSE(loadUserPreset(p, std::string("\x00\x01\x02", 3), m, err));
    for (int j = 0; j < kNumParams; ++j) CHECK(p.param(j) == before[static_cast<size_t>(j)]);
}
