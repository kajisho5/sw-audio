#include "doctest.h"
#include "ut03/ut03.hpp"
#include "tu.hpp"
#include <cstring>
using namespace sw;
using namespace sw::ut03;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void tag(std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(t[i])); }
// WAV with `bits` (16, 24 PCM or 32 float), channels 1 or 2
std::vector<uint8_t> wav(const std::vector<float>& l, const std::vector<float>* r, double rate, int bits) {
    const int nch = r ? 2 : 1, bytes = bits / 8; std::vector<uint8_t> d;
    for (size_t i = 0; i < l.size(); ++i) for (int c = 0; c < nch; ++c) {
        const float x = c == 0 ? l[i] : (*r)[i];
        if (bits == 32) { uint32_t u; std::memcpy(&u, &x, 4); put32(d, u); }
        else { const int32_t v = static_cast<int32_t>(std::lround(x * (bits == 16 ? 32767.0 : 8388607.0))); for (int k = 0; k < bytes; ++k) d.push_back(static_cast<uint8_t>(v >> (8 * k))); }
    }
    std::vector<uint8_t> f; tag(f, "RIFF"); put32(f, static_cast<uint32_t>(36 + d.size())); tag(f, "WAVE"); tag(f, "fmt "); put32(f, 16);
    put16(f, bits == 32 ? 3 : 1); put16(f, static_cast<uint16_t>(nch)); put32(f, static_cast<uint32_t>(rate)); put32(f, static_cast<uint32_t>(rate * nch * bytes)); put16(f, static_cast<uint16_t>(nch * bytes)); put16(f, static_cast<uint16_t>(bits));
    tag(f, "data"); put32(f, static_cast<uint32_t>(d.size())); f.insert(f.end(), d.begin(), d.end()); return f;
}
void be32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
std::vector<uint8_t> aiff16(const std::vector<float>& l, double rate) {
    std::vector<uint8_t> d; for (float x : l) { const int16_t v = static_cast<int16_t>(std::lround(x * 32767.0)); d.push_back(static_cast<uint8_t>(v >> 8)); d.push_back(static_cast<uint8_t>(v)); }
    std::vector<uint8_t> f; tag(f, "FORM"); be32(f, static_cast<uint32_t>(4 + 8 + 18 + 8 + 8 + d.size())); tag(f, "AIFF"); tag(f, "COMM"); be32(f, 18);
    f.push_back(0); f.push_back(1); be32(f, static_cast<uint32_t>(l.size())); f.push_back(0); f.push_back(16);
    int e = 0; double m = rate; while (m >= 2.0) { m /= 2; ++e; } while (m < 1.0) { m *= 2; --e; }
    const uint64_t mant = static_cast<uint64_t>(m * 9223372036854775808.0); const int ex = 16383 + e; f.push_back(static_cast<uint8_t>(ex >> 8)); f.push_back(static_cast<uint8_t>(ex));
    for (int i = 7; i >= 0; --i) f.push_back(static_cast<uint8_t>(mant >> (8 * i)));
    tag(f, "SSND"); be32(f, static_cast<uint32_t>(8 + d.size())); be32(f, 0); be32(f, 0); f.insert(f.end(), d.begin(), d.end()); return f;
}
bool load(Processor& p, int slot, const std::vector<float>& x) { const auto w = wav(x, nullptr, kFs, 32); return p.loadReference(slot, w.data(), w.size()); }
}  // namespace

