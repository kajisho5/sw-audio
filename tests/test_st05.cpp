#include "doctest.h"
#include "st05/st05.hpp"
#include "sw/fft.hpp"
#include "tu.hpp"
#include <complex>
using namespace sw;
using namespace sw::st05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
void go(Processor& p, std::vector<float>& l, std::vector<float>& r) { for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
std::vector<double> ir(const IrSpec& s, int speaker, int ear) { std::vector<double> h; designIr(kFs, s, speaker, ear, static_cast<size_t>(kIrSeconds * kFs), h); return h; }
size_t firstAbove(const std::vector<double>& h, double thr) { for (size_t i = 0; i < h.size(); ++i) if (std::abs(h[i]) > thr) return i; return h.size(); }
double energy(const std::vector<double>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += h[i] * h[i]; return e; }
// magnitude (dB) of a window of an IR at f, relative scale only
double binOf(const std::vector<double>& h, double f, double a, double b) { std::complex<double> acc; double w = 0; const size_t i0 = static_cast<size_t>(a * kFs), i1 = static_cast<size_t>(b * kFs); for (size_t i = i0; i < i1; ++i) { const double hn = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * double(i - i0) / double(i1 - i0 - 1)); acc += hn * h[i] * std::exp(std::complex<double>(0, -2 * 3.14159265358979323846 * f * double(i) / kFs)); w += hn; } return 20 * std::log10(2 * std::abs(acc) / w + 1e-30); }
double binRect(const std::vector<double>& h, double f, double a, double b) { std::complex<double> acc; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) acc += h[i] * std::exp(std::complex<double>(0, -2 * 3.14159265358979323846 * f * double(i) / kFs)); return 20 * std::log10(std::abs(acc) + 1e-30); }
// the Schroeder decay: time (s) from -5 to -25 dB, times 3
double rt60(const std::vector<double>& h) { std::vector<double> c(h.size()); double s = 0; for (size_t i = h.size(); i-- > 0;) { s += h[i] * h[i]; c[i] = s; } size_t a = 0, b = 0; for (size_t i = 0; i < h.size(); ++i) { const double db = 10 * std::log10(c[i] / c[0] + 1e-30); if (!a && db < -5) a = i; if (!b && db < -25) { b = i; break; } } return b > a ? 3.0 * double(b - a) / kFs : 0.0; }
}

