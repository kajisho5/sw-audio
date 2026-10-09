// SW IN07 SWINGBY: a whole patch (a preset) changes without a step. beginPatch() / setParam()... / endPatch(): while anything sounds the
// output fades out (kPatchFadeMs), the new values go in, the voices and effects start again from silence, and the keys still held (and the
// notes the pedal holds) play again with the new patch. Nothing sounding: the values go in at once.
#include "doctest.h"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace sw::in07;

namespace {
constexpr double kFs = 48000.0;

int presetIndex(const std::string& name) {
    const auto& n = presetNames();
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == name) return static_cast<int>(i);
    return -1;
}

void loadPreset(Processor& p, int index) { p.beginPatch(); applyPreset(p, index); p.endPatch(); }

// renders n samples in blocks of 64, appending to L and R
void render(Processor& p, size_t n, std::vector<float>& L, std::vector<float>& R) {
    std::vector<float> l(64), r(64);
    float* c[2] = {l.data(), r.data()};
    for (size_t off = 0; off < n; off += 64) {
        const int m = static_cast<int>(std::min<size_t>(64, n - off));
        p.process(c, 2, m);
        L.insert(L.end(), l.begin(), l.begin() + m);
        R.insert(R.end(), r.begin(), r.begin() + m);
    }
}

double d2(const std::vector<float>& L, const std::vector<float>& R, size_t i) {
    return std::max(std::fabs(L[i] - 2.0 * L[i - 1] + L[i - 2]), std::fabs(R[i] - 2.0 * R[i - 1] + R[i - 2]));
}

// the largest second difference of the new patch's own attack: the chord played from silence, its first 20 ms
double attackD2(int b) {
    Processor q;
    applyPreset(q, b);
    q.prepare(kFs, 256);
    q.setTempo(120);
    for (int k : {48, 55, 60, 64}) q.noteOn(k, 0.8);
    std::vector<float> L, R;
    render(q, static_cast<size_t>(0.02 * kFs), L, R);
    double m = 0.0;
    for (size_t i = 2; i < L.size(); ++i) m = std::max(m, d2(L, R, i));
    return m;
}

// the largest second difference in the 20 ms after the change (the fade, then the held chord starting again with the new patch),
// against the larger of: the 99.9th percentile of both patches' own sound (the 0.3 s before the change, and 40..300 ms after it), and the
// new patch's own attack from silence. Without the fade the old patch's values jumped under the held notes: steps 4..28 times that.
double stepRatio(int a, int b) {
    Processor p;
    applyPreset(p, a);
    p.prepare(kFs, 256);
    p.setTempo(120);
    for (int k : {48, 55, 60, 64}) p.noteOn(k, 0.8);
    std::vector<float> L, R;
    render(p, static_cast<size_t>(1.0 * kFs), L, R);
    const size_t sw = L.size();
    loadPreset(p, b);
    render(p, static_cast<size_t>(0.3 * kFs), L, R);
    std::vector<double> d;
    for (size_t i = sw - static_cast<size_t>(0.3 * kFs); i < sw; ++i) d.push_back(d2(L, R, i));
    for (size_t i = sw + static_cast<size_t>(0.04 * kFs); i < L.size(); ++i) d.push_back(d2(L, R, i));
    std::sort(d.begin(), d.end());
    const double ref = std::max(d[static_cast<size_t>(static_cast<double>(d.size()) * 0.999)], attackD2(b)) + 1e-6;
    double after = 0.0;
    for (size_t i = sw; i < sw + static_cast<size_t>(0.02 * kFs); ++i) after = std::max(after, d2(L, R, i));
    return after / ref;
}
}  // namespace

TEST_CASE("IN07 PATCH: a preset change under a held chord has no step (the pairs that stepped before, and a spread of others)") {
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"Tine Piano", "Wide Saw Lead"}, {"Jazz Organ", "Bounce Seq"}, {"Sine Glide", "Anthem Supersaw"}, {"Glass Horizon", "Kalimba Moon"},
        {"Polar Bass", "Arp Pulse"}, {"Fold Bass", "Formant Seq"}, {"Deep Field", "Comet Tail"}, {"Slow Sweep", "Moon Slap"},
        {"Anthem Supersaw", "Sine Glide"}, {"Radio Static", "Velvet Keys"}};
    for (const auto& pr : pairs) {
        const int a = presetIndex(pr.first), b = presetIndex(pr.second);
        REQUIRE_MESSAGE(a >= 0, pr.first);
        REQUIRE_MESSAGE(b >= 0, pr.second);
        const double ratio = stepRatio(a, b);
        CHECK_MESSAGE(ratio < 1.5, pr.first << " -> " << pr.second << ": " << ratio);
    }
}

