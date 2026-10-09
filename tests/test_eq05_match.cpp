// EQ05 Match (EVO, class B): the tone curve of a reference is compared with the input's long-term average (1/6 octave, 10 s of playing) and the EQ's knobs are fitted to the difference by least squares
// (within the knobs' ranges); the values are written to the host. The reference comes from the screen as float samples in base64 pieces (GUI thread); the fit runs on the GUI thread; the audio thread listens and applies.
#include "doctest.h"
#include "eq05/eq05.hpp"
#include "sw/band_spectrum.hpp"
#include "tu.hpp"
#include <cstring>
using namespace tu;
using namespace sw::eq05;
namespace {
std::string b64(const unsigned char* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o;
    for (size_t i = 0; i < n; i += 3) { const unsigned v = (d[i] << 16) | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += i + 1 < n ? t[(v >> 6) & 63] : '='; o += i + 2 < n ? t[v & 63] : '='; }
    return o;
}
void sendReference(Processor& p, const std::vector<float>& x, double rate, size_t piece = 30000) {
    p.refBegin(rate);
    for (size_t off = 0; off < x.size(); off += piece) { const size_t n = std::min(piece, x.size() - off); p.refAppendBase64(b64(reinterpret_cast<const unsigned char*>(x.data() + off), n * sizeof(float)).c_str()); }
    p.refCommit();
}
Processor make(std::vector<std::pair<int, double>> set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
std::vector<float> through(Processor& p, std::vector<float> x, size_t block = 256) {
    std::vector<float> r = x;
    for (size_t off = 0; off < x.size(); off += block) { const int n = static_cast<int>(std::min(block, x.size() - off)); float* c[2] = {x.data() + off, r.data() + off}; p.process(c, 2, n); }
    return x;
}
// the band levels (dB) of a signal, and the difference of two sets with the mean taken out
std::vector<double> bands(const std::vector<float>& x) { sw::BandSpectrum s; s.prepare(kFs); s.start(); for (size_t off = 0; off < x.size(); off += 256) s.process(x.data() + off, static_cast<int>(std::min<size_t>(256, x.size() - off))); std::vector<double> d(sw::BandSpectrum::kBands); s.levels(d.data()); return d; }
double rmsDiff(const std::vector<double>& a, const std::vector<double>& b, int lo = 8, int hi = 52) {   // bands 8 .. 52: 50 Hz .. 12 kHz; the mean difference is taken out (the fit does not set the level)
    double m = 0; for (int i = lo; i < hi; ++i) m += a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]; m /= (hi - lo);
    double s = 0; for (int i = lo; i < hi; ++i) { const double e = a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)] - m; s += e * e; } return std::sqrt(s / (hi - lo));
}
// listen to `x`, fit, collect the writes
struct Out { std::vector<std::pair<int, double>> w; bool fit = false; };
Out runMatch(Processor& p, const std::vector<float>& x, size_t block = 256) {
    Out o; p.match(); through(p, x, block);
    if (p.needsFit()) { p.fit(); o.fit = true; }
    int id; double v; float dummy[2] = {0, 0}; float* c[2] = {dummy, dummy};
    p.process(c, 2, 1);   // (the writes are handed out after a block)
    while (p.takeParamWrite(id, v) == 7) o.w.emplace_back(id, v);
    return o;
}
}  // namespace

