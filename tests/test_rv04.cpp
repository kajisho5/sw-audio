#include "doctest.h"
#include "rv04/rv04.hpp"
#include "tu.hpp"
#include <atomic>
#include <cstring>
#include <thread>
using namespace sw;
using namespace sw::rv04;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}, double bpm = 0) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); if (bpm > 0) p.setTempo(bpm); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; return run(p, x); }
double crossing(const std::vector<float>& h, double db) {
    double tot = 0; for (float v : h) tot += double(v) * v; double e = tot;
    for (size_t i = 0; i < h.size(); ++i) { if (10 * std::log10(e / tot + 1e-30) <= -db) return static_cast<double>(i) / kFs; e -= double(h[i]) * h[i]; }
    return static_cast<double>(h.size()) / kFs;
}
double rt60(const std::vector<float>& h) { return 3.0 * (crossing(h, 30.0) - crossing(h, 10.0)); }
double energy(const std::vector<float>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += double(h[i]) * h[i]; return e; }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
Set clean(int cat) { return {{Category, double(cat)}, {PreDelay, 0}, {LowCut, 20}, {HighCut, 20000}, {Length, 100}, {Size, 100}, {Reverse, 0}}; }
}

double ratio(double a, double b) { return a / b; }
#define NEAR_RATIO(a, b, expected) (std::abs(ratio((a), (b)) - (expected)) < 0.15)