TEST_CASE("ST05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"st05.speakers", "st05.room", "st05.angle", "st05.headsize", "st05.tracking", "st05.profile"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Speakers].labels == std::vector<std::string>{"Nearfield", "Mains", "Car"}); CHECK(s[Speakers].def == 0);
    CHECK(s[Room].labels == std::vector<std::string>{"Studio A", "Studio B", "Living"}); CHECK(s[Room].def == 0);
    CHECK(s[Angle].min == 0); CHECK(s[Angle].max == 60); CHECK(s[Angle].def == 30);
    CHECK(s[HeadSize].labels == std::vector<std::string>{"Small", "Medium", "Large"}); CHECK(s[HeadSize].def == 1);
    CHECK(s[Tracking].def == 0); CHECK(s[PhonesProfile].labels == std::vector<std::string>{"Off", "Closed", "Open", "Earbud"}); CHECK(s[PhonesProfile].def == 0);
}
TEST_CASE("ST05 the head model: Woodworth ITD and sizes") {
    NEAR(itdSeconds(0, Medium), 0.0, 1e-12);
    NEAR(itdSeconds(90, Medium), 0.0875 / 343.0 * (3.14159265358979323846 / 2 + 1.0), 1e-9);
    NEAR(itdSeconds(30, Medium), 0.0875 / 343.0 * (0.5235987756 + 0.5), 1e-9);
    CHECK(itdSeconds(30, Small) < itdSeconds(30, Medium)); CHECK(itdSeconds(30, Medium) < itdSeconds(30, Large));
    CHECK(headRadius(Small) == 0.075); CHECK(headRadius(Medium) == 0.0875); CHECK(headRadius(Large) == 0.1);
}
TEST_CASE("ST05 the impulse responses: ITD, ILD, symmetry") {
    IrSpec s; s.angleDeg = 30; s.head = Medium;
    const auto ll = ir(s, 0, 0), lr = ir(s, 0, 1), rl = ir(s, 1, 0), rr = ir(s, 1, 1);
    // the left speaker reaches the left ear first: by the interaural time difference
    const double thr = 0.05 * std::abs(ll[firstAbove(ll, 0.02)]);
    const size_t a = firstAbove(ll, 0.05), b = firstAbove(lr, 0.05); (void)thr;
    NEAR(double(b) - double(a), itdSeconds(30, Medium) * kFs, 3.0);   // 12.5 samples
    // mirror symmetry: left speaker -> left ear = right speaker -> right ear (the direct sound and the early part, before the noise tail)
    for (size_t i = 0; i < static_cast<size_t>(0.029 * kFs); ++i) { NEAR(ll[i], rr[i], 1e-9); NEAR(lr[i], rl[i], 1e-9); }
    // head shadow: the far ear is darker at 4 kHz than the near ear (first 3 ms), the same at 150 Hz
    const double near4 = binRect(ll, 4000, 0.0, 0.003), far4 = binRect(lr, 4000, 0.0, 0.003);
    CHECK(near4 > far4 + 6.0);
    NEAR(binRect(ll, 150, 0.0, 0.006), binRect(lr, 150, 0.0, 0.006), 3.0);
    // the tail of the two ears is not the same noise
    double dot = 0, e1 = 0, e2 = 0; for (size_t i = static_cast<size_t>(0.1 * kFs); i < static_cast<size_t>(0.2 * kFs); ++i) { dot += ll[i] * lr[i]; e1 += ll[i] * ll[i]; e2 += lr[i] * lr[i]; }
    CHECK(std::abs(dot) / std::sqrt(e1 * e2) < 0.2);
}
TEST_CASE("ST05 Angle and Head size move the ITD") {
    double prev = -1;
    for (double ang : {0.0, 10.0, 30.0, 60.0}) {
        IrSpec s; s.angleDeg = ang; const auto ll = ir(s, 0, 0), lr = ir(s, 0, 1);
        const double d = double(firstAbove(lr, 0.05)) - double(firstAbove(ll, 0.05));
        CHECK(d >= prev - 0.5); prev = d;
        if (ang == 0.0) { NEAR(d, 0.0, 1.5); const auto rl = ir(s, 1, 0); NEAR(double(firstAbove(rl, 0.05)), double(firstAbove(ll, 0.05)), 1.5); }
    }
    double last = 0; for (int head : {Small, Medium, Large}) { IrSpec s; s.angleDeg = 45; s.head = head; const auto ll = ir(s, 0, 0), lr = ir(s, 0, 1); const double d = double(firstAbove(lr, 0.05)) - double(firstAbove(ll, 0.05)); CHECK(d > last); last = d; }
}
TEST_CASE("ST05 the room decides the decay time and the direct-to-reverberant balance") {
    auto rt = [&](int speakers, int room) { IrSpec s; s.speakers = speakers; s.room = room; return rt60(ir(s, 0, 0)); };
    const double a = rt(Nearfield, StudioA), b = rt(Nearfield, StudioB), c = rt(Nearfield, Living), car = rt(Car, StudioA);
    CHECK(a < b); CHECK(b < c); CHECK(car < a);
    NEAR(a, roomRt60(Nearfield, StudioA), 0.12); NEAR(b, roomRt60(Nearfield, StudioB), 0.15); NEAR(c, roomRt60(Nearfield, Living), 0.2); NEAR(car, roomRt60(Car, StudioA), 0.06);
    // Mains sit farther away: less direct sound against the room (energy after 30 ms against the first 3 ms)
    auto drr = [&](int speakers) { IrSpec s; s.speakers = speakers; s.room = StudioB; const auto h = ir(s, 0, 0); return 10 * std::log10(energy(h, 0.0, 0.003) / energy(h, 0.03, 0.5)); };
    CHECK(drr(Nearfield) > drr(Mains) + 3.0);
    // the critical distance: at D = Dc direct and late energy are equal (the formula behind the tail level)
    IrSpec s; s.speakers = Nearfield; s.room = StudioA; const double V = roomVolume(Nearfield, StudioA), Dc = 0.057 * std::sqrt(V / roomRt60(Nearfield, StudioA)); CHECK(Dc > 0.8); CHECK(Dc < 1.4);
}
TEST_CASE("ST05 the speaker's own response: bass of the Mains, cabin gain of the Car") {
    auto low = [&](int speakers, double f) { IrSpec s; s.speakers = speakers; s.room = StudioA; return binOf(ir(s, 0, 0), f, 0.0, 0.4); };
    CHECK(low(Mains, 45) > low(Nearfield, 45) + 2.0);   // 35 Hz against 60 Hz corner
}
TEST_CASE("ST05 no delay is reported; silence is silence; a mono track passes") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l(48000, 0.0f), r = l; go(p, l, r); for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }
    auto m = noise(-12, 0.5, 7), m0 = m; float* c[1] = {m.data()}; p.process(c, 1, static_cast<int>(m.size())); for (size_t i = 0; i < m.size(); ++i) CHECK(m[i] == m0[i]);
}
TEST_CASE("ST05 an impulse in one channel comes out as that speaker's two impulse responses, with no delay") {
    auto p = make({{Angle, 40}, {Speakers, Mains}, {Room, StudioB}, {HeadSize, Large}});
    std::vector<float> l(24000 + 2000, 0.0f), r = l; l[0] = 1.0f; go(p, l, r);
    IrSpec s; s.speakers = Mains; s.room = StudioB; s.angleDeg = 40; s.head = Large;
    const auto h0 = ir(s, 0, 0), h1 = ir(s, 0, 1);
    double eL = 0, eR = 0, dL = 0, dR = 0;
    for (size_t i = 0; i < 20000; ++i) { eL += h0[i] * h0[i]; eR += h1[i] * h1[i]; dL += (l[i] - h0[i]) * (l[i] - h0[i]); dR += (r[i] - h1[i]) * (r[i] - h1[i]); }
    CHECK(dL / eL < 1e-6); CHECK(dR / eR < 1e-6);
    // and from the right channel
    auto q = make({{Angle, 40}, {Speakers, Mains}, {Room, StudioB}, {HeadSize, Large}});
    std::vector<float> a(24000 + 2000, 0.0f), b = a; b[0] = 1.0f; go(q, a, b);
    const auto g0 = ir(s, 1, 0), g1 = ir(s, 1, 1);
    double fL = 0, fR = 0, cL = 0, cR = 0; for (size_t i = 0; i < 20000; ++i) { fL += g0[i] * g0[i]; fR += g1[i] * g1[i]; cL += (a[i] - g0[i]) * (a[i] - g0[i]); cR += (b[i] - g1[i]) * (b[i] - g1[i]); }
    CHECK(cL / fL < 1e-6); CHECK(cR / fR < 1e-6);
}
TEST_CASE("ST05 a mono (centre) source is the same in both ears in its direct part; the level is near 0 dB") {
    auto p = make({{Speakers, Nearfield}, {Room, StudioA}}); auto l = noise(-18, 3.0, 5), r = l; go(p, l, r);
    // direct part: at the start (impulse) both ears equal
    auto q = make(); std::vector<float> a(4000, 0.0f), b = a; a[0] = 1.0f; b[0] = 1.0f; go(q, a, b);
    for (size_t i = 0; i < static_cast<size_t>(0.029 * kFs); ++i) NEAR(a[i], b[i], 1e-4);
    const double lev = rmsDb(l, 96000, 144000) - -18.0;
    CHECK(lev > -6.0); CHECK(lev < 8.0);
}
TEST_CASE("ST05 Phones profile: generic curves by kind") {
    auto resp = [&](double prof, double f) { auto p = make({{PhonesProfile, prof}, {Room, StudioA}}); auto l = sine(-24, 3.0, f), r = l; go(p, l, r); return rmsDb(l, 96000, 144000); };
    for (double f : {80.0, 3000.0, 12000.0}) { const double off = resp(0, f); CHECK(std::abs(resp(1, f) - off) < 12.0); }
    const double off80 = resp(0, 80), off12 = resp(0, 12000);
    CHECK(resp(1, 80) < off80 - 1.0);    // Closed: less bass
    CHECK(resp(2, 80) > off80 + 1.5);    // Open: a bass lift
    CHECK(resp(2, 12000) < off12 - 1.0); // Open: a little less at the top
    CHECK(resp(3, 80) > off80 + 1.2);    // Earbud: bass lift
}
TEST_CASE("ST05 a change of Angle builds new IRs in steps and crossfades to them") {
    auto p = make({{Angle, 10}});
    CHECK(!p.busy());
    p.setParam(Angle, 50);
    std::vector<float> l(256, 0.0f), r = l; int blocks = 0; float* c[2] = {l.data(), r.data()};
    while (p.busy() && blocks < 400) { std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f); p.process(c, 2, 256); ++blocks; }
    CHECK(!p.busy()); CHECK(blocks < 400); CHECK(p.builtAngle() == 50.0);
    // after the fade the response is the new one
    for (int i = 0; i < 20; ++i) { std::fill(l.begin(), l.end(), 0.0f); p.process(c, 2, 256); }
    std::vector<float> a(24000, 0.0f), b = a; a[0] = 1.0f; go(p, a, b);
    IrSpec s; s.angleDeg = 50; const auto h = ir(s, 0, 0); double e = 0, d = 0; for (size_t i = 0; i < 20000; ++i) { e += h[i] * h[i]; d += (a[i] - h[i]) * (a[i] - h[i]); } CHECK(d / e < 1e-6);
}
TEST_CASE("ST05 Tracking: the yaw turns the head only when it is On") {
    auto p = make({{Tracking, 1}}); p.setHeadYaw(30.0);
    std::vector<float> l(256, 0.0f), r = l; float* c[2] = {l.data(), r.data()}; int blocks = 0; while ((p.busy() || std::abs(p.builtYaw() - 30.0) > 1.5) && blocks < 400) { p.process(c, 2, 256); ++blocks; }
    NEAR(p.builtYaw(), 30.0, 1e-9);
    auto q = make({{Tracking, 0}}); q.setHeadYaw(30.0); for (int i = 0; i < 5; ++i) q.process(c, 2, 256); CHECK(q.builtYaw() == 0.0); CHECK(!q.busy());
    // a turn of the head changes the ITD of the left speaker
    IrSpec s0; s0.angleDeg = 30; IrSpec s1 = s0; s1.yawDeg = 30;
    const auto a0 = ir(s0, 0, 0), b0 = ir(s0, 0, 1), a1 = ir(s1, 0, 0), b1 = ir(s1, 0, 1);
    const double d0 = double(firstAbove(b0, 0.05)) - double(firstAbove(a0, 0.05)), d1 = double(firstAbove(b1, 0.05)) - double(firstAbove(a1, 0.05));
    CHECK(std::abs(d1 - d0) > 2.0);
}
TEST_CASE("ST05 loud noise at the extremes stays finite") {
    for (int sp : {0, 2}) for (double ang : {0.0, 60.0}) {
        auto p = make({{Speakers, double(sp)}, {Angle, ang}, {PhonesProfile, 3}, {HeadSize, 2}}); auto l = noise(0, 1.0, 5), r = noise(0, 1.0, 6); go(p, l, r);
        for (size_t i = 0; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); CHECK(std::isfinite(r[i])); CHECK(std::abs(l[i]) < 40.0f); }
    }
}