TEST_CASE("UT03 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"ut03.source", "ut03.match", "ut03.xfade", "ut03.loop", "ut03.sync", "ut03.level"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Source].labels == std::vector<std::string>{"A Mix", "B Ref 1", "C Ref 2"}); CHECK(s[Source].def == 0);
    CHECK(s[LoudnessMatch].def == 1);
    CHECK(s[Crossfade].min == 0); CHECK(s[Crossfade].max == 500); CHECK(s[Crossfade].def == 50);
    CHECK(s[Loop].labels == std::vector<std::string>{"Intro", "Verse", "Chorus", "Custom"}); CHECK(s[Loop].def == 2);
    CHECK(s[Sync].def == 1);
    CHECK(s[Level].min == -24); CHECK(s[Level].max == 24); CHECK(s[Level].def == 0);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("UT03 Source A leaves the input untouched, even with references loaded") {
    auto p = make(); CHECK(load(p, 1, noise(-20, 5.0, 3)));
    const auto x = noise(-18, 3.0, 4); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    CHECK(std::abs(p.matchDb() - 2.0) <= 0.5);   // on Source A the screen shows what B would get
}
TEST_CASE("UT03 decodes WAV (16, 24, float), AIFF and refuses the rest") {
    const auto a = noise(-12, 0.5, 1), b = sine(-12, 0.5, 440);
    for (int bits : {16, 24, 32}) {
        const auto f = wav(a, &b, 44100, bits); Decoded d; REQUIRE(decodeAudio(f.data(), f.size(), d)); CHECK(d.rate == 44100); REQUIRE(d.l.size() == a.size());
        const double tol = bits == 16 ? 1e-4 : 1e-6; for (size_t i = 0; i < a.size(); i += 31) { NEAR(d.l[i], a[i], tol); NEAR(d.r[i], b[i], tol); }
    }
    { const auto f = wav(a, nullptr, 48000, 16); Decoded d; REQUIRE(decodeAudio(f.data(), f.size(), d)); for (size_t i = 0; i < a.size(); i += 31) CHECK(d.l[i] == d.r[i]); }
    { const auto f = aiff16(a, 44100); Decoded d; REQUIRE(decodeAudio(f.data(), f.size(), d)); CHECK(d.rate == doctest::Approx(44100)); REQUIRE(d.l.size() == a.size()); for (size_t i = 0; i < a.size(); i += 31) NEAR(d.l[i], a[i], 1e-4); }
    Decoded d; const uint8_t junk[40] = {1, 2, 3}; CHECK_FALSE(decodeAudio(junk, sizeof junk, d)); CHECK_FALSE(decodeAudio(nullptr, 0, d));
    const uint8_t fl[16] = {'f', 'L', 'a', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; CHECK_FALSE(decodeAudio(fl, sizeof fl, d));
    auto f = wav(a, nullptr, 48000, 16); for (size_t cut : {size_t(5), size_t(20), size_t(43)}) { Decoded e; CHECK_FALSE(decodeAudio(f.data(), cut, e)); }
    f.resize(f.size() - 7); { Decoded e; CHECK(decodeAudio(f.data(), f.size(), e)); }   // a data chunk cut short: the whole frames that are there
    auto p = make(); CHECK_FALSE(p.loadReference(1, junk, sizeof junk)); CHECK_FALSE(p.hasReference(1)); CHECK_FALSE(p.loadReference(3, f.data(), f.size()));
}
TEST_CASE("UT03 resampler keeps frequency and level") {
    for (double from : {44100.0, 96000.0, 22050.0}) {
        std::vector<float> x(static_cast<size_t>(from)); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / from));
        const auto y = resample(x, from, 48000.0); CHECK(std::abs(static_cast<double>(y.size()) - 48000.0) <= 1.0);
        double s = 0; size_t n = 0; for (size_t i = 4800; i + 4800 < y.size(); ++i) { const double e = y[i] - 0.5 * std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / 48000.0); s += e * e; ++n; }
        CHECK(10 * std::log10(s / static_cast<double>(n) + 1e-30) < -70.0);
    }
    // downsampling does not alias: 30 kHz at 96 kHz -> 48 kHz is above 24 kHz and disappears
    std::vector<float> h(96000); for (size_t i = 0; i < h.size(); ++i) h[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 30000.0 * static_cast<double>(i) / 96000.0));
    CHECK(rmsDb(resample(h, 96000.0, 48000.0), 4800, 43000) < -60.0);
}
TEST_CASE("UT03 loudness match: the reference is played as loud as the input") {
    const auto in = noise(-20, 12.0, 5);
    auto p = make({{Source, 1}, {Crossfade, 0}}); CHECK(load(p, 1, noise(-32, 30.0, 6)));
    const auto y = run(p, in);
    CHECK(p.matchDb() == doctest::Approx(12.0).epsilon(0.05));
    CHECK(rmsDb(y, y.size() - 48000, y.size()) == doctest::Approx(-20.0).epsilon(0.03));
    auto q = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}}); CHECK(load(q, 1, noise(-32, 30.0, 6)));
    const auto z = run(q, in); CHECK(rmsDb(z, z.size() - 48000, z.size()) == doctest::Approx(-32.0).epsilon(0.03)); CHECK(q.matchDb() == 0.0);
    CHECK(std::abs(p.referenceLufs(1) - (-25.85)) <= 0.5); CHECK(std::abs(p.inputLufs() - (-13.85)) <= 0.5);   // white noise: K-weighting adds about 6.1 dB to the RMS
}
TEST_CASE("UT03 no match while the input is silent; match is limited to 24 dB") {
    auto p = make({{Source, 1}, {Crossfade, 0}}); CHECK(load(p, 1, noise(-20, 30.0, 6)));
    std::vector<float> z(48000, 0.0f); run(p, z); CHECK(p.matchDb() == 0.0);
    auto q = make({{Source, 1}, {Crossfade, 0}}); CHECK(load(q, 1, noise(-70, 30.0, 6))); run(q, noise(-10, 8.0, 7)); CHECK(q.matchDb() == 24.0);
}
TEST_CASE("UT03 Level trims the reference only") {
    const auto r = noise(-30, 30.0, 8);
    auto a = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}}); load(a, 1, r); const auto ya = run(a, std::vector<float>(48000, 0.0f));
    auto b = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}, {Level, 6}}); load(b, 1, r); const auto yb = run(b, std::vector<float>(48000, 0.0f));
    CHECK(std::abs((rmsDb(yb, 24000, 48000) - rmsDb(ya, 24000, 48000)) - (6.0)) <= 0.1);
    auto c = make({{Level, 12}}); const auto x = noise(-18, 1.0, 9); const auto yc = run(c, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(yc[i] == x[i]);
}
TEST_CASE("UT03 Loop regions: Intro, Verse, Chorus (the loudest 20 s), Custom") {
    std::vector<float> x(static_cast<size_t>(60 * kFs)); const auto lo = noise(-30, 60.0, 1), hi = noise(-10, 60.0, 2);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (i >= 25 * 48000 && i < 45 * 48000) ? hi[i] : lo[i];
    auto p = make(); REQUIRE(load(p, 1, x)); double a, b;
    p.setParam(Loop, Intro); p.regionOf(1, a, b); CHECK(a == 0); CHECK(b == doctest::Approx(20));
    p.setParam(Loop, Verse); p.regionOf(1, a, b); CHECK(a == doctest::Approx(20)); CHECK(b == doctest::Approx(40));
    p.setParam(Loop, Chorus); p.regionOf(1, a, b); CHECK(std::abs((a) - (25)) <= 1.0); CHECK(b - a == doctest::Approx(20));
    p.setParam(Loop, Custom); p.setLoopRegion(3.0, 7.5); p.regionOf(1, a, b); CHECK(a == doctest::Approx(3)); CHECK(b == doctest::Approx(7.5));
    auto q = make(); REQUIRE(load(q, 1, noise(-20, 8.0, 3))); q.setParam(Loop, Chorus); q.regionOf(1, a, b); CHECK(a == 0); CHECK(b == doctest::Approx(8));   // short file: the whole file
    double none = 1; Processor e; e.regionOf(1, none, none); CHECK(none == 0);
}
TEST_CASE("UT03 Sync play follows the host time, silent while the host stands still") {
    const auto r = noise(-20, 30.0, 11);
    auto p = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}, {Loop, Intro}}); REQUIRE(load(p, 1, r));
    // host at 5 s: block output = reference from 5 s (loop Intro = 0..20 s, so no offset)
    std::vector<float> blk(256, 0.0f); std::vector<float> blk2 = blk; float* c[2] = {blk.data(), blk2.data()};
    p.setPlayhead(5.0, true); p.process(c, 2, 256); for (int k = 240; k < 256; ++k) NEAR(blk[static_cast<size_t>(k)], r[static_cast<size_t>(5 * 48000 + k)], 1e-6);
    // next block continues without a jump
    p.setPlayhead(5.0 + 256.0 / 48000, true); std::fill(blk.begin(), blk.end(), 0.0f); std::fill(blk2.begin(), blk2.end(), 0.0f); p.process(c, 2, 256);
    for (int k = 0; k < 256; ++k) NEAR(blk[static_cast<size_t>(k)], r[static_cast<size_t>(5 * 48000 + 256 + k)], 1e-6);
    // wraps at 20 s
    p.setPlayhead(45.0, true); std::fill(blk.begin(), blk.end(), 0.0f); p.process(c, 2, 256);   // a jump: 5 ms fade-in
    CHECK(std::abs(blk[10]) < std::abs(r[static_cast<size_t>(5 * 48000 + 10)]) * 0.2 + 1e-9);
    p.setPlayhead(45.0 + 256.0 / 48000, true); std::fill(blk.begin(), blk.end(), 0.0f); p.process(c, 2, 256);   // 45 s = 5 s into the 20 s loop
    for (int k = 0; k < 256; ++k) NEAR(blk[static_cast<size_t>(k)], r[static_cast<size_t>(5 * 48000 + 256 + k)], 1e-6);
    p.setPlayhead(5.0, false); std::fill(blk.begin(), blk.end(), 0.0f); p.process(c, 2, 256); for (float v : blk) CHECK(v == 0.0f);
}
TEST_CASE("UT03 free running (Sync Off, or a host without time) starts at the loop start and loops") {
    const auto r = noise(-20, 30.0, 12);
    for (int mode = 0; mode < 2; ++mode) {
        auto p = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}, {Loop, Verse}, {Sync, mode == 0 ? 0.0 : 1.0}}); REQUIRE(load(p, 1, r));
        if (mode == 0) p.setPlayhead(7.0, true); else p.setPlayhead(-1.0, true);
        const auto y = run(p, std::vector<float>(48000, 0.0f)); double a, b; p.regionOf(1, a, b); CHECK(a == doctest::Approx(20)); CHECK(b == doctest::Approx(30));
        for (size_t i = 1000; i < 40000; i += 37) NEAR(y[i], r[static_cast<size_t>(20 * 48000) + i], 1e-6);
        // 10 s region: 12 s of audio wraps once and stays finite
        const auto y2 = run(p, std::vector<float>(static_cast<size_t>(12 * kFs), 0.0f)); for (float v : y2) REQUIRE(std::isfinite(v));
    }
}
TEST_CASE("UT03 Crossfade is equal-power; 0 ms switches at once") {
    const auto ref = sine(-20, 20.0, 2000);
    auto p = make({{Crossfade, 100}, {LoudnessMatch, 0}, {Sync, 0}}); REQUIRE(load(p, 1, ref));
    std::vector<float> x = sine(-20, 1.0, 1000); run(p, std::vector<float>(2400, 0.0f)); p.setParam(Source, 1);
    const auto y = run(p, x);
    // middle of the 100 ms ramp (about 50 ms): both tones about -3 dB below their full level (-23 dBFS rms-ish); end: only the reference
    CHECK(std::abs(binDb(y, 1000, 2000, 3000) - (binDb(x, 1000, 2000, 3000) - 3.0)) <= 1.5);
    CHECK(std::abs(binDb(y, 2000, 2000, 3000) - (binDb(ref, 2000, 2000, 3000) - 3.0)) <= 1.5);
    CHECK(binDb(y, 1000, 12000, 48000) < binDb(x, 1000, 12000, 48000) - 70.0);
    auto q = make({{Crossfade, 0}, {LoudnessMatch, 0}, {Sync, 0}}); load(q, 1, ref); q.setParam(Source, 1); q.snapToTargets();
    const auto z = run(q, std::vector<float>(4800, 0.0f)); CHECK(rmsDb(z, 100, 4800) > -25.0);
    q.setParam(Source, 0); const auto z2 = run(q, x); for (size_t i = 10; i < 200; ++i) REQUIRE(z2[i] == x[i]);
}
TEST_CASE("UT03 two references, clearing, mono input, other sample rate, odd blocks") {
    const auto r1 = noise(-20, 30.0, 21), r2 = noise(-26, 30.0, 22);
    auto p = make({{Crossfade, 0}, {LoudnessMatch, 0}, {Sync, 0}}); REQUIRE(load(p, 1, r1)); REQUIRE(load(p, 2, r2)); CHECK(p.hasReference(1)); CHECK(p.hasReference(2));
    p.setParam(Source, 2); p.snapToTargets(); const auto y = run(p, std::vector<float>(48000, 0.0f)); CHECK(std::abs((rmsDb(y, 24000, 48000)) - (-26.0)) <= 1.0);
    p.clearReference(2); CHECK_FALSE(p.hasReference(2)); const auto y0 = run(p, std::vector<float>(4800, 0.0f)); for (float v : y0) CHECK(v == 0.0f);
    { auto m = make({{Source, 1}, {Crossfade, 0}, {LoudnessMatch, 0}, {Sync, 0}}); load(m, 1, r1); std::vector<float> l(9999, 0.0f); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; m.process(c, 1, n); } CHECK(std::abs((rmsDb(l, 1000, 9999)) - (-20.0)) <= 1.5); for (float v : l) REQUIRE(std::isfinite(v)); }
    { Processor q; q.setParam(Source, 1); q.prepare(44100, 256); q.snapToTargets(); const auto w = wav(noise(-20, 20.0, 5), nullptr, 48000, 16); REQUIRE(q.loadReference(1, w.data(), w.size()));
      q.setParam(Source, 1); q.snapToTargets(); const auto v = run(q, noise(-20, 3.0, 6)); for (float s : v) REQUIRE(std::isfinite(s)); q.prepare(96000, 256); const auto v2 = run(q, noise(-20, 1.0, 6)); for (float s : v2) REQUIRE(std::isfinite(s)); }
    { Processor q; std::vector<float> l(256, 0.5f); float* c[1] = {l.data()}; q.process(c, 1, 256); CHECK(l[0] == 0.5f); q.snapToTargets(); q.setPlayhead(1, true); }
}
