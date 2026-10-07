#include "doctest.h"
#include "lv15/lv15.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv15;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
// run several mics (one block at a time, all instances interleaved) for `seconds`; mic i gets signal sig[i]
void runMics(std::vector<Processor*> ps, const std::vector<std::vector<float>>& sig) {
    for (size_t off = 0; off < sig[0].size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, sig[0].size() - off));
        for (size_t k = 0; k < ps.size(); ++k) { std::vector<float> l(sig[k].begin() + off, sig[k].begin() + off + n), r = l; float* c[2] = {l.data(), r.data()}; ps[k]->process(c, 2, n); } }
}
}

TEST_CASE("LV15 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Mode].labels == std::vector<std::string>{"Gain share", "Gate"}); CHECK(s[Mode].def == 0);
    CHECK(s[LastMicHold].def == 1); CHECK(s[OffAtten].min == -40); CHECK(s[OffAtten].max == 0); CHECK(s[OffAtten].def == -15);
    CHECK(s[Response].labels == std::vector<std::string>{"Slow", "Medium", "Fast"}); CHECK(s[Response].def == 2);
    CHECK(s[Priority].labels.size() == 9); CHECK(s[Priority].labels[0] == "None"); CHECK(s[Priority].def == 1);
    CHECK(s[NomLimit].steps.size() == 8); CHECK(s[NomLimit].def == 4);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV15 mic numbers follow the order; a leaving mic frees its number") {
    Processor a = make(), b = make(), c = make(); CHECK(a.micNumber() == 1); CHECK(b.micNumber() == 2); CHECK(c.micNumber() == 3);
    { Processor d = make(); CHECK(d.micNumber() == 4); }
    Processor e = make(); CHECK(e.micNumber() == 4);
}
TEST_CASE("LV15 one talker: the talker is open, the quiet mics are at Off atten") {
    Processor a = make(), b = make(), c = make(); const auto talk = sine(-20, 3.0, 500), room = noise(-65, 3.0, 4);
    runMics({&a, &b, &c}, {room, talk, room});
    CHECK(std::abs(b.gain() - 1.0) < 0.02); CHECK(std::abs(20 * std::log10(a.gain()) - (-15.0)) < 0.7); CHECK(std::abs(20 * std::log10(c.gain()) - (-15.0)) < 0.7);
}
TEST_CASE("LV15 Gain share: the gains of the talkers add up to 1") {
    Processor a = make({{Priority, 0}}), b = make({{Priority, 0}}); const auto x = sine(-20, 3.0, 500), y = sine(-26, 3.0, 700);
    runMics({&a, &b}, {x, y});   // levels 2 : 1
    CHECK(std::abs(a.gain() - 2.0 / 3.0) < 0.03); CHECK(std::abs(b.gain() - 1.0 / 3.0) < 0.03); CHECK(std::abs(a.gain() + b.gain() - 1.0) < 0.03);
}
TEST_CASE("LV15 Priority counts three times; NOM limit keeps only the loudest") {
    { Processor a = make({{Priority, 1}}), b = make({{Priority, 1}}); const auto x = sine(-26, 3.0, 500), y = sine(-20, 3.0, 700);   // a is quieter but priority (x3)
    runMics({&a, &b}, {x, y}); CHECK(std::abs(a.gain() - 3.0 * 0.5 / (3.0 * 0.5 + 1.0)) < 0.03); }
    Processor c = make({{Priority, 0}, {NomLimit, 1}}), d = make({{Priority, 0}, {NomLimit, 1}}), e = make({{Priority, 0}, {NomLimit, 1}});
    runMics({&c, &d, &e}, {sine(-23, 3.0, 400), sine(-20, 3.0, 500), sine(-26, 3.0, 600)});
    CHECK(std::abs(d.gain() - 1.0) < 0.03); CHECK(c.gain() < 0.2); CHECK(e.gain() < 0.2);
}
TEST_CASE("LV15 Gate mode: active mics pass at 0 dB (within NOM), the rest at Off atten") {
    Processor a = make({{Mode, GateMode}, {Priority, 0}, {OffAtten, -20}}), b = make({{Mode, GateMode}, {Priority, 0}, {OffAtten, -20}}), c = make({{Mode, GateMode}, {Priority, 0}, {OffAtten, -20}});
    runMics({&a, &b, &c}, {sine(-20, 3.0, 400), sine(-30, 3.0, 500), noise(-70, 3.0, 5)});
    CHECK(std::abs(a.gain() - 1.0) < 0.02); CHECK(std::abs(b.gain() - 1.0) < 0.02); CHECK(std::abs(20 * std::log10(c.gain()) - (-20.0)) < 0.7);
}
TEST_CASE("LV15 Last mic hold keeps the last talker open when everybody is quiet") {
    const auto z = noise(-70, 2.0, 3);
    std::vector<float> tA = sine(-20, 2.0, 500), qA = noise(-70, 2.0, 6); std::vector<float> sigA = tA; sigA.insert(sigA.end(), z.begin(), z.end());
    std::vector<float> sigB = qA; const auto qB = noise(-70, 2.0, 7); sigB.insert(sigB.end(), qB.begin(), qB.end());
    { Processor a = make({{Priority, 0}}), b = make({{Priority, 0}});
    runMics({&a, &b}, {sigA, sigB});   // A talks for 2 s then everybody is quiet for 2 s
    CHECK(std::abs(a.gain() - 1.0) < 0.03); CHECK(b.gain() < 0.2); }
    Processor c = make({{Priority, 0}, {LastMicHold, 0}}), d = make({{Priority, 0}, {LastMicHold, 0}}); runMics({&c, &d}, {sigA, sigB});
    CHECK(std::abs(c.gain() - 0.5) < 0.05); CHECK(std::abs(d.gain() - 0.5) < 0.05);   // gain share with nobody talking: 1/N
}
TEST_CASE("LV15 Response: Fast reaches the new state sooner than Slow") {
    auto at = [&](double resp) { Processor a = make({{Response, resp}, {Priority, 0}}), b = make({{Response, resp}, {Priority, 0}}); runMics({&a, &b}, {sine(-20, 0.35, 500), noise(-70, 0.35, 3)}); return b.gain(); };
    CHECK(at(2) < at(0));
}
TEST_CASE("LV15 an instance that stopped processing is forgotten after half a second") {
    Processor a = make({{Priority, 0}}), b = make({{Priority, 0}}); const auto x = sine(-20, 1.0, 500), y = sine(-20, 1.0, 700);
    runMics({&a, &b}, {x, y}); CHECK(std::abs(a.gain() - 0.5) < 0.05);
    runMics({&a}, {sine(-20, 2.0, 500)});   // b is bypassed from here on: its last numbers stay in the slot, but its beat stops
    CHECK(std::abs(a.gain() - 1.0) < 0.03);
}
TEST_CASE("LV15 mono, odd blocks, before prepare, the ninth mic") {
    std::vector<Processor> v; v.reserve(9); for (int i = 0; i < 9; ++i) v.push_back(make()); CHECK(v[8].micNumber() == 0);
    std::vector<float> l = sine(-20, 0.5, 500), keep = l; float* c[1] = {l.data()}; v[8].process(c, 1, static_cast<int>(l.size())); CHECK(l == keep);   // not in the group: untouched
    auto p = make(); std::vector<float> m = noise(-20, 1.0, 3); for (size_t off = 0; off < m.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, m.size() - off)); float* cc[1] = {m.data() + off}; p.process(cc, 1, n); }
    for (float x : m) REQUIRE(std::isfinite(x));
    Processor z; std::vector<float> a(256, 0.3f); float* cz[1] = {a.data()}; z.process(cz, 1, 256); CHECK(a[0] == 0.3f);
}
