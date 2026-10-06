#include "doctest.h"
#include "rv07/rv07.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::rv07;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
struct Ir { std::vector<float> l, r; };
Ir impulse(Processor& p, double sec) { std::vector<float> x(static_cast<size_t>(sec * kFs), 0.0f); x[0] = 1.0f; auto o = run2(p, x, x); return {o.first, o.second}; }
double energy(const std::vector<float>& h, double a, double b) { double e = 0; for (size_t i = static_cast<size_t>(a * kFs); i < std::min(h.size(), static_cast<size_t>(b * kFs)); ++i) e += double(h[i]) * h[i]; return e; }
double peak(const std::vector<float>& h, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(h.size(), b); ++i) m = std::max(m, double(std::abs(h[i]))); return m; }
double band(const std::vector<float>& y, double f0, double f1, size_t a, size_t b) { double s = 0; int n = 0; for (double f = f0; f <= f1; f *= 1.1) { s += std::pow(10.0, binDb(y, f, a, b) / 10.0); ++n; } return 10 * std::log10(s / n + 1e-30); }
Set room(double dist = 3.5, double ang = 0, int size = 1, int wall = Wood, int use = Instrument) { return {{Distance, dist}, {Angle, ang}, {RoomSize, double(size)}, {Wall, double(wall)}, {Use, double(use)}}; }
}

