#include "doctest.h"
#include "mt05/mt05.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::mt05;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
void feed(Processor& p, std::vector<float> l) { std::vector<float> r = l; for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); } }
}

TEST_CASE("MT05 table follows the spec") {
    const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[RefDb].id) == "mt05.ref"); CHECK(s[RefDb].labels == std::vector<std::string>{"-14", "-18", "-20"}); CHECK(s[RefDb].def == -18);
    CHECK(std::string(s[MeterType].id) == "mt05.meter"); CHECK(s[MeterType].labels == std::vector<std::string>{"VU", "PPM"}); CHECK(s[MeterType].def == 0);
}
TEST_CASE("MT05 the signal passes unchanged; no delay") {
    Processor q; CHECK(q.latencySamples() == 0);
    auto p = make(); std::vector<float> l = noise(-18, 1.0, 3), r = noise(-18, 1.0, 4); const auto l0 = l, r0 = r;
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    for (size_t i = 0; i < l.size(); ++i) { CHECK(l[i] == l0[i]); CHECK(r[i] == r0[i]); }
}
TEST_CASE("MT05 VU: a sine at the reference reads 0 VU; Ref moves the zero") {
    for (double ref : {-14.0, -18.0, -20.0}) { auto p = make({{RefDb, ref}}); feed(p, sine(ref, 2.0, 1000)); NEAR(p.vuDb(0), 0.0, 0.1); NEAR(p.levelDb(1), 0.0, 0.1); }
    auto p = make({{RefDb, -18}}); feed(p, sine(-12, 2.0, 1000)); NEAR(p.vuDb(0), 6.0, 0.1);
}
TEST_CASE("MT05 VU ballistics: 99 % of a step in 300 ms") {
    auto p = make(); feed(p, sine(-18, 1.0, 1000)); const double steady = p.vuDb(0);
    p.reset(); const auto x = sine(-18, 1.0, 1000); std::vector<float> l(x.begin(), x.begin() + 14400), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, 14400);   // 300 ms
    const double a = std::pow(10.0, p.vuDb(0) / 20.0), a0 = std::pow(10.0, steady / 20.0); NEAR(a / a0, 0.99, 0.015);
}
TEST_CASE("MT05 PPM: a quick rise, a slow fall of 20 dB in 1.7 s") {
    auto p = make({{MeterType, Ppm}, {RefDb, -18}}); feed(p, sine(-18, 1.0, 1000)); const double full = p.ppmDb(0);
    NEAR(full, 0.0, 1.0);                                                                       // a steady sine (peak) at the reference reads about 0
    p.reset(); std::vector<float> burst = sine(-18, 0.010, 1000); feed(p, burst); CHECK(p.ppmDb(0) > full - 3.5); CHECK(p.ppmDb(0) < full - 0.2);   // a 10 ms burst: a little under
    auto q = make({{MeterType, Ppm}}); feed(q, sine(-18, 1.0, 1000)); const double top = q.ppmDb(0); feed(q, std::vector<float>(static_cast<size_t>(1.7 * kFs), 0.0f));
    NEAR(top - q.ppmDb(0), 20.0, 1.5);
}
TEST_CASE("MT05 the two channels are read separately; silence and loud input are finite") {
    auto p = make(); std::vector<float> l = sine(-18, 2.0, 1000), r = sine(-30, 2.0, 1000);
    for (size_t off = 0; off < l.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, n); }
    NEAR(p.vuDb(0) - p.vuDb(1), 12.0, 0.1);
    auto q = make(); feed(q, std::vector<float>(48000, 0.0f)); CHECK(q.vuDb(0) < -150.0);
    auto x = noise(6, 1.0, 9); for (auto& v : x) v *= 8.0f; feed(q, x); CHECK(std::isfinite(q.vuDb(0))); CHECK(std::isfinite(q.ppmDb(0)));
}
