#include "doctest.h"
#include "vo03/vo03.hpp"
#include "tu.hpp"
#include "voice.hpp"
using namespace sw;
using namespace sw::vo03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// the voices off except those listed
Set only(std::vector<int> voices, Set extra = {}) { Set s; for (int v = 0; v < kVoices; ++v) s.push_back({voiceParam(v, On), 0.0}); for (int v : voices) s.push_back({voiceParam(v, On), 1.0}); for (auto& e : extra) s.push_back(e); return s; }
std::pair<std::vector<float>, std::vector<float>> runLR(Processor& p, const std::vector<float>& x) { auto l = x, r = x; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } return {l, r}; }
double level(const std::vector<float>& y, double f) { return binDb(y, f, 50000, 90000); }
}

TEST_CASE("VO03 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams)); CHECK(kNumParams == 31);
    CHECK(std::string(s[Source].id) == "vo03.source"); CHECK(s[Source].labels == std::vector<std::string>{"MIDI", "Scale", "Fixed"}); CHECK(s[Source].def == 1);
    for (int v = 0; v < kVoices; ++v) {
        const std::string pre = "vo03.v" + std::to_string(v + 1) + ".";
        CHECK(std::string(s[static_cast<size_t>(voiceParam(v, On))].id) == pre + "on"); CHECK(s[static_cast<size_t>(voiceParam(v, On))].def == (v < 2 ? 1.0 : 0.0));
        CHECK(std::string(s[static_cast<size_t>(voiceParam(v, Interval))].id) == pre + "interval"); CHECK(s[static_cast<size_t>(voiceParam(v, Interval))].min == -7); CHECK(s[static_cast<size_t>(voiceParam(v, Interval))].max == 7);
        CHECK(s[static_cast<size_t>(voiceParam(v, Level))].min == -60); CHECK(s[static_cast<size_t>(voiceParam(v, Level))].max == 0); CHECK(s[static_cast<size_t>(voiceParam(v, Level))].def == -3);
        CHECK(s[static_cast<size_t>(voiceParam(v, Pan))].min == -100); CHECK(s[static_cast<size_t>(voiceParam(v, Pan))].max == 100);
        CHECK(s[static_cast<size_t>(voiceParam(v, Formant))].min == -3); CHECK(s[static_cast<size_t>(voiceParam(v, Formant))].max == 3); CHECK(s[static_cast<size_t>(voiceParam(v, Formant))].def == 0);
        CHECK(s[static_cast<size_t>(voiceParam(v, Humanize))].def == 25); CHECK(s[static_cast<size_t>(voiceParam(v, Delay))].min == 0); CHECK(s[static_cast<size_t>(voiceParam(v, Delay))].max == 100); CHECK(s[static_cast<size_t>(voiceParam(v, Delay))].def == 15);
    }
    CHECK(s[static_cast<size_t>(voiceParam(0, Interval))].def == 2); CHECK(s[static_cast<size_t>(voiceParam(1, Interval))].def == 4);   // +3rd, +5th
    CHECK(s[static_cast<size_t>(voiceParam(0, Pan))].def == -40); CHECK(s[static_cast<size_t>(voiceParam(1, Pan))].def == 40);
    CHECK(std::string(s[Key].id) == "vo03.key"); CHECK(std::string(s[Scale].id) == "vo03.scale");
    CHECK(stepScale(scaleBits(0, 0), 59, 2) == 62); CHECK(stepScale(scaleBits(0, 0), 59, 4) == 65); CHECK(stepScale(scaleBits(0, 0), 59, -2) == 55); CHECK(stepScale(scaleBits(0, 0), 60, 7) == 72); CHECK(stepScale(scaleBits(2, 0), 60, 3) == 63);
    CHECK(majorDegreeSemitones(2) == 4); CHECK(majorDegreeSemitones(4) == 7); CHECK(majorDegreeSemitones(7) == 12); CHECK(majorDegreeSemitones(-2) == -3 - 0 + 0 - 0); CHECK(majorDegreeSemitones(-7) == -12);
}
TEST_CASE("VO03 reports the lookahead; silence is silence") {
    Processor q; CHECK(q.latencySamples() == PitchAnalyzer::latencyFor(engineConfig(kFs)));
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : runLR(p, z).first) CHECK(v == 0.0f);
}
TEST_CASE("VO03 the lead passes at the centre, delayed by the latency, with the voices off") {
    auto p = make(only({})); const auto x = voice(235.0, 1.0); const auto y = runLR(p, x);
    const int lat = p.latencySamples();
    for (size_t i = 30000; i < 30200; ++i) { NEAR(y.first[i], x[i - static_cast<size_t>(lat)], 1e-6); NEAR(y.second[i], x[i - static_cast<size_t>(lat)], 1e-6); }
}
TEST_CASE("VO03 Scale source: degrees of the scale above the singer's note") {
    // 235 Hz is B (59) in C major; +3rd (2 degrees) = D (62, 293.66 Hz), +5th (4 degrees) = F (65, 349.23 Hz)
    auto p = make(only({0, 1}, {{voiceParam(0, Humanize), 0}, {voiceParam(1, Humanize), 0}, {voiceParam(0, Level), 0}, {voiceParam(1, Level), 0}}));
    const auto y = runLR(p, voice(235.0, 2.0));
    const double nearD = level(y.first, 293.66), farD = level(y.first, 270.0), nearF = level(y.first, 349.23), farF = level(y.first, 320.0), dry = level(y.first, 235.0);
    CHECK(nearD > farD + 15.0); CHECK(nearF > farF + 15.0); CHECK(dry > farD + 15.0);
    NEAR(peakHz(y.first, 280, 310, 50000, 90000), 293.66, 1.0); NEAR(peakHz(y.first, 335, 365, 50000, 90000), 349.23, 1.2);
    // -3rd (2 degrees below B = G, 196 Hz) and +1 octave (B4, 493.9 Hz)
    auto q = make(only({2, 3}, {{voiceParam(2, On), 1}, {voiceParam(3, On), 1}, {voiceParam(2, Humanize), 0}, {voiceParam(3, Humanize), 0}, {voiceParam(2, Level), 0}, {voiceParam(3, Level), 0}}));
    const auto z = runLR(q, voice(235.0, 2.0));
    NEAR(peakHz(z.first, 185, 208, 50000, 90000), 196.0, 1.0); NEAR(peakHz(z.first, 480, 510, 50000, 90000), 493.88, 1.5);
}
TEST_CASE("VO03 Key and Scale choose the degrees; Fixed ignores them") {
    // D major (key 2): 235 Hz -> B (59); +3rd = D (62), as before; in B minor... use Key 4 (E major): B is the 5th degree, +3rd = D# (63, 311.13 Hz)
    auto p = make(only({0}, {{Key, 4}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Level), 0}}));
    const auto y = runLR(p, voice(235.0, 2.0));
    NEAR(peakHz(y.first, 300, 325, 50000, 90000), 311.13, 1.2);
    // Fixed: always a major third (+4 semitones) above the singer's (scale) note B: D# (63) whatever the key
    auto q = make(only({0}, {{Source, Fixed}, {Key, 0}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Level), 0}}));
    const auto z = runLR(q, voice(235.0, 2.0));
    NEAR(peakHz(z.first, 300, 325, 50000, 90000), 311.13, 1.2);
    // MIDI is not connected: it plays like Scale (C major: D)
    auto r = make(only({0}, {{Source, Midi}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Level), 0}}));
    const auto w = runLR(r, voice(235.0, 2.0)); NEAR(peakHz(w.first, 280, 310, 50000, 90000), 293.66, 1.0);
}
TEST_CASE("VO03 Level, Pan and Delay") {
    auto lv = [&](double db) { auto p = make(only({0}, {{voiceParam(0, Level), db}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Pan), 0}})); const auto y = runLR(p, voice(235.0, 2.0)); return level(y.first, 293.66); };
    NEAR(lv(-6) - lv(0), -6.0, 0.7); NEAR(lv(-12) - lv(0), -12.0, 0.7);
    auto pan = [&](double pn, bool leftCh) { auto p = make(only({0}, {{voiceParam(0, Level), 0}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Pan), pn}})); const auto y = runLR(p, voice(235.0, 2.0)); return level(leftCh ? y.first : y.second, 293.66); };
    CHECK(pan(-100, true) > pan(-100, false) + 25.0); CHECK(pan(100, false) > pan(100, true) + 25.0); NEAR(pan(0, true), pan(0, false), 0.3);
    // constant power: left^2 + right^2 is the same at the middle and at an edge
    const double c = std::pow(10.0, pan(0, true) / 10.0) + std::pow(10.0, pan(0, false) / 10.0), e = std::pow(10.0, pan(-100, true) / 10.0) + std::pow(10.0, pan(-100, false) / 10.0);
    NEAR(10 * std::log10(c / e), 0.0, 0.6);
    // Delay: the harmony starts later by the delay (the lead is the same): the energy of the 293.66 Hz band appears later
    auto onset = [&](double ms) {
        auto p = make(only({0}, {{voiceParam(0, Level), 0}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Pan), 0}, {voiceParam(0, Delay), ms}}));
        std::vector<float> x(48000, 0.0f); const auto v = voice(235.0, 1.0); for (size_t i = 12000; i < 48000; ++i) x[i] = v[i - 12000];
        const auto y = runLR(p, x);
        // the harmony alone: subtract the lead (which is x delayed by the latency)
        const int lat = p.latencySamples(); std::vector<float> h(y.first.size());
        for (size_t i = 0; i < h.size(); ++i) h[i] = y.first[i] - (i >= static_cast<size_t>(lat) ? x[i - static_cast<size_t>(lat)] : 0.0f);
        size_t k = 0; while (k < h.size() && std::abs(h[k]) < 0.02f) ++k; return k;
    };
    const size_t d0 = onset(0), d50 = onset(50);
    NEAR(static_cast<double>(d50) - static_cast<double>(d0), 0.05 * kFs, 0.002 * kFs);
}
TEST_CASE("VO03 Formant moves only the harmony's vowel; Humanize drifts the pitch") {
    auto energyAround = [&](const std::vector<float>& y, double f) { double s = 0; for (double g = f * 0.85; g <= f * 1.15; g += 4.0) s += std::pow(10.0, binDb(y, g, 50000, 90000) / 10.0); return 10 * std::log10(s + 1e-30); };
    const auto x = voice(130.8, 2.0, 0.0, 700.0, 1800.0);   // C3: the harmony +3rd = E3 (164.8 Hz)
    auto flat = make(only({0}, {{voiceParam(0, Formant), 0}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Level), 0}})); auto up = make(only({0}, {{voiceParam(0, Formant), 3}, {voiceParam(0, Humanize), 0}, {voiceParam(0, Level), 0}}));
    const auto yf = runLR(flat, x), yu = runLR(up, x);
    const double r = std::exp2(3.0 / 12.0);
    CHECK(energyAround(yu.first, 1800 * r) > energyAround(yf.first, 1800 * r) + 0.5);
    NEAR(peakHz(yf.first, 155, 175, 50000, 90000), peakHz(yu.first, 155, 175, 50000, 90000), 1.5);   // the same pitch
    // Humanize: the pitch of the harmony wanders, within about 30 cents
    auto wander = [&](double h) {
        auto p = make(only({0}, {{voiceParam(0, Humanize), h}, {voiceParam(0, Level), 0}, {voiceParam(0, Pan), -100}}));
        const auto y = runLR(p, voice(235.0, 6.0)); PitchAnalyzer an; an.prepare(engineConfig(kFs));
        // the right channel carries the lead only (pan -100 puts the harmony on the left): use the left minus the lead
        std::vector<float> hv(y.first.size()); const int lat = p.latencySamples(); const auto lead = voice(235.0, 6.0);
        for (size_t i = 0; i < hv.size(); ++i) hv[i] = y.first[i] - (i >= static_cast<size_t>(lat) ? lead[i - static_cast<size_t>(lat)] : 0.0f);
        std::vector<double> t; for (size_t i = 0; i < hv.size(); ++i) { an.push(hv[i]); if (i > 96000 && (i % 256) == 0 && an.currentVoiced()) t.push_back(semis(an.currentF0())); }
        double m = 0; for (double v : t) m += v; m /= std::max<size_t>(1, t.size()); double sd = 0; for (double v : t) sd += (v - m) * (v - m); return std::sqrt(sd / std::max<size_t>(1, t.size())) * 100.0;   // cents
    };
    CHECK(wander(0) < 4.0); CHECK(wander(100) > 6.0); CHECK(wander(100) < 30.0);
}
TEST_CASE("VO03 loud input stays finite; a stereo input is summed") {
    auto p = make({{voiceParam(2, On), 1}, {voiceParam(3, On), 1}}); const auto y = runLR(p, voice(500.0, 1.0, 800.0, 700.0, 1800.0, 0.9)); for (size_t i = 0; i < y.first.size(); ++i) { CHECK(std::isfinite(y.first[i])); CHECK(std::abs(y.first[i]) < 12.0f); }
    const auto n = runLR(p, noise(0, 1.0, 5)); for (float v : n.first) { CHECK(std::isfinite(v)); CHECK(std::abs(v) < 12.0f); }
}

TEST_CASE("VO03 reports the singer's pitch and each voice's note for the screen") {
    // 235 Hz = 58.1: B (59) in C major; +3rd (2 degrees) = D (62), +5th (4 degrees) = F (65)
    auto p = make(only({0, 1}, {{voiceParam(0, Humanize), 0}, {voiceParam(1, Humanize), 0}}));
    runLR(p, voice(235.0, 2.0));
    CHECK(p.voiced());
    NEAR(p.leadSemitones(), 58.1, 0.15); NEAR(p.voiceSemitones(0), 62.0, 0.2); NEAR(p.voiceSemitones(1), 65.0, 0.2);
    auto q = make(); runLR(q, std::vector<float>(48000, 0.0f));
    CHECK(!q.voiced());
}
