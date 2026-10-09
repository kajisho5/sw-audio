// SW AUDIO core: the demo gate (core/include/sw/demo_gate.hpp). Without a licence the plug-ins play with silence put in (the owner's
// decision, 2026-10-09: 無音を挟む): 3 s of silence every 60 s from 30 s after the start, with 10 ms fades (no clicks). Checked: the
// schedule to the sample, the fades, that the sound outside the gaps is untouched to the bit, and that the block size changes nothing.
#include "doctest.h"
#include "sw/demo_gate.hpp"
#include <cmath>
#include <vector>

namespace {
std::vector<float> runGate(double fs, double seconds, int block, int nch = 2) {
    sw::DemoGate g;
    g.prepare(fs);
    const size_t n = static_cast<size_t>(seconds * fs);
    std::vector<std::vector<float>> ch(static_cast<size_t>(nch), std::vector<float>(n));
    for (int c = 0; c < nch; ++c)
        for (size_t i = 0; i < n; ++i) ch[static_cast<size_t>(c)][i] = 0.5f + 0.25f * static_cast<float>(std::sin(0.001 * static_cast<double>(i) + c));
    const std::vector<float> in = ch[0];
    for (size_t off = 0; off < n; off += static_cast<size_t>(block)) {
        const int k = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), n - off));
        std::vector<float*> p;
        for (auto& v : ch) p.push_back(v.data() + off);
        g.process(p.data(), nch, k);
    }
    if (nch > 1) {   // every channel gets the same gain
        bool same = true;
        for (size_t i = 0; i < n; ++i) {
            const float in1 = 0.5f + 0.25f * static_cast<float>(std::sin(0.001 * static_cast<double>(i) + 1));
            same = same && std::abs(ch[1][i] / in1 - ch[0][i] / in[i]) <= 1e-5f;
        }
        CHECK(same);
    }
    std::vector<float> gain(n);
    for (size_t i = 0; i < n; ++i) gain[i] = ch[0][i] / in[i];
    return gain;
}
}  // namespace

TEST_CASE("DEMO GATE: 3 s of silence every 60 s from 30 s, faded over 10 ms; untouched elsewhere") {
    const double fs = 48000.0;
    sw::DemoGate g;
    g.prepare(fs);
    const long long first = 30 * 48000, period = 60 * 48000, gap = 3 * 48000, fade = 480;
    CHECK(g.gainAt(0) == 1.0f);
    CHECK(g.gainAt(first - 1) == 1.0f);
    CHECK(g.gainAt(first) == 1.0f);                      // the fade starts here
    CHECK(g.gainAt(first + fade / 2) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(g.gainAt(first + fade) == 0.0f);
    CHECK(g.gainAt(first + gap / 2) == 0.0f);
    CHECK(g.gainAt(first + gap - fade - 1) == 0.0f);
    CHECK(g.gainAt(first + gap - fade / 2) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(g.gainAt(first + gap) == 1.0f);
    CHECK(g.gainAt(first + period - 1) == 1.0f);
    CHECK(g.gainAt(first + period + fade) == 0.0f);      // and again a minute later
    CHECK(g.gainAt(first + 10 * period + gap / 2) == 0.0f);
    // through process(): the gaps where they belong, nothing else changed (the gain is exactly 1)
    const std::vector<float> gain = runGate(fs, 100.0, 256);
    long long zero = 0, touched = 0;
    float maxStep = 0.0f;
    for (size_t i = 0; i < gain.size(); ++i) {
        zero += gain[i] == 0.0f;
        const long long u = static_cast<long long>(i) - first;
        const bool inGap = u >= 0 && (u % period) < gap;
        if (!inGap) CHECK(gain[i] == 1.0f);
        touched += inGap;
        if (i) maxStep = std::max(maxStep, std::abs(gain[i] - gain[i - 1]));
    }
    CHECK(zero == 2 * (gap - 2 * fade));                 // two gaps in 100 s (at 30 s and 90 s)
    CHECK(touched == 2 * gap);
    CHECK(maxStep <= 1.0f / static_cast<float>(fade) + 1e-6f);   // a 10 ms ramp, no step
}

TEST_CASE("DEMO GATE: the block size, the sample rate and the channel count change nothing; prepare restarts the schedule") {
    const std::vector<float> a = runGate(48000.0, 40.0, 256), b = runGate(48000.0, 40.0, 37), m = runGate(48000.0, 40.0, 512, 1);
    REQUIRE(a.size() == b.size());
    bool same = true;
    for (size_t i = 0; i < a.size(); ++i) same = same && a[i] == b[i] && a[i] == m[i];
    CHECK(same);
    sw::DemoGate g;
    g.prepare(44100.0);
    CHECK(g.gainAt(30 * 44100 + 441) == 0.0f);
    CHECK(g.gainAt(30 * 44100 - 1) == 1.0f);
    g.prepare(96000.0);
    CHECK(g.gainAt(30 * 96000 + 960) == 0.0f);
    // prepare again: the clock starts again (a host re-activating the plug-in)
    float ones[1] = {1.0f}, *p[1] = {ones};
    for (int i = 0; i < 31 * 96; ++i) { ones[0] = 1.0f; g.process(p, 1, 1); }
    CHECK(g.position() == 31 * 96);
    g.prepare(96000.0);
    CHECK(g.position() == 0);
}

// a new activation (the host changed the sample rate, or switched the plug-in off and on) keeps the time played: the first 30 s are not
// clean again. prepare(fs) without keepTime starts over.
TEST_CASE("DEMO GATE: a new activation keeps the time played (at the new rate) (2026-10-09)") {
    sw::DemoGate g;
    g.prepare(48000.0, true);   // the first one: nothing to keep
    CHECK(g.position() == 0);
    std::vector<float> a(4800), b(4800);
    float* ch[2] = {a.data(), b.data()};
    for (int k = 0; k < 400; ++k) g.process(ch, 2, 4800);   // 40 s
    CHECK(g.position() == 400 * 4800);
    g.prepare(48000.0, true);
    CHECK(g.position() == 400 * 4800);
    g.prepare(96000.0, true);   // the same 40 s at the new rate
    CHECK(g.position() == 800 * 4800);
    CHECK(g.gainAt(g.position()) == 1.0f);
    CHECK(g.gainAt(static_cast<int64_t>(90.5 * 96000)) == 0.0f);   // the gaps at 90 s at the new rate
    g.prepare(44100.0);
    CHECK(g.position() == 0);
}