TEST_CASE("IN07 PATCH: the output fades out over kPatchFadeMs, then the held keys play again with the new patch") {
    const int a = presetIndex("Velvet Keys"), b = presetIndex("Anthem Supersaw");
    REQUIRE(a >= 0); REQUIRE(b >= 0);
    Processor p;
    applyPreset(p, a);
    p.prepare(kFs, 256);
    for (int k : {48, 55, 60}) p.noteOn(k, 0.8);
    std::vector<float> L, R;
    render(p, static_cast<size_t>(0.5 * kFs), L, R);
    const double cutoffB = [&] { Processor q; applyPreset(q, b); return q.param(lp(1, Cutoff)); }();
    loadPreset(p, b);
    CHECK(p.patchPending());
    // the host reads the new values at once (they are reported back while the fade runs)
    CHECK(p.param(lp(1, Cutoff)) == doctest::Approx(cutoffB));
    CHECK(p.param(lp(1, On)) == doctest::Approx(1.0));
    const size_t sw = L.size(), fade = static_cast<size_t>(std::lround(Processor::kPatchFadeMs * 1e-3 * kFs));
    render(p, fade, L, R);
    CHECK_FALSE(p.patchPending());
    // the fade: falls to silence at its last sample, never above the level before it
    double before = 0.0;
    for (size_t i = sw - 480; i < sw; ++i) before = std::max(before, static_cast<double>(std::max(std::fabs(L[i]), std::fabs(R[i]))));
    for (size_t i = sw; i < sw + fade; ++i) CHECK(std::fabs(L[i]) <= before * 1.05 + 1e-6);
    CHECK(std::fabs(L[sw + fade - 1]) < 1e-4);
    // the three keys sound again, on every layer the new patch turns on (and on no other)
    CHECK(p.notes() == 3);
    Processor q;
    applyPreset(q, b);
    for (int k : {48, 55, 60})
        for (int l = 0; l < kLayers; ++l) CHECK((p.find(k, l) != nullptr) == (q.param(lp(l, On)) > 0.5));
    render(p, static_cast<size_t>(0.2 * kFs), L, R);
    double after = 0.0;
    for (size_t i = sw + fade; i < L.size(); ++i) after = std::max(after, static_cast<double>(std::fabs(L[i])));
    CHECK(after > 0.01);   // and they are heard
    // releasing the keys releases the new notes
    for (int k : {48, 55, 60}) p.noteOff(k);
    render(p, static_cast<size_t>(2.0 * kFs), L, R);
    CHECK(p.notes() == 0);
}

TEST_CASE("IN07 PATCH: nothing sounding: the values go in at once, with no fade") {
    Processor p;
    p.prepare(kFs, 256);
    std::vector<float> L, R;
    render(p, 4800, L, R);
    p.beginPatch();
    p.setParam(lp(0, Cutoff), 800.0);
    p.setParam(Level, -12.0);
    p.endPatch();
    CHECK_FALSE(p.patchPending());
    CHECK(p.param(lp(0, Cutoff)) == doctest::Approx(800.0));
    // a note right away is not held back
    p.noteOn(60, 0.8);
    CHECK(p.find(60, 0) != nullptr);
    render(p, 480, L, R);
    double pk = 0.0;
    for (size_t i = L.size() - 480; i < L.size(); ++i) pk = std::max(pk, static_cast<double>(std::fabs(L[i])));
    CHECK(pk > 1e-3);
}

