#include "doctest.h"
#include "vo05/vo05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::vo05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
// vocal on the main input, music on the sidechain (identical L/R); returns the left output
std::vector<float> runSc(Processor& p, std::vector<float> voc, const std::vector<float>& mus) {
    std::vector<float> r = voc;
    for (size_t off = 0; off < voc.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, voc.size() - off));
        std::vector<float> m(static_cast<size_t>(n), 0.0f);
        for (int i = 0; i < n; ++i) if (off + static_cast<size_t>(i) < mus.size()) m[static_cast<size_t>(i)] = mus[off + static_cast<size_t>(i)];
        float* c[2] = {voc.data() + off, r.data() + off}; const float* s[2] = {m.data(), m.data()};
        p.processWithSidechain(c, 2, n, s, 2);
    }
    return voc;
}
}

TEST_CASE("VO05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"vo05.target", "vo05.range", "vo05.sensitivity", "vo05.breathskip", "vo05.ride", "vo05.evo.on", "vo05.link.music"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Target].min == -40); CHECK(s[Target].max == -6); CHECK(s[Target].def == -18);
    CHECK(s[Range].min == 0); CHECK(s[Range].max == 12); CHECK(s[Range].def == 6);
    CHECK(s[Sensitivity].labels == std::vector<std::string>{"Low", "Mid", "High"}); CHECK(s[Sensitivity].def == 1);
    CHECK(s[BreathSkip].labels == std::vector<std::string>{"Off", "On"}); CHECK(s[BreathSkip].def == 1);
    CHECK(s[Ride].min == -12); CHECK(s[Ride].max == 12); CHECK(s[Ride].def == 0); CHECK(s[Ride].automatable);
    CHECK(s[Write].def == 0); CHECK_FALSE(s[Write].automatable);
    // Music from (SW Link; appended at the end): the host's sidechain, all the other SW AUDIO instances, or the instance of one product (not a vocal product)
    const auto& m = s[MusicFrom];
    CHECK(m.def == 0); CHECK_FALSE(m.automatable); CHECK(m.curve == Curve::Step);
    REQUIRE(m.labels.size() > 20); CHECK(m.labels[0] == "Sidechain"); CHECK(m.labels[1] == "All other SW AUDIO");
    CHECK(m.steps.size() == m.labels.size()); CHECK(m.steps.front() == 0); CHECK(m.steps.back() == static_cast<double>(m.labels.size() - 1));
    CHECK(std::find(m.labels.begin(), m.labels.end(), "MS06 Master Chain") != m.labels.end());
    for (const auto& l : m.labels) CHECK(l.rfind("VO", 0) != 0);          // no vocal product can be the music
    CHECK(Processor::musicProduct(0) == nullptr); CHECK(std::string(Processor::musicProduct(1)) == "*"); CHECK(std::string(Processor::musicProduct(2)).size() == 4);
    for (size_t k = 2; k < m.labels.size(); ++k) CHECK(m.labels[k].rfind(Processor::musicProduct(static_cast<int>(k)), 0) == 0);   // the label begins with the code
}
TEST_CASE("VO05 the ride pulls the vocal to music + Target, within Range") {
    // vocal -30 dBFS, music -20 dBFS (both 1 kHz): the vocal should sit at music - 18 = -38, so the ride is -8 dB
    auto p = make({{Write, 1}, {Range, 12}, {Target, -18}});
    const auto y = runSc(p, sine(-30, 14), sine(-20, 14));
    NEAR(p.rideDb(), -8.0, 0.8); CHECK(p.listening());
    NEAR(rmsDb(y, y.size() - 24000, y.size()), -30.0 + p.rideDb(), 0.3);
    // a louder music lifts the vocal; Range stops it
    auto q = make({{Write, 1}, {Range, 6}, {Target, -10}}); runSc(q, sine(-30, 14), sine(-10, 14)); NEAR(q.rideDb(), 6.0, 0.05);
    auto r = make({{Write, 1}, {Range, 0}}); runSc(r, sine(-30, 6), sine(-10, 6)); NEAR(r.rideDb(), 0.0, 1e-9);
    // the vocal follows the music's level: music 6 dB louder -> the ride 6 dB higher
    auto a = make({{Write, 1}, {Range, 12}, {Target, -20}}); runSc(a, sine(-30, 14), sine(-20, 14));
    auto b = make({{Write, 1}, {Range, 12}, {Target, -20}}); runSc(b, sine(-30, 14), sine(-14, 14));
    NEAR(b.rideDb() - a.rideDb(), 6.0, 0.6);
}
TEST_CASE("VO05 no music, no ride: without a sidechain (or with a silent one) it holds") {
    auto p = make({{Write, 1}, {Range, 12}}); run(p, sine(-30, 8)); NEAR(p.rideDb(), 0.0, 1e-9); CHECK_FALSE(p.listening());
    auto q = make({{Write, 1}, {Range, 12}}); runSc(q, sine(-30, 8), std::vector<float>(48000 * 8, 0.0f)); NEAR(q.rideDb(), 0.0, 1e-9); CHECK_FALSE(q.listening());
    auto r = make({{Write, 1}, {Range, 12}}); runSc(r, std::vector<float>(48000 * 8, 0.0f), sine(-20, 8)); NEAR(r.rideDb(), 0.0, 1e-9);   // no vocal: no ride
}
TEST_CASE("VO05 Sensitivity: High follows faster than Mid than Low; Low keeps a wider dead band") {
    auto t63 = [](int sens) {
        auto p = make({{Write, 1}, {Range, 12}, {Target, -18}, {Sensitivity, static_cast<double>(sens)}});
        const auto in = sine(-30, 30), mus = sine(-20, 30); int n = 0; const double goal = 0.632 * -8.0;
        // let the detectors settle first (ride held at 0 by Range 0 is not possible: measure the time from the start instead)
        for (size_t off = 0; off + 256 <= in.size(); off += 256) {
            std::vector<float> a(in.begin() + static_cast<long>(off), in.begin() + static_cast<long>(off) + 256), b = a, m(mus.begin() + static_cast<long>(off), mus.begin() + static_cast<long>(off) + 256);
            float* c[2] = {a.data(), b.data()}; const float* s[2] = {m.data(), m.data()}; p.processWithSidechain(c, 2, 256, s, 2); n += 256;
            if (p.rideDb() <= goal) return n / kFs;
        }
        return 99.0;
    };
    const double hi = t63(2), mid = t63(1), lo = t63(0);
    CHECK(hi < mid); CHECK(mid < lo);
    // dead band: a 1 dB error is not ridden by Low (+-1.5 dB), is by High (+-0.25 dB)
    auto ride = [](int sens) { auto p = make({{Write, 1}, {Range, 12}, {Target, -18}, {Sensitivity, static_cast<double>(sens)}}); runSc(p, sine(-37.0, 40), sine(-20, 40)); return p.rideDb(); };   // wants -1 dB
    CHECK(std::abs(ride(0)) < 0.2); CHECK(ride(2) < -0.5);
}
TEST_CASE("VO05 Breath skip: a breath (much quieter than the phrase) is not lifted") {
    auto ride = [](double skip) {
        auto p = make({{Write, 1}, {Range, 12}, {Target, -18}, {BreathSkip, skip}, {Sensitivity, 2}});
        std::vector<float> v = sine(-30, 10); const auto br = sine(-50, 0.7); v.insert(v.end(), br.begin(), br.end());   // phrase, then a 0.7 s breath 20 dB lower
        const auto mus = sine(-20, 10.7); runSc(p, v, mus); return p.rideDb();
    };
    const double on = ride(1), off = ride(0);
    CHECK(on < -6.0);           // stayed where the phrase put it (-8)
    CHECK(off > on + 3.0);      // without the skip the quiet breath is lifted toward the target
}
TEST_CASE("VO05 Write Off: the Ride parameter (host automation) is the gain") {
    auto p = make({{Ride, 6}});
    NEAR(rmsDb(run(p, sine(-30, 2))), -24.0, 0.2);
    int id = 0; double v = 0; CHECK(p.takeParamWrite(id, v) == 0);
    NEAR(p.rideDb(), 6.0, 1e-6);
}
TEST_CASE("VO05 Write On reports the ride to the host as a gesture: begin + values ... end") {
    auto p = make({{Write, 1}, {Range, 12}});
    int id = -1; double v = 0; int flags = 0, begins = 0, ends = 0, values = 0; double last = 0;
    const auto in = sine(-30, 6), mus = sine(-20, 6);
    for (size_t off = 0; off + 256 <= in.size(); off += 256) {
        std::vector<float> a(in.begin() + static_cast<long>(off), in.begin() + static_cast<long>(off) + 256), b = a, m(mus.begin() + static_cast<long>(off), mus.begin() + static_cast<long>(off) + 256);
        float* c[2] = {a.data(), b.data()}; const float* s[2] = {m.data(), m.data()}; p.processWithSidechain(c, 2, 256, s, 2);
        flags = p.takeParamWrite(id, v);
        if (flags & 1) ++begins; if (flags & 2) { ++values; last = v; CHECK(id == Ride); } if (flags & 4) ++ends;
    }
    CHECK(begins == 1); CHECK(ends == 0); CHECK(values > 100); NEAR(last, p.rideDb(), 0.5);
    p.setParam(Write, 0);
    std::vector<float> a(256, 0.0f), b = a; float* c[2] = {a.data(), b.data()}; p.process(c, 2, 256);
    flags = p.takeParamWrite(id, v); CHECK((flags & 4) != 0);
    CHECK(p.takeParamWrite(id, v) == 0);
}
TEST_CASE("VO05 silence stays silent, extreme input finite, latency 0") {
    { auto z = make({{Write, 1}}); for (float v : run(z, std::vector<float>(4800, 0.0f))) CHECK(v == 0.0f); }
    auto p = make({{Write, 1}, {Range, 12}});
    std::vector<float> x(4800); for (size_t i = 0; i < x.size(); ++i) x[i] = (i & 1) ? 1e6f : -1e6f;
    for (float v : runSc(p, x, x)) REQUIRE(std::isfinite(v));
    CHECK(p.latencySamples() == 0);
}