TEST_CASE("EQ05 Match: the reference is a signal EQ'd by known settings; the fitted EQ brings the input's tone curve to the reference's within 1 dB") {
    const auto input = noise(-20.0, 14.0, 21), plain = noise(-20.0, 14.0, 22);
    auto trueEq = make({{HfGain, 6.0}, {HfFreq, 9000.0}, {LmfGain, -5.0}, {LmfFreq, 500.0}, {LmfQ, 1.2}, {LfGain, 4.0}, {LfFreq, 80.0}, {HmfGain, 3.0}, {HmfFreq, 3000.0}, {HmfQ, 1.5}, {Drive, 0.0}});
    const auto ref = through(trueEq, plain);
    auto p = make({{Drive, 0.0}});
    CHECK(!p.hasReference());
    sendReference(p, ref, kFs);
    REQUIRE(p.hasReference());
    const auto o = runMatch(p, input);
    REQUIRE(o.fit);
    CHECK(o.w.size() >= 10);
    INFO("before " << p.matchBefore() << " dB, after " << p.matchAfter() << " dB");
    CHECK(p.matchAfter() < 1.0); CHECK(p.matchBefore() > 2.0);
    // what the writes do: a fresh EQ05 with them, on the input, against the reference
    auto fitted = make({{Drive, 0.0}});
    for (const auto& w : o.w) { CHECK(w.first >= 0); CHECK(w.first < kNumParams); fitted.setParam(w.first, w.second); }
    fitted.snapToTargets();
    const auto out = through(fitted, input);
    const double e = rmsDiff(bands(out), bands(ref));
    INFO("fitted EQ vs the reference: " << e << " dB");
    CHECK(e < 1.0);
    CHECK(rmsDiff(bands(input), bands(ref)) > 2.0);   // (it was not already close)
    // the writes are in the knobs' ranges and are not the shapes / frequencies of a different kind of knob
    for (const auto& w : o.w) { const auto& sp = specs()[static_cast<size_t>(w.first)]; CHECK(w.second >= sp.min - 1e-9); CHECK(w.second <= sp.max + 1e-9); }
}

TEST_CASE("EQ05 Match: a target beyond the knobs' ranges stays inside them; without a reference, or without sound, nothing happens; cancel; the same writes whatever the block size; refclear") {
    {   // +30 dB of tilt cannot be reached: the writes are clipped to the ranges and are finite
        const auto plain = noise(-20.0, 14.0, 31); auto x = plain; double lp = 0; for (auto& v : x) { lp += 0.05 * (v - lp); v = static_cast<float>(v + 6.0 * lp); }
        auto p = make(); sendReference(p, x, kFs); const auto o = runMatch(p, plain);
        REQUIRE(o.fit); for (const auto& w : o.w) { const auto& sp = specs()[static_cast<size_t>(w.first)]; CHECK(std::isfinite(w.second)); CHECK(w.second >= sp.min - 1e-9); CHECK(w.second <= sp.max + 1e-9); }
    }
    { auto p = make(); p.match(); CHECK(!p.matching()); }   // no reference: it does not start
    { auto p = make(); sendReference(p, noise(-20.0, 8.0, 5), kFs); p.match(); CHECK(p.matching()); through(p, std::vector<float>(static_cast<size_t>(30 * kFs), 0.0f)); CHECK(p.matching()); CHECK(!p.needsFit()); CHECK(p.matchProgress() == 0.0); }   // silence is not playing
    { auto p = make(); sendReference(p, noise(-20.0, 8.0, 5), kFs); p.match(); through(p, noise(-20.0, 4.0, 6)); CHECK(p.matching()); p.match(); CHECK(!p.matching()); CHECK(!p.needsFit()); }   // pressed again: cancelled
    {   // a reference with a tilt, the input heard in different cuts: the same writes
        const auto plain = noise(-20.0, 14.0, 41); auto ref = plain; double lp = 0; for (auto& v : ref) { lp += 0.3 * (v - lp); v = static_cast<float>(0.5 * v + 0.9 * lp); }
        auto a = make(), b = make(); sendReference(a, ref, kFs); sendReference(b, ref, kFs);
        const auto oa = runMatch(a, plain, 256), ob = runMatch(b, plain, 37);
        REQUIRE(oa.fit); REQUIRE(ob.fit); REQUIRE(oa.w.size() == ob.w.size()); for (size_t i = 0; i < oa.w.size(); ++i) { CHECK(oa.w[i].first == ob.w[i].first); CHECK(oa.w[i].second == ob.w[i].second); }
    }
    { auto p = make(); sendReference(p, noise(-20.0, 8.0, 5), kFs); CHECK(p.hasReference()); p.refClear(); CHECK(!p.hasReference()); }
}