TEST_CASE("RV07 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    const char* ids[] = {"rv07.use", "rv07.distance", "rv07.angle", "rv07.roomsize", "rv07.wall"};
    for (int i = 0; i < kNumParams; ++i) CHECK(std::string(s[static_cast<size_t>(i)].id) == ids[i]);
    CHECK(s[Use].labels == std::vector<std::string>{"Dialog", "Instrument", "Foley"}); CHECK(s[Use].def == 0);
    CHECK(s[Distance].min == 0.5); CHECK(s[Distance].max == 20); CHECK(s[Distance].def == 3.5); CHECK(s[Distance].curve == Curve::Log);
    CHECK(s[Angle].min == -90); CHECK(s[Angle].max == 90); CHECK(s[Angle].def == 15);
    CHECK(s[RoomSize].labels == std::vector<std::string>{"Small", "Medium", "Large"}); CHECK(s[RoomSize].def == 1);
    CHECK(s[Wall].labels == std::vector<std::string>{"Wood", "Concrete", "Glass", "Curtain"}); CHECK(s[Wall].def == 0);
}
TEST_CASE("RV07 no delay is reported; silence is silence") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> z(48000, 0.0f); for (float v : run(p, z)) CHECK(v == 0.0f);
}
TEST_CASE("RV07 the image sources: the direct sound first and undelayed, then 24 reflections") {
    for (int size = 0; size < 3; ++size) for (double d : {0.5, 3.5, 20.0}) {
        const auto t = earlyTaps(size, d, 15); REQUIRE(t.size() == 25);
        CHECK(t[0].delaySeconds == 0.0); CHECK(t[0].order == 0);
        for (size_t i = 1; i < t.size(); ++i) { CHECK(t[i].delaySeconds > 0.0); CHECK(t[i].order >= 1); CHECK(t[i].order <= 2); CHECK(t[i].gain < t[0].gain * 1.01 + 1e-9); CHECK(std::abs(t[i].pan) <= 1.0); }
    }
    auto firstExcess = [](int size) { auto t = earlyTaps(size, 3.5, 0); double b = 1e9; for (size_t i = 1; i < t.size(); ++i) b = std::min(b, t[i].delaySeconds); return b; };
    CHECK(firstExcess(2) > firstExcess(0));
    // a source farther than the room allows makes the room grow (it stays a plausible room)
    const auto small20 = roomDims(0, 20.0, 0), small1 = roomDims(0, 1.0, 0); CHECK(small20.lx > small1.lx * 3); NEAR(small1.lx, 4.0, 1e-9);
}
TEST_CASE("RV07 the direct sound is not delayed; the level follows the distance") {
    auto a = make(room(2.0)); auto b = make(room(4.0));
    const auto ha = impulse(a, 0.5), hb = impulse(b, 0.5);
    size_t pk = 0; for (size_t i = 0; i < 200; ++i) if (std::abs(ha.l[i]) > std::abs(ha.l[pk])) pk = i;
    CHECK(pk <= 2); CHECK(peak(ha.l, 0, 3) > 3.0 * peak(ha.l, 10, 24000));
    NEAR(peak(ha.l, 0, 3) / peak(hb.l, 0, 3), 2.0, 0.25);
    // farther: the reflections are relatively stronger
    auto ratio = [](const Ir& h) { return energy(h.l, 0.0005, 0.2) / energy(h.l, 0.0, 0.0005); };
    CHECK(ratio(hb) > ratio(ha));
}
TEST_CASE("RV07 Angle places the sound") {
    auto c = make(room(3.0, 0)); auto r = make(room(3.0, 60)); auto l = make(room(3.0, -60));
    const auto hc = impulse(c, 0.2), hr = impulse(r, 0.2), hl = impulse(l, 0.2);
    NEAR(20 * std::log10(peak(hc.r, 0, 3) / peak(hc.l, 0, 3)), 0.0, 0.2);
    CHECK(20 * std::log10(peak(hr.r, 0, 3) / peak(hr.l, 0, 3)) > 6.0);
    CHECK(20 * std::log10(peak(hl.l, 0, 3) / peak(hl.r, 0, 3)) > 6.0);
}
TEST_CASE("RV07 Wall: concrete rings, curtain soaks it up, glass is thin in the lows") {
    auto con = make(room(3.5, 0, 1, Concrete)); auto wood = make(room(3.5, 0, 1, Wood)); auto glass = make(room(3.5, 0, 1, Glass)); auto cur = make(room(3.5, 0, 1, Curtain));
    const auto hc = impulse(con, 0.5), hw = impulse(wood, 0.5), hg = impulse(glass, 0.5), hu = impulse(cur, 0.5);
    const double ec = energy(hc.l, 0.002, 0.5), ew = energy(hw.l, 0.002, 0.5), eu = energy(hu.l, 0.002, 0.5);
    CHECK(10 * std::log10(eu / ew) < -6.0); CHECK(10 * std::log10(ec / ew) > 1.0);
    auto tail = [](const Ir& h) { return std::vector<float>(h.l.begin() + 96, h.l.end()); };   // the reflections only (after the first 2 ms)
    const auto tc = tail(hc), tw = tail(hw), tg = tail(hg), tu_ = tail(hu);
    auto hiLo = [](const std::vector<float>& y) { return band(y, 5000, 9000, 0, y.size()) - band(y, 800, 1500, 0, y.size()); };
    CHECK(hiLo(tc) > hiLo(tw) + 2.0); CHECK(hiLo(tu_) < hiLo(tw) - 4.0);
    auto lowHigh = [](const std::vector<float>& y) { return band(y, 60, 150, 0, y.size()) - band(y, 1000, 2000, 0, y.size()); };
    CHECK(lowHigh(tg) < lowHigh(tw) - 3.0);
}
TEST_CASE("RV07 Use: Dialog is the lightest, Foley the heaviest") {
    auto d = make(room(3.5, 0, 1, Wood, Dialog)); auto i = make(room(3.5, 0, 1, Wood, Instrument)); auto f = make(room(3.5, 0, 1, Wood, Foley));
    const double ed = energy(impulse(d, 0.5).l, 0.002, 0.5), ei = energy(impulse(i, 0.5).l, 0.002, 0.5), ef = energy(impulse(f, 0.5).l, 0.002, 0.5);
    CHECK(10 * std::log10(ei / ed) > 2.0); CHECK(10 * std::log10(ef / ei) > 2.0);
}
TEST_CASE("RV07 the extremes stay finite") {
    for (int use : {0, 2}) for (double d : {0.5, 20.0}) for (double a : {-90.0, 90.0}) for (int size : {0, 2}) for (int wall : {0, 3}) {
        auto p = make(room(d, a, size, wall, use)); for (float v : run(p, noise(-3, 0.3))) CHECK(std::isfinite(v));
    }
}
