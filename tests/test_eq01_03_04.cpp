#include "doctest.h"
#include "eq_helpers.hpp"
#include "os_helpers.hpp"
#include "eq01/eq01.hpp"
#include "eq03/eq03.hpp"
#include "eq04/eq04.hpp"
#include "sw/text.hpp"
using namespace sw;
using eqt::gainDb;

TEST_CASE("reversed parameters: Width runs Narrow (Q 2.0) to Wide (Q 0.4) and reads as Q") {
    const ParamSpec& w = eq01::specs()[eq01::Width];
    CHECK(w.toValue(0.0) == doctest::Approx(2.0)); CHECK(w.toValue(1.0) == doctest::Approx(0.4));
    CHECK(w.toNorm(2.0) == doctest::Approx(0.0)); CHECK(w.def == doctest::Approx(0.8));
    CHECK(formatValue(w, 0.8) == "Q 0.80");
    double v = 0; CHECK(parseValue(w, "Q 1.25", v)); CHECK(v == doctest::Approx(1.25));
    for (int i = 0; i <= 200; ++i) { const std::string t = formatValue(w, w.toValue(i / 200.0)); double x = 0; REQUIRE(parseValue(w, t, x)); CHECK(formatValue(w, x) == t); }
}
TEST_CASE("EQ01 table follows the spec") {
    using namespace eq01; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(std::string(s[LowFreq].id) == "eq01.low.freq"); CHECK(s[LowFreq].min == 20); CHECK(s[LowFreq].max == 400); CHECK(s[LowFreq].def == 60);
    CHECK(s[LowGain].min == -12); CHECK(s[Contour].max == 10); CHECK(s[Contour].def == 0);
    CHECK(s[AirFreq].min == 2000); CHECK(s[AirFreq].max == 20000); CHECK(s[AirFreq].def == 10000);
    CHECK(std::string(s[Drive].id) == "eq01.out.drive"); CHECK(s[Drive].def == 2); CHECK(std::string(s[Output].id) == "eq01.out.level");
    CHECK(s[Mode].labels == std::vector<std::string>{"LR", "MS"});
}
TEST_CASE("EQ01: low shelf, Contour dip above it, Air bell with Width") {
    using namespace eq01;
    auto make = [](std::vector<std::pair<int, double>> set) { Processor p; p.setParam(Drive, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; };
    { auto p = make({{LowGain, 8}}); CHECK(gainDb(p, 20) > 6.5); }
    double dip0 = 0, dip10 = 0;
    for (double f : {90.0, 120.0, 160.0, 220.0, 300.0}) {
        auto a = make({{LowGain, 8}}); dip0 = std::min(dip0, gainDb(a, f));
        auto b = make({{LowGain, 8}, {Contour, 10}}); dip10 = std::min(dip10, gainDb(b, f));
    }
    CHECK(dip0 > -0.05);     // plain shelf: never below 0 dB above the corner
    CHECK(dip10 < -1.0);     // Contour: "boost and cut a little above"
    { auto p = make({{AirGain, 6}}); CHECK(gainDb(p, 10000) == doctest::Approx(6.0).epsilon(0.02)); }
    auto narrow = make({{AirGain, 6}, {Width, 2.0}}), wide = make({{AirGain, 6}, {Width, 0.4}});
    CHECK(gainDb(wide, 5000) > gainDb(narrow, 5000) + 2.0);
}
TEST_CASE("EQ01 MS mode: the EQ works on the mid; a side-only signal passes") {
    using namespace eq01;
    Processor p; p.setParam(Drive, 0); p.setParam(AirGain, 6); p.setParam(Mode, 1); p.prepare(eqt::kFs, 256); p.snapToTargets();
    CHECK(std::abs(gainDb(p, 10000, 0.001, 1)) < 0.05);
    Processor q; q.setParam(Drive, 0); q.setParam(AirGain, 6); q.setParam(Mode, 1); q.prepare(eqt::kFs, 256); q.snapToTargets();
    CHECK(gainDb(q, 10000) == doctest::Approx(6.0).epsilon(0.02));
}
TEST_CASE("EQ03 table follows the spec") {
    using namespace eq03; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[DipFreq].steps == std::vector<double>{200, 500, 1000, 1500, 2000, 3000}); CHECK(s[DipFreq].def == 1000);
    CHECK(s[PeakFreq].steps == std::vector<double>{700, 1000, 1500, 2000, 3000, 4000, 5000}); CHECK(s[PeakFreq].def == 2000);
    CHECK(s[Dip].max == 10); CHECK(s[Peak].max == 10); CHECK(s[Width].def == doctest::Approx(1.2)); CHECK(s[Ride].def == 0);
    CHECK(std::string(s[Ride].id) == "eq03.evo.on");
}
TEST_CASE("EQ03: Dip 10 = -10 dB, Peak 10 = +10 dB, Width shared") {
    using namespace eq03;
    auto make = [](std::vector<std::pair<int, double>> set) { Processor p; p.setParam(Drive, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; };
    { auto p = make({{Dip, 10}}); CHECK(gainDb(p, 1000) == doctest::Approx(-10).epsilon(0.01)); }
    { auto p = make({{Peak, 10}}); CHECK(gainDb(p, 2000) == doctest::Approx(10).epsilon(0.01)); }
    auto n = make({{Peak, 10}, {Width, 2.5}}), w = make({{Peak, 10}, {Width, 0.6}});
    CHECK(gainDb(w, 4000) > gainDb(n, 4000) + 2.0);
}
TEST_CASE("EQ03 Ride lowers the peak when the vocal is loud") {
    using namespace eq03;
    auto peakAt = [](double amp) { Processor p; p.setParam(Drive, 0); p.setParam(Peak, 10); p.setParam(Ride, 1); p.prepare(eqt::kFs, 256); p.snapToTargets(); return gainDb(p, 2000, amp); };
    CHECK(peakAt(0.01) == doctest::Approx(10).epsilon(0.02));   // -43 dBFS RMS: below the reference -> as set
    CHECK(peakAt(0.7) < 10 - 3.0);                               // -6 dBFS RMS: well above -> pulled down
}
TEST_CASE("EQ04 table follows the spec") {
    using namespace eq04; const auto& s = specs();
    REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Hpf].steps == std::vector<double>{0, 50, 80, 160, 300}); CHECK(s[Hpf].def == 0);
    CHECK(s[LowFreq].steps == std::vector<double>{35, 60, 110, 220}); CHECK(s[LowFreq].def == 60);
    CHECK(s[MidFreq].steps == std::vector<double>{360, 700, 1600, 3200, 4800, 7200}); CHECK(s[MidFreq].def == 1600);
    CHECK(s[HighFreq].steps == std::vector<double>{10000, 12000, 16000}); CHECK(s[HighFreq].def == 12000);
    CHECK(s[LowGain].max == 16); CHECK(s[MidGain].max == 18); CHECK(s[HighGain].max == 16); CHECK(s[Iron].def == 1);
}
TEST_CASE("EQ04: HPF 18 dB/oct, low shelf with a small bump, broad mid bell, high shelf") {
    using namespace eq04;
    auto make = [](std::vector<std::pair<int, double>> set) { Processor p; p.setParam(Drive, 0); for (auto& s : set) p.setParam(s.first, s.second); p.prepare(eqt::kFs, 256); p.snapToTargets(); return p; };
    { auto p = make({{Hpf, 80}}); CHECK(gainDb(p, 80) == doctest::Approx(-3.01).epsilon(0.03)); auto q = make({{Hpf, 80}}); CHECK(gainDb(q, 40) == doctest::Approx(-18.1).epsilon(0.05)); }
    { double mx = -99; for (double f : {20.0, 25.0, 30.0, 35.0, 40.0, 45.0}) { auto p = make({{LowGain, 16}}); mx = std::max(mx, gainDb(p, f)); } CHECK(mx > 16.5); CHECK(mx < 17.5); }
    { auto p = make({{MidGain, 18}}); CHECK(gainDb(p, 1600) == doctest::Approx(18).epsilon(0.01)); }
    { auto p = make({{HighGain, 16}}); CHECK(gainDb(p, 20000) > 14.5); }
}
TEST_CASE("EQ04 Iron: harmonics only on boosted bands") {
    using namespace eq04;
    // the output stage (Drive) always colours a little, so compare Iron on / off at the same settings
    auto h3 = [](double low, double iron) { Processor p; p.setParam(Drive, 0); p.setParam(LowGain, low); p.setParam(Iron, iron); p.prepare(eqt::kFs, 256); p.snapToTargets(); return eqt::harmonicDb(p, 50, 3, 0.02); };
    CHECK(h3(16, 1) > h3(16, 0) + 15.0);                     // boosted low band: iron adds odd harmonics
    CHECK(std::abs(h3(-16, 1) - h3(-16, 0)) < 0.5);          // cut band: iron adds nothing
}