TEST_CASE("EQ05 Match: the high-pass that is set is not part of the fit (the fit makes up for the filters as they are)") {
    const auto input = noise(-20.0, 14.0, 51), plain = noise(-20.0, 14.0, 52);
    auto trueEq = make({{Hpf, 80.0}, {HmfGain, 5.0}, {HmfFreq, 3000.0}, {HmfQ, 1.2}, {Drive, 0.0}});
    const auto ref = through(trueEq, plain);
    {   // the same high-pass is set on the matching EQ: the shelf does not try to be the high-pass
        auto p = make({{Hpf, 80.0}, {Drive, 0.0}}); sendReference(p, ref, kFs); const auto o = runMatch(p, input); REQUIRE(o.fit);
        double lf = 99, hmf = 0; for (const auto& w : o.w) { if (w.first == LfGain) lf = w.second; if (w.first == HmfGain) hmf = w.second; }
        INFO("LF gain " << lf << ", HMF gain " << hmf); CHECK(std::abs(lf) < 1.5); CHECK(hmf > 3.0);
        CHECK(p.matchAfter() < 0.8);
    }
    {   // without it the low cut has to come from the low shelf
        auto p = make({{Drive, 0.0}}); sendReference(p, ref, kFs); const auto o = runMatch(p, input); REQUIRE(o.fit);
        double lf = 99; for (const auto& w : o.w) if (w.first == LfGain) lf = w.second;
        INFO("LF gain " << lf); CHECK(lf < -4.0);
    }
}

TEST_CASE("EQ05 Match: a broken or too short reference is refused; the previous one stays when a new one fails") {
    auto p = make();
    p.refBegin(kFs); CHECK(!p.refAppendBase64("!!not base64!!")); CHECK(!p.refCommit()); CHECK(!p.hasReference());
    sendReference(p, std::vector<float>(static_cast<size_t>(0.05 * kFs), 0.1f), kFs); CHECK(!p.hasReference());   // 50 ms
    sendReference(p, std::vector<float>(static_cast<size_t>(5 * kFs), 0.0f), kFs); CHECK(!p.hasReference());      // silence
    sendReference(p, noise(-20.0, 6.0, 9), kFs); CHECK(p.hasReference());
    p.refBegin(kFs); CHECK(!p.refAppendBase64("@@@")); CHECK(!p.refCommit());
    CHECK(p.hasReference());   // (the failed one did not take the last one away)
    sendReference(p, noise(-20.0, 6.0, 9), 44100.0); CHECK(p.hasReference());   // another rate is analysed at its own rate
}

TEST_CASE("EQ05 Match: the reference may be given as its long-term spectrum (UT03's, over SW Link): the same writes as the file it was made from; values that cannot be a spectrum are refused") {
    const auto input = noise(-20.0, 14.0, 61), plain = noise(-20.0, 14.0, 62);
    auto trueEq = make({{HfGain, 5.0}, {HfFreq, 7000.0}, {LfGain, -4.0}, {LfFreq, 120.0}, {HmfGain, 3.0}, {HmfFreq, 2500.0}, {HmfQ, 1.4}, {Drive, 0.0}});
    const auto ref = through(trueEq, plain);
    auto a = make({{Drive, 0.0}}), b = make({{Drive, 0.0}});
    sendReference(a, ref, kFs);
    const auto bands60 = bands(ref);   // what UT03 would share: BandSpectrum of the file
    CHECK(!b.hasReference());
    REQUIRE(b.refFromBands(bands60.data())); CHECK(b.hasReference()); CHECK(b.refLoadsDone() == 1);
    const auto oa = runMatch(a, input), ob = runMatch(b, input);
    REQUIRE(oa.fit); REQUIRE(ob.fit); REQUIRE(oa.w.size() == ob.w.size());
    for (size_t i = 0; i < oa.w.size(); ++i) { CHECK(oa.w[i].first == ob.w[i].first); CHECK(oa.w[i].second == doctest::Approx(ob.w[i].second).epsilon(1e-9)); }
    CHECK(b.matchAfter() < 1.0);
    // refused: no values, NaN, nothing but the floor; the reference in place stays, and the failures are counted
    auto c = make(); REQUIRE(c.refFromBands(bands60.data()));
    CHECK(!c.refFromBands(nullptr)); std::vector<double> bad = bands60; bad[10] = std::nan(""); CHECK(!c.refFromBands(bad.data()));
    std::vector<double> floor(bands60.size(), -300.0); CHECK(!c.refFromBands(floor.data()));
    CHECK(c.hasReference()); CHECK(c.refLoadsFailed() == 3); CHECK(c.refLoadsDone() == 1);
}