TEST_CASE("RV04 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv04.category", "rv04.predelay", "rv04.length", "rv04.size", "rv04.lowcut", "rv04.highcut", "rv04.reverse", "rv04.mix", "rv04.evo.on"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Category].labels == std::vector<std::string>{"Halls", "Rooms", "Churches", "Gear", "Custom"}); CHECK(s[Category].def == 0);
    CHECK(s[PreDelay].max == 500); CHECK(s[PreDelay].def == 12); CHECK(s[PreDelay].curve == Curve::Skew); CHECK(s[PreDelay].skew == 2);
    CHECK(s[Length].min == 10); CHECK(s[Length].max == 100); CHECK(s[Length].def == 100); CHECK(s[Size].min == 50); CHECK(s[Size].max == 150); CHECK(s[Size].def == 100);
    CHECK(s[LowCut].def == 80); CHECK(s[HighCut].def == 12000); CHECK(s[Reverse].def == 0); CHECK(s[Mix].def == 20); CHECK(s[BarFit].def == 0);
}
TEST_CASE("RV04 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV04 the categories have their own reverberation times, and the same level (unit energy)") {
    struct C { int cat; double rt, sec; } cs[] = {{Halls, 3.0, 6.0}, {Rooms, 0.8, 2.5}, {Churches, 6.5, 9.0}, {Gear, 2.5, 4.0}};   // broadband T20 -> 60 dB, measured; the band tables ask for 2.6 / 0.65 / 5.5 / 2.2 at 1 kHz
    for (auto c : cs) {
        auto p = make(clean(c.cat)); const auto h = impulse(p, c.sec);
        NEAR(rt60(h) / c.rt, 1.0, 0.2);
        double e = 0; for (float v : h) e += double(v) * v; NEAR(10 * std::log10(e), 0.0, 1.0);
    }
}
TEST_CASE("RV04 Length cuts the tail with a fade") {
    auto full = make(clean(Halls)); auto half = make([&] { auto s = clean(Halls); for (auto& kv : s) if (kv.first == Length) kv.second = 50; return s; }());
    const auto hf = impulse(full, 6.0), hh = impulse(half, 6.0);
    const double natural = 5.0;
    CHECK(energy(hh, 0.52 * natural, 6.0) < energy(hh, 0.0, 6.0) * 1e-7);
    CHECK(energy(hf, 0.52 * natural, 6.0) > energy(hf, 0.0, 6.0) * 1e-6);
}
TEST_CASE("RV04 Size stretches the IR: 150 % is 1.5 times as long, 50 % half") {
    auto a = make(clean(Halls)); auto b = make([&] { auto s = clean(Halls); for (auto& kv : s) if (kv.first == Size) kv.second = 150; return s; }()); auto c = make([&] { auto s = clean(Halls); for (auto& kv : s) if (kv.first == Size) kv.second = 50; return s; }());
    const double r0 = rt60(impulse(a, 6.0)), r1 = rt60(impulse(b, 8.0)), r2 = rt60(impulse(c, 4.0));
    NEAR(r1 / r0, 1.5, 0.2); NEAR(r2 / r0, 0.5, 0.15);
}
TEST_CASE("RV04 Reverse puts the tail first") {
    auto off = make(clean(Halls)); auto on = make([&] { auto s = clean(Halls); for (auto& kv : s) if (kv.first == Reverse) kv.second = 1; return s; }());
    const auto h0 = impulse(off, 6.0), h1 = impulse(on, 6.0);
    CHECK(energy(h0, 0.0, 0.5) > 10.0 * energy(h0, 4.0, 4.5)); CHECK(energy(h1, 4.5, 5.0) > 10.0 * energy(h1, 0.0, 0.5));
}
TEST_CASE("RV04 Pre-delay: nothing before it") {
    auto p = make([&] { auto s = clean(Rooms); for (auto& kv : s) if (kv.first == PreDelay) kv.second = 80; return s; }()); const auto h = impulse(p, 1.0);
    double pk = 0; for (size_t i = 0; i < static_cast<size_t>(0.079 * kFs); ++i) pk = std::max(pk, double(std::abs(h[i]))); CHECK(pk < 1e-6);
    double after = 0; for (size_t i = static_cast<size_t>(0.08 * kFs); i < static_cast<size_t>(0.2 * kFs); ++i) after = std::max(after, double(std::abs(h[i]))); CHECK(after > 1e-3);
}
TEST_CASE("RV04 Low cut and High cut") {
    auto flat = make(clean(Rooms)); auto cut = make([&] { auto s = clean(Rooms); for (auto& kv : s) { if (kv.first == LowCut) kv.second = 1000; if (kv.first == HighCut) kv.second = 2000; } return s; }());
    const auto x = noise(-20, 2.0), a = run(flat, x), b = run(cut, x); const size_t t0 = 24000, t1 = x.size();
    CHECK(band(b, 80, 160, t0, t1) < band(a, 80, 160, t0, t1) - 20.0);
    CHECK(band(b, 8000, 12000, t0, t1) < band(a, 8000, 12000, t0, t1) - 20.0);
}
TEST_CASE("RV04 a loaded IR plays as it is (Custom), through all the tiers") {
    Gauss g(7); const size_t N = 60000; std::vector<float> ir(N); for (size_t i = 0; i < N; ++i) ir[i] = static_cast<float>(0.01 * g.gauss() * std::exp(-static_cast<double>(i) / 20000.0));
    ir[100] = 0.5f; ir[5000] = -0.3f; ir[20000] = 0.2f;
    auto p = make([&] { auto s = clean(Custom); return s; }()); p.loadIr(ir.data(), N, 1, kFs); p.snapToTargets();
    CHECK(p.irLoaded());
    // the engine normalises the energy; compare shapes: the peaks keep their ratio and place
    const auto h = impulse(p, 2.0);
    CHECK(std::abs(h[100]) > 10.0 * std::abs(h[99]));   // (the 20 kHz low-pass of the output stage smears the peak by a sample or two) CHECK(NEAR_RATIO(h[5000], h[100], -0.6));
    CHECK(NEAR_RATIO(h[20000], h[100], 0.4));
}
TEST_CASE("RV04 Custom without an IR passes the signal through; IR state round-trips") {
    auto p = make(clean(Custom)); const auto x = sine(-20, 0.5, 500.0), y = run(p, x);
    double e = 0, d = 0; for (size_t i = 0; i < x.size(); ++i) { e += double(x[i]) * x[i]; d += std::pow(double(y[i]) - x[i], 2); } CHECK(d < 1e-2 * e);
    std::vector<float> ir(3000, 0.0f); ir[10] = 1.0f; ir[2000] = 0.5f;
    auto q = make(clean(Custom)); q.loadIr(ir.data(), ir.size(), 1, kFs); q.snapToTargets();
    std::vector<uint8_t> st; q.saveExtra(st); CHECK(st.size() > 12000);
    auto r = make(clean(Custom)); r.loadExtra(st.data(), st.size()); r.snapToTargets(); CHECK(r.irLoaded());
    const auto hq = impulse(q, 0.3), hr = impulse(r, 0.3); for (size_t i = 0; i < hq.size(); ++i) CHECK(std::abs(hq[i] - hr[i]) < 1e-6);
}
TEST_CASE("RV04 Bar fit: the length snaps to whole beats, with a fade") {
    // 120 bpm: a beat is 0.5 s. Halls are 5 s long; Length 60 % asks for 3 s -> the largest of 1/2/4/8/16/32 beats not above that: 4 beats = 2 s
    auto set = clean(Halls); for (auto& kv : set) if (kv.first == Length) kv.second = 60; set.push_back({BarFit, 1});
    auto p = make(set, 120.0); const auto h = impulse(p, 6.0);
    NEAR(p.irSeconds(), 2.0, 0.02);
    CHECK(energy(h, 2.05, 6.0) < energy(h, 0.0, 6.0) * 1e-5);
    auto q = make(set); NEAR(q.irSeconds(), 3.0, 0.02);   // no tempo: the Length as it is
}
TEST_CASE("RV04 changing a setting builds the new IR in steps without stopping the audio") {
    auto p = make(clean(Rooms)); run(p, noise(-20, 0.5));
    p.setParam(Category, Halls); p.setParam(Size, 120);
    double worst = 0; for (int k = 0; k < 400; ++k) { auto o = run(p, noise(-30, 0.05, static_cast<unsigned>(k + 2))); for (float v : o) { CHECK(std::isfinite(v)); worst = std::max(worst, double(std::abs(v))); } }
    CHECK(worst < 4.0);
    run(p, std::vector<float>(static_cast<size_t>(10 * kFs), 0.0f));
    auto fresh = make([&] { auto s = clean(Halls); for (auto& kv : s) if (kv.first == Size) kv.second = 120; return s; }());
    p.setParam(Length, 100); p.setParam(PreDelay, 0); p.setParam(LowCut, 20); p.setParam(HighCut, 20000); p.setParam(Reverse, 0);
    run(p, std::vector<float>(static_cast<size_t>(3 * kFs), 0.0f));
    const auto a = impulse(p, 2.0), b = impulse(fresh, 2.0);
    double worstd = 0, mx = 0; for (size_t i = 0; i < a.size(); ++i) { worstd = std::max(worstd, double(std::abs(a[i] - b[i]))); mx = std::max(mx, double(std::abs(b[i]))); }
    CHECK(worstd < 1e-3 * mx + 1e-6);
}