// ---- the common oversampling setting (spec 共通機能: 1x / 2x / 4x, default 2x): the Drive stage of all three, and EQ04's Iron
TEST_CASE("EQ01 / EQ03 / EQ04: the oversampling parameter is the last one, 1x / 2x / 4x, default 2x") {
    auto check = [](const std::vector<ParamSpec>& s, int os, const char* id) {
        REQUIRE(s.size() == static_cast<size_t>(os) + 1);
        const ParamSpec& p = s[static_cast<size_t>(os)];
        CHECK(std::string(p.id) == id); CHECK(p.def == 2.0); CHECK(p.steps == std::vector<double>{1, 2, 4}); CHECK(p.labels == std::vector<std::string>{"1x", "2x", "4x"}); CHECK(p.automatable);
    };
    check(eq01::specs(), eq01::Oversample, "eq01.os"); check(eq03::specs(), eq03::Oversample, "eq03.os"); check(eq04::specs(), eq04::Oversample, "eq04.os");
}
TEST_CASE("EQ01 / EQ03 / EQ04: at Drive 10 a 15 kHz tone's 3rd harmonic folds to 3 kHz at 1x and does not at 2x / 4x") {
    auto alias = [](auto p, int driveId, int osId, int os) { p.setParam(driveId, 10); p.setParam(osId, os); p.prepare(ost::kFs, 256); p.snapToTargets(); return ost::relDb(p, 15000, 3000, 0.3); };
    for (int product = 0; product < 3; ++product) {
        auto a = [&](int os) {
            return product == 0 ? alias(eq01::Processor{}, eq01::Drive, eq01::Oversample, os) : product == 1 ? alias(eq03::Processor{}, eq03::Drive, eq03::Oversample, os) : alias(eq04::Processor{}, eq04::Drive, eq04::Oversample, os);
        };
        const double a1 = a(1), a2 = a(2), a4 = a(4);
        INFO("EQ0" << (product == 0 ? 1 : product == 1 ? 3 : 4) << ": 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
        CHECK(a1 > -45.0); CHECK(a2 < -60.0); CHECK(ost::notWorse(a4, a2));
    }
}
TEST_CASE("EQ04 Iron: its saturation also follows the oversampling (a 7 kHz tone through +18 dB at 7.2 kHz, Drive 0)") {
    using namespace eq04;
    auto alias = [](int os) { Processor p; p.setParam(Drive, 0); p.setParam(MidFreq, 7200); p.setParam(MidGain, 18); p.setParam(Iron, 1); p.setParam(Oversample, os); p.prepare(ost::kFs, 256); p.snapToTargets(); return ost::relDb(p, 7000, 13000, 0.1); };
    const double a1 = alias(1), a2 = alias(2), a4 = alias(4);
    INFO("alias at 13 kHz: 1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
    CHECK(a2 < a1 - 10.0); CHECK(ost::notWorse(a4, a2));
}
