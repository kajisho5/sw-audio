#include "doctest.h"
#include "lv23/lv23.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv23;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }
}

TEST_CASE("LV23 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Preset].labels == std::vector<std::string>{"ARIB -24", "EBU -23", "Stream -14"}); CHECK(s[Preset].def == 0);
    Processor q; CHECK(q.latencySamples() == 0); CHECK(q.target() == -24.0);
}
TEST_CASE("LV23 the sound passes untouched; the engine is MT01's") {
    auto p = make(); const auto x = sine(-20.0, 10.0, 1000), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    CHECK(std::abs(p.integrated() - (-16.99)) < 0.2);   // -20 dBFS RMS on both channels: the two channel powers add (+3.01 dB), the K filter is flat at 1 kHz
    CHECK(std::abs(p.difference() - (p.integrated() + 24.0)) < 1e-9);
}
TEST_CASE("LV23 Preset changes the target") {
    auto a = make({{Preset, Ebu}}); CHECK(a.target() == -23.0); auto b = make({{Preset, Stream}}); CHECK(b.target() == -14.0);
    b.setParam(Preset, Arib); CHECK(b.target() == -24.0);
}
TEST_CASE("LV23 Dead air and true peak over are noticed and cleared by reset") {
    auto p = make(); run(p, sine(-30, 3.0, 1000)); CHECK_FALSE(p.deadAirSeen()); CHECK_FALSE(p.tpOver());
    run(p, std::vector<float>(48000 * 8, 0.0f)); CHECK(p.deadAirSeen());
    std::vector<float> hot = sine(-3, 1.0, 1000); for (auto& v : hot) v *= 1.0f; run(p, hot); CHECK(p.tpOver());   // -3 dBFS rms = about 0 dBFS peak
    p.reset(); CHECK_FALSE(p.deadAirSeen()); CHECK_FALSE(p.tpOver()); CHECK(p.logCount() == 0);
    auto q = make(); run(q, sine(-30, 3.0, 1000)); run(q, std::vector<float>(48000, 0.0f)); CHECK_FALSE(q.deadAirSeen());   // a 1 s pause is not dead air
}
TEST_CASE("LV23 the log has one line a second, with the values and a header") {
    auto p = make(); p.setStartTime(1700000000.0); run(p, sine(-26.3, 12.5, 1000)); CHECK(p.logCount() == 12);
    const std::string csv = p.exportCsv(); CHECK(csv.rfind("elapsed,clock,momentary_lufs,short_term_lufs,integrated_lufs,range_lu,true_peak_dbtp,dead_air\n", 0) == 0);
    int lines = 0; for (char c : csv) if (c == '\n') ++lines; CHECK(lines == 13);
    CHECK(csv.find("00:00:12,") != std::string::npos); CHECK(csv.find("00:00:01,22:13:21,") != std::string::npos);   // 1700000000 = 22:13:20 UTC
    auto q = make(); CHECK(q.exportCsv().find('\n') == q.exportCsv().size() - 1);   // only the header
    run(q, noise(-30, 2.0, 3)); const std::string c2 = q.exportCsv(); CHECK(c2.find("00:00:01,,") != std::string::npos);   // no clock without a start time
}
TEST_CASE("LV23 mono, odd blocks, before prepare") {
    auto p = make(); std::vector<float> l = noise(-20, 1.5, 3); for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); }
    for (float v : l) REQUIRE(std::isfinite(v));
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); CHECK(z.exportCsv().size() > 10);
}