TEST_CASE("VO05 Music from SW Link: the level the other instances give is the music (no sidechain); the sidechain is not used then") {
    // vocal -30 dBFS (1 kHz); the linked music is 20 dB louder than -20 dBFS means -20 LUFS-ish: the same ride as with a sidechain of -20 dBFS (K-weighting of a 1 kHz sine is +0.7 dB: use the meter's own number)
    auto asSc = make({{Write, 1}, {Range, 12}, {Target, -18}});
    runSc(asSc, sine(-30, 14), sine(-20, 14));
    const double musicLufs = asSc.musicLufs();
    // fed by SW Link: the same music as a number, and a sidechain with something else (-10 dBFS) that must not be listened to
    auto p = make({{Write, 1}, {Range, 12}, {Target, -18}, {MusicFrom, 1}});
    std::vector<float> voc = sine(-30, 14), r = voc; const auto loud = sine(-10, 14);
    for (size_t off = 0; off + 256 <= voc.size(); off += 256) { float* c[2] = {voc.data() + off, r.data() + off}; const float* sc[2] = {loud.data() + off, loud.data() + off}; p.setLinkedMusic(true, musicLufs); p.processWithSidechain(c, 2, 256, sc, 2); }
    NEAR(p.rideDb(), asSc.rideDb(), 0.3); CHECK(p.listening());
    // a louder music from SW Link lifts the vocal by as much
    auto q = make({{Write, 1}, {Range, 12}, {Target, -18}, {MusicFrom, 1}});
    std::vector<float> v2 = sine(-30, 14), r2 = v2;
    for (size_t off = 0; off + 256 <= v2.size(); off += 256) { float* c[2] = {v2.data() + off, r2.data() + off}; q.setLinkedMusic(true, musicLufs + 6.0); q.process(c, 2, 256); }
    NEAR(q.rideDb() - p.rideDb(), 6.0, 0.6);
    // nothing audible from SW Link (no instance plays): not listening, the ride stays where it is
    auto z = make({{Write, 1}, {Range, 12}, {MusicFrom, 1}});
    std::vector<float> v3 = sine(-30, 8), r3 = v3;
    for (size_t off = 0; off + 256 <= v3.size(); off += 256) { float* c[2] = {v3.data() + off, r3.data() + off}; z.setLinkedMusic(false, -200.0); z.process(c, 2, 256); }
    NEAR(z.rideDb(), 0.0, 1e-9); CHECK_FALSE(z.listening());
    // Music from = Sidechain: the number from SW Link is not used, the sidechain is (as before)
    auto w = make({{Write, 1}, {Range, 12}, {Target, -18}, {MusicFrom, 0}});
    std::vector<float> v4 = sine(-30, 14), r4 = v4; const auto m4 = sine(-20, 14);
    for (size_t off = 0; off + 256 <= v4.size(); off += 256) { float* c[2] = {v4.data() + off, r4.data() + off}; const float* sc[2] = {m4.data() + off, m4.data() + off}; w.setLinkedMusic(false, -5.0); w.processWithSidechain(c, 2, 256, sc, 2); }
    NEAR(w.rideDb(), asSc.rideDb(), 0.3);
}
