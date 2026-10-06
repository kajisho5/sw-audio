#include "doctest.h"
#include "sw/text.hpp"
#include "eq05/eq05.hpp"
#include <string>
using namespace sw;

TEST_CASE("value -> text -> value -> text is stable for every EQ05 parameter") {
    for (const auto& s : eq05::specs()) {
        for (int i = 0; i <= 2000; ++i) {
            const std::string t1 = formatValue(s, s.toValue(i / 2000.0));
            double v = 0;
            REQUIRE(parseValue(s, t1, v));
            const std::string t2 = formatValue(s, v);
            CHECK_MESSAGE(t1 == t2, s.id << ": " << t1 << " -> " << v << " -> " << t2);
        }
    }
}
TEST_CASE("display follows the spec rules") {
    ParamSpec hz{"f", "Freq", 20, 20000, 1000, Curve::Log, 1, {}, "Hz"};
    CHECK(formatValue(hz, 400) == "400 Hz");
    CHECK(formatValue(hz, 999.7) == "1.0 kHz");
    CHECK(formatValue(hz, 2400) == "2.4 kHz");
    CHECK(formatValue(hz, 9990) == "10 kHz");
    CHECK(formatValue(hz, 12000) == "12 kHz");
    ParamSpec db{"g", "Gain", -15, 15, 0, Curve::Lin, 1, {}, "dB"};
    CHECK(formatValue(db, -4.5) == "-4.5 dB");
    CHECK(formatValue(db, -0.04) == "0.0 dB");  // no negative zero
}
TEST_CASE("stepped parameters show their labels and parse them back") {
    const auto& s = eq05::specs()[eq05::Hpf];
    CHECK(formatValue(s, 0) == "Off");
    CHECK(formatValue(s, 80) == "80 Hz");
    double v = -1;
    CHECK(parseValue(s, "120 Hz", v)); CHECK(v == 120.0);
    CHECK_FALSE(parseValue(s, "banana", v));
}
TEST_CASE("units: ms, %, ratio, dBFS/dBTP/LUFS") {
    ParamSpec ms{"a", "Attack", 0.05, 200, 10, Curve::Skew, 3, {}, "ms"};
    CHECK(formatValue(ms, 2.345) == "2.3 ms");
    CHECK(formatValue(ms, 12.6) == "13 ms");
    ParamSpec pct{"m", "Mix", 0, 100, 100, Curve::Lin, 1, {}, "%"};
    CHECK(formatValue(pct, 42.4) == "42 %");
    ParamSpec ratio{"r", "Ratio", 1, 20, 2, Curve::Log, 1, {}, ":1"};
    CHECK(formatValue(ratio, 2.5) == "2.5:1");
    ParamSpec tp{"c", "Ceiling", -12, 0, -1, Curve::Lin, 1, {}, "dBTP"};
    CHECK(formatValue(tp, -1.0) == "-1.0 dBTP");
    double v = 0;
    CHECK(parseValue(tp, "-0.5 dBTP", v)); CHECK(v == doctest::Approx(-0.5));
}
TEST_CASE("end labels: Off at min, Auto / infinity at max") {
    ParamSpec hpf{"h", "SC HPF", 20, 300, 20, Curve::Log, 1, {}, "Hz"}; hpf.minLabel = "Off";
    CHECK(formatValue(hpf, 20) == "Off");
    CHECK(formatValue(hpf, 90) == "90 Hz");
    double v = 0;
    CHECK(parseValue(hpf, "Off", v)); CHECK(v == 20.0);
    ParamSpec rel{"r", "Release", 1, 1000, 1000, Curve::Log, 1, {}, "ms"}; rel.maxLabel = "Auto";
    CHECK(formatValue(rel, 1000) == "Auto");
    CHECK(parseValue(rel, "Auto", v)); CHECK(v == 1000.0);
    ParamSpec ratio{"r", "Ratio", 1, 20, 2, Curve::Log, 1, {}, ":1"}; ratio.maxLabel = "inf";
    CHECK(formatValue(ratio, 20) == "inf");
}
TEST_CASE("every unit and label round-trips text -> value -> text") {
    std::vector<ParamSpec> all = {
        {"a", "A", 0.05, 200, 10, Curve::Skew, 3, {}, "ms"}, {"b", "B", 0, 100, 100, Curve::Lin, 1, {}, "%"},
        {"c", "C", 1, 20, 2, Curve::Log, 1, {}, ":1"}, {"d", "D", -12, 0, -1, Curve::Lin, 1, {}, "dBTP"},
        {"e", "E", 5, 3000, 150, Curve::Skew, 3, {}, "ms"}, {"f", "F", 0.1, 4, 0.3, Curve::Skew, 2, {}, "s"}};
    all.push_back({"g", "G", 20, 300, 20, Curve::Log, 1, {}, "Hz"}); all.back().minLabel = "Off";
    all.push_back({"h", "H", 1, 1000, 1000, Curve::Log, 1, {}, "ms"}); all.back().maxLabel = "Auto";
    for (const auto& s : all)
        for (int i = 0; i <= 1000; ++i) {
            const std::string t1 = formatValue(s, s.toValue(i / 1000.0));
            double v = 0; REQUIRE(parseValue(s, t1, v));
            CHECK_MESSAGE(t1 == formatValue(s, v), s.id << ": " << t1 << " -> " << formatValue(s, v));
        }
}