TEST_CASE("IN07 PATCH: values set during the fade wait for it; a second patch during the fade replaces the first") {
    const int a = presetIndex("Glass Horizon"), b = presetIndex("Polar Bass"), c = presetIndex("Velvet Keys");
    REQUIRE(a >= 0); REQUIRE(b >= 0); REQUIRE(c >= 0);
    Processor p;
    applyPreset(p, a);
    p.prepare(kFs, 256);
    p.noteOn(60, 0.8);
    std::vector<float> L, R;
    render(p, 9600, L, R);
    loadPreset(p, b);
    render(p, 64, L, R);
    CHECK(p.patchPending());
    p.setParam(Level, -20.0);   // automation during the fade
    loadPreset(p, c);           // and another preset
    render(p, static_cast<size_t>(0.02 * kFs), L, R);
    CHECK_FALSE(p.patchPending());
    Processor q;
    applyPreset(q, c);
    for (int i = 0; i < kNumParams; ++i)
        if (i != PresetSelect) CHECK_MESSAGE(p.param(i) == doctest::Approx(q.param(i)), specs()[static_cast<size_t>(i)].id);
    CHECK(p.notes() == 1);
}

TEST_CASE("IN07 PATCH: the pedal: a note it holds plays again and stays held by it; released notes in their tail end and are reported") {
    const int a = presetIndex("Glass Horizon"), b = presetIndex("Warm Analog Pad");
    REQUIRE(a >= 0); REQUIRE(b >= 0);
    Processor p;
    applyPreset(p, a);
    p.prepare(kFs, 256);
    p.noteOn(60, 0.8, 0, 11);
    p.noteOn(64, 0.8, 0, 12);
    std::vector<float> L, R;
    render(p, 4800, L, R);
    p.noteOff(64);            // in its release tail (no pedal yet)
    p.sustain(true);
    p.noteOff(60);            // held by the pedal
    render(p, 480, L, R);
    int key, ch, id;
    while (p.takeEnded(key, ch, id)) {}
    loadPreset(p, b);
    render(p, static_cast<size_t>(0.02 * kFs), L, R);
    // 64 was released: it ends with the change; 60 sounds again under the pedal and is not reported
    std::vector<int> ended;
    while (p.takeEnded(key, ch, id)) ended.push_back(id);
    CHECK(ended == std::vector<int>{12});
    CHECK(p.notes() == 1);
    CHECK(p.find(60, 0) != nullptr);
    p.sustain(false);
    render(p, static_cast<size_t>(6.0 * kFs), L, R);
    CHECK(p.notes() == 0);
    ended.clear();
    while (p.takeEnded(key, ch, id)) ended.push_back(id);
    CHECK(ended == std::vector<int>{11});
}

TEST_CASE("IN07 PATCH: poly to legato and back: the top key sounds, the other held keys stay held") {
    const int poly = presetIndex("Glass Horizon"), legato = presetIndex("Sine Glide");
    REQUIRE(poly >= 0); REQUIRE(legato >= 0);
    Processor p;
    applyPreset(p, poly);
    p.prepare(kFs, 256);
    for (int k : {48, 52, 55}) p.noteOn(k, 0.8);
    std::vector<float> L, R;
    render(p, 4800, L, R);
    loadPreset(p, legato);
    render(p, static_cast<size_t>(0.02 * kFs), L, R);
    CHECK(p.notes() == 1);
    CHECK(p.find(55, 0) != nullptr);   // the last key played
    p.noteOff(55);                     // back to the key still held (legato)
    render(p, 480, L, R);
    CHECK(p.find(52, 0) != nullptr);
    loadPreset(p, poly);               // and back to poly: both held keys sound
    render(p, static_cast<size_t>(0.02 * kFs), L, R);
    CHECK(p.notes() == 2);
    CHECK(p.find(48, 0) != nullptr);
    CHECK(p.find(52, 0) != nullptr);
}

TEST_CASE("IN07 PATCH: before prepare, and a prepare during a fade, take the values at once") {
    Processor p;
    p.beginPatch();
    p.setParam(lp(0, Cutoff), 900.0);
    p.endPatch();
    CHECK(p.param(lp(0, Cutoff)) == doctest::Approx(900.0));
    p.prepare(kFs, 256);
    p.noteOn(60, 0.8);
    std::vector<float> L, R;
    render(p, 4800, L, R);
    p.beginPatch();
    p.setParam(lp(0, Cutoff), 1200.0);
    p.endPatch();
    CHECK(p.patchPending());
    p.prepare(kFs, 256);
    CHECK_FALSE(p.patchPending());
    CHECK(p.param(lp(0, Cutoff)) == doctest::Approx(1200.0));
}
