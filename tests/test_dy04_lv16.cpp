#include "doctest.h"
#include "dy04/dy04.hpp"
#include "lv16/lv16.hpp"
#include <cmath>
#include <vector>
using namespace sw;
namespace {
const double kPi = 3.14159265358979323846, kFs = 48000.0;
double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return 10 * std::log10(s / (b - a) + 1e-30); }
template <class P> std::vector<float> run(P& p, std::vector<float> l, const std::vector<float>* key = nullptr) {
    std::vector<float> r = l;
    for (size_t off = 0; off < l.size(); off += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - off));
        float* c[2] = {l.data() + off, r.data() + off};
        if (key) { const float* k[2] = {key->data() + off, key->data() + off}; p.processWithSidechain(c, 2, n, k, 2); }
        else p.process(c, 2, n);
    }
    return l;
}
std::vector<float> burstThenQuiet() {  // 0.5 s at -6 dBFS, then 1.5 s at -60 dBFS
    std::vector<float> x(48000 * 2);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>((i < 24000 ? 0.5 : 0.001) * std::sin(2 * kPi * 200.0 * i / kFs));
    return x;
}
}

TEST_CASE("DY04 table follows 04_parameters ranges") {
    using namespace dy04; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Threshold].min == -80); CHECK(s[Threshold].max == 0);
    CHECK(s[Range].min == -80); CHECK(s[Range].max == 0);
    CHECK(s[Attack].min == 0.01); CHECK(s[Attack].max == 25);
    CHECK(s[Hold].max == 2000); CHECK(s[Release].min == 5); CHECK(s[Release].max == 4000);
    CHECK(s[Mode].labels == std::vector<std::string>{"Gate", "Expand", "Duck"});
}
TEST_CASE("DY04 gate: the quiet tail is attenuated by the range, the burst passes") {
    using namespace dy04;
    Processor p; p.setParam(Threshold, -40); p.prepare(kFs, 256); p.snapToTargets();
    const auto y = run(p, burstThenQuiet());
    CHECK(rmsDb(y, 2400, 24000) == doctest::Approx(20 * std::log10(0.5) - 3.01).epsilon(0.01));
    CHECK(rmsDb(y, 72000, 96000) == doctest::Approx(20 * std::log10(0.001) - 3.01 - 40).epsilon(0.02));
}
TEST_CASE("DY04 Listen outputs the key (after the key filters)") {
    using namespace dy04;
    Processor p; p.setParam(Listen, 1); p.setParam(KeyHpf, 1); p.setParam(KeyHpfHz, 1000); p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> x(48000); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 50.0 * i / kFs));
    CHECK(rmsDb(run(p, x), 24000, 48000) < 20 * std::log10(0.5) - 3.01 - 40);  // 50 Hz removed by the 1 kHz key HPF
}
TEST_CASE("DY04 Duck: an external key pulls the main signal down by the range") {
    using namespace dy04;
    Processor p; p.setParam(Mode, 2); p.setParam(Threshold, -30); p.setParam(Range, -20); p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> x(48000), k(48000);
    for (size_t i = 0; i < x.size(); ++i) { x[i] = static_cast<float>(0.3 * std::sin(2 * kPi * 440.0 * i / kFs)); k[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 100.0 * i / kFs)); }
    CHECK(rmsDb(run(p, x, &k), 24000, 48000) == doctest::Approx(20 * std::log10(0.3) - 3.01 - 20).epsilon(0.01));
}
TEST_CASE("LV16 table uses the screen values as defaults") {
    using namespace lv16; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Threshold].def == -42); CHECK(s[Range].def == -40); CHECK(s[Hold].def == 80); CHECK(s[Release].def == 250);
    CHECK(s[KeyHpf].def == 1); CHECK(s[Mode].labels == std::vector<std::string>{"Gate", "Duck"});
}
TEST_CASE("LV16: stage rumble below 100 Hz does not open the gate with Key HPF on") {
    using namespace lv16;
    auto tail = [](double keyHpf) {
        Processor p; p.setParam(KeyHpf, keyHpf); p.prepare(kFs, 256); p.snapToTargets();
        std::vector<float> x(48000 * 2); for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.05 * std::sin(2 * kPi * 30.0 * i / kFs));  // -26 dBFS rumble
        return rmsDb(run(p, x), 48000, 96000);
    };
    CHECK(tail(0) > -30.0);       // without the key filter the rumble keeps the gate open
    CHECK(tail(1) < -60.0);       // with it the gate stays closed
}
