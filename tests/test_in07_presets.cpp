// SW IN07 SWINGBY factory presets: the table resolves, a preset sets every parameter, every preset plays at the same loudness
// (BS.1770, its category's audition phrase), never past the ceiling, holds up in mono, and follows its category's conventions
#include "doctest.h"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <cmath>
#include <set>
#include <string>

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
