// parameter IDs, defaults and automation flags reconciled with the spec document (仕様書 v1.0, 2026-10-05)
#include "doctest.h"
#include "dy01/dy01.hpp"
#include "dy02/dy02.hpp"
#include "dy03/dy03.hpp"
#include "dy04/dy04.hpp"
#include "dy05/dy05.hpp"
#include "dy07/dy07.hpp"
#include "dy08/dy08.hpp"
#include "ms02/ms02.hpp"
#include "ms04/ms04.hpp"
#include "sw/text.hpp"
#include <cmath>
#include <string>
#include <vector>
using namespace sw;
namespace {
std::vector<std::string> ids(const std::vector<ParamSpec>& s) { std::vector<std::string> v; for (const auto& p : s) v.push_back(p.id); return v; }
}
TEST_CASE("parameter IDs follow the spec") {
    CHECK(ids(dy01::specs()) == std::vector<std::string>{"dy01.drive", "dy01.ratio", "dy01.speed", "dy01.bite", "dy01.color", "dy01.out", "dy01.mix", "dy01.schpf"});
    CHECK(ids(dy02::specs()) == std::vector<std::string>{"dy02.level", "dy02.out", "dy02.speed", "dy02.target", "dy02.emph", "dy02.mix", "dy02.evo.on", "dy02.automakeup"});
    CHECK(ids(dy03::specs()) == std::vector<std::string>{"dy03.thresh", "dy03.ratio", "dy03.attack", "dy03.release", "dy03.makeup", "dy03.mix", "dy03.knee", "dy03.schpf", "dy03.evo.on"});
    CHECK(ids(dy04::specs()) == std::vector<std::string>{"dy04.thresh", "dy04.range", "dy04.attack", "dy04.hold", "dy04.release", "dy04.mode", "dy04.key.hpf", "dy04.key.lpf", "dy04.key.hpffreq", "dy04.key.lpffreq", "dy04.listen"});
    CHECK(ids(dy05::specs()) == std::vector<std::string>{"dy05.mode", "dy05.freq", "dy05.thresh", "dy05.range", "dy05.lookahead", "dy05.listen", "dy05.evo.on"});
    CHECK(ids(dy07::specs()) == std::vector<std::string>{"dy07.thresh", "dy07.ratio", "dy07.out", "dy07.snap", "dy07.mix", "dy07.knee"});
    CHECK(ids(dy08::specs()) == std::vector<std::string>{"dy08.thresh", "dy08.ratio", "dy08.knee", "dy08.attack", "dy08.release", "dy08.evo.on", "dy08.makeup", "dy08.mix", "dy08.schpf", "dy08.detector", "dy08.lookahead", "dy08.sc"});
    CHECK(ids(ms02::specs()) == std::vector<std::string>{"ms02.gain", "ms02.ceiling", "ms02.release", "ms02.lookahead", "ms02.tp", "ms02.isp", "ms02.link", "ms02.dither"});
    CHECK(ids(ms04::specs()) == std::vector<std::string>{"ms04.drive", "ms04.ceiling", "ms04.knee", "ms04.mix", "ms04.os", "ms04.gainmatch", "ms04.listen", "ms04.lowlat"});
}
TEST_CASE("DY04 defaults: threshold -80 dBFS (open); Listen is not automatable") {
    CHECK(dy04::specs()[dy04::Threshold].def == -80.0);
    CHECK_FALSE(dy04::specs()[dy04::Listen].automatable);
    CHECK(dy04::specs()[dy04::Range].automatable);
}
TEST_CASE("MS04 Listen: off by default, not automatable") {
    CHECK(ms04::specs()[ms04::Listen].def == 0.0);
    CHECK_FALSE(ms04::specs()[ms04::Listen].automatable);
}
TEST_CASE("ratio: the rightmost 5 % reads and acts as infinity (DY07, DY08)") {
    for (const ParamSpec* r : {&dy07::specs()[dy07::Compress], &dy08::specs()[dy08::Ratio]}) {
        CHECK(formatValue(*r, r->toValue(0.96)) == "inf");
        CHECK(formatValue(*r, r->toValue(0.90)) != "inf");
        CHECK(isInfiniteRatio(*r, r->toValue(0.96)));
        CHECK_FALSE(isInfiniteRatio(*r, r->toValue(0.90)));
    }
}