namespace {
std::string b64(const std::vector<uint8_t>& v, size_t a, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o;
    for (size_t i = a; i < a + n; i += 3) { const uint32_t x = (uint32_t(v[i]) << 16) | (i + 1 < a + n ? uint32_t(v[i + 1]) << 8 : 0) | (i + 2 < a + n ? v[i + 2] : 0); o += T[x >> 18]; o += T[(x >> 12) & 63]; o += i + 1 < a + n ? T[(x >> 6) & 63] : '='; o += i + 2 < a + n ? T[x & 63] : '='; }
    return o;
}
std::vector<uint8_t> floatBytes(const std::vector<float>& f) { std::vector<uint8_t> b(f.size() * 4); std::memcpy(b.data(), f.data(), b.size()); return b; }
}
TEST_CASE("RV04 an IR sent from the screen (float samples as base64 pieces) is the same as loadIr") {
    std::vector<float> ir(6000, 0.0f); ir[10] = 1.0f; ir[2000] = 0.5f; ir[5000] = -0.25f;
    auto a = make(clean(Custom)); a.loadIr(ir.data(), ir.size(), 1, kFs); a.snapToTargets();
    auto b = make(clean(Custom));
    CHECK_FALSE(b.irAppendBase64("AAAA")); CHECK_FALSE(b.irCommit());   // nothing open
    CHECK_FALSE(b.irBegin(3, kFs)); CHECK_FALSE(b.irBegin(1, 0.0));
    REQUIRE(b.irBegin(1, kFs)); const auto by = floatBytes(ir);
    for (size_t off = 0; off < by.size(); off += 3000) REQUIRE(b.irAppendBase64(b64(by, off, std::min<size_t>(3000, by.size() - off)).c_str()));
    REQUIRE(b.irCommit()); b.snapToTargets(); CHECK(b.irLoaded()); CHECK(b.irLoadsDone() == 1); CHECK(b.irLoadsFailed() == 0);
    const auto ha = impulse(a, 0.3), hb = impulse(b, 0.3); for (size_t i = 0; i < ha.size(); ++i) REQUIRE(std::abs(ha[i] - hb[i]) < 1e-6);
    // stereo, and a file whose size does not fit its channels or whose text is damaged: refused, the IR in place stays
    { std::vector<float> st(2 * 1000, 0.1f); const auto sb = floatBytes(st); REQUIRE(b.irBegin(2, 44100.0)); REQUIRE(b.irAppendBase64(b64(sb, 0, sb.size()).c_str())); CHECK(b.irCommit()); b.snapToTargets(); CHECK(b.irLoaded()); }
    REQUIRE(b.irBegin(2, kFs)); REQUIRE(b.irAppendBase64(b64(by, 0, 12).c_str())); CHECK(b.irAppendBase64(b64(by, 0, 3).c_str())); CHECK_FALSE(b.irCommit()); CHECK(b.irLoadsFailed() == 1); CHECK(b.irLoaded());
    REQUIRE(b.irBegin(1, kFs)); CHECK_FALSE(b.irAppendBase64("AB!D")); CHECK_FALSE(b.irCommit()); CHECK(b.irLoadsFailed() == 2);
    REQUIRE(b.irBegin(1, kFs)); b.irAbort(); CHECK_FALSE(b.irCommit());
}
TEST_CASE("RV04 an IR can be loaded while the audio thread plays (the screen's thread against the audio thread)") {
    Gauss g(11); std::vector<float> ir1(30000), ir2(50000); for (size_t i = 0; i < ir1.size(); ++i) ir1[i] = static_cast<float>(0.02 * g.gauss() * std::exp(-double(i) / 8000.0)); for (size_t i = 0; i < ir2.size(); ++i) ir2[i] = static_cast<float>(0.02 * g.gauss() * std::exp(-double(i) / 12000.0));
    auto p = make(clean(Custom)); p.loadIr(ir1.data(), ir1.size(), 1, kFs); p.snapToTargets();
    std::atomic<bool> stop{false};
    std::thread t([&] { for (int i = 0; i < 15; ++i) { const auto& v = (i & 1) ? ir1 : ir2; p.loadIr(v.data(), v.size(), 1, kFs); } stop = true; });
    std::vector<float> l(256), r(256); size_t blocks = 0; bool finite = true; double mx = 0;
    while (!stop || blocks < 300) {
        for (size_t i = 0; i < 256; ++i) l[i] = r[i] = static_cast<float>(0.1 * std::sin(0.01 * static_cast<double>(blocks * 256 + i)));
        float* ch[2] = {l.data(), r.data()}; p.process(ch, 2, 256);
        for (size_t i = 0; i < 256; ++i) { finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]); mx = std::max(mx, static_cast<double>(std::abs(l[i]))); }
        ++blocks;
    }
    t.join(); CHECK(finite); CHECK(mx < 4.0); CHECK(p.irLoaded());
}
