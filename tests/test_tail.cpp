// tailSeconds(): how long a product rings on after its input has stopped (sw/tail.hpp; the CLAP tail extension / VST3 getTailSamples). Each one is checked against a burst of noise run through the core:
// what the core reports has to be at least what it does, and not absurdly more. host_smoke --tails does the same with the real plug-ins and random settings.
#include "doctest.h"
#include "sw/tail.hpp"
#include "tu.hpp"
#include "cr03/cr03.hpp"
#include "cr06/cr06.hpp"
#include "dl01/dl01.hpp"
#include "dl02/dl02.hpp"
#include "dl03/dl03.hpp"
#include "dl04/dl04.hpp"
#include "dl05/dl05.hpp"
#include "gt03/gt03.hpp"
#include "lv14/lv14.hpp"
#include "lv19/lv19.hpp"
#include "lv24/lv24.hpp"
#include "lv25/lv25.hpp"
#include "rv01/rv01.hpp"
#include "rv02/rv02.hpp"
#include "rv03/rv03.hpp"
#include "rv05/rv05.hpp"
#include "rv06/rv06.hpp"
#include "vo07/vo07.hpp"
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
template <class P> P make(const Set& set = {}) { P p; for (const auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); p.snapToTargets(); return p; }

// seconds from the end of 0.1 s of noise (about -20 dBFS RMS) to the last sample above -80 dBFS, over `seconds` of silence after it
template <class P> double ring(P& p, double seconds = 20.0) {
    const size_t at = static_cast<size_t>(0.1 * kFs), n = at + static_cast<size_t>(seconds * kFs);
    std::vector<float> l = noise(-20.0, 0.1, 3), r = noise(-20.0, 0.1, 4); l.resize(n, 0.0f); r.resize(n, 0.0f);
    for (size_t off = 0; off < n; off += 256) { const int m = static_cast<int>(std::min<size_t>(256, n - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, m); }
    long last = -1; for (size_t i = n; i-- > 0;) if (std::fabs(l[i]) > 1e-4f || std::fabs(r[i]) > 1e-4f) { last = static_cast<long>(i); break; }
    return last < static_cast<long>(at) ? 0.0 : (static_cast<double>(last) - static_cast<double>(at)) / kFs;
}
// reported >= what rings (a burst of noise, then silence), and (for a setting that is not a limit case) not more than `slack` times it plus `plus` seconds
template <class P> void agrees(const char* what, P p, double slack = 6.0, double plus = 1.0) {
    const double said = p.tailSeconds(), real = ring(p);
    INFO(std::string(what) << ": rings " << real << " s, says " << said << " s");
    CHECK(said >= real);
    CHECK(said <= slack * real + plus);
}
}  // namespace

TEST_CASE("tail helpers: a loop, taps, an RT60") {
    // one repeat is `gain` of the one before: n repeats are 20 log10(gain) n dB down
    NEAR(sw::tail::loop(0.5, 0.5, 60.0), 0.5 * (1 + std::ceil(60.0 / 6.0206)), 1e-9);
    CHECK(sw::tail::loop(0.5, 1.0) >= 1e8); CHECK(sw::tail::loop(0.5, 0.0) == doctest::Approx(0.5));
    // a single tap is the same loop (the decay rate form rounds the repeats to a continuous count)
    const double d[1] = {0.5};
    NEAR(sw::tail::multi(d, 1, 0.5, 60.0), 0.5 * 60.0 / 6.0206 + 0.5, 1e-3);
    // two taps of 1 and 2 units, 0.3 each: the decay per unit is the root of 0.3 r^-1 + 0.3 r^-2 = 1 -> r = 0.7 r... solved here: 0.3 / r + 0.3 / r^2 = 1 -> r = (0.3 + sqrt(0.09 + 1.2)) / 2
    const int m[2] = {1, 2};
    const double r = (0.3 + std::sqrt(0.09 + 1.2)) / 2.0;
    NEAR(sw::tail::taps(1.0, m, 2, 0.3, 60.0), 60.0 / (-20.0 * std::log10(r)) + 2.0, 1e-3);
    CHECK(sw::tail::taps(1.0, m, 2, 0.6) >= 1e8);   // 1.2 in all: it grows
    NEAR(sw::tail::fromRt60(3.0, 80.0), 4.0, 1e-12);
}

TEST_CASE("tail: the delays (repeats from the feedback, Freeze goes on for ever)") {
    agrees("DL01 default", make<sw::dl01::Processor>());
    agrees("DL01 400 ms, 70 %", make<sw::dl01::Processor>({{sw::dl01::Time, 400}, {sw::dl01::Sync, 0}, {sw::dl01::Feedback, 70}}));
    CHECK(make<sw::dl01::Processor>({{sw::dl01::Feedback, 100}}).tailSeconds() >= 1e8);
    agrees("DL02 default", make<sw::dl02::Processor>());
    agrees("DL02 all heads, Intensity 7", make<sw::dl02::Processor>({{sw::dl02::Heads, 5}, {sw::dl02::Intensity, 7}}));
    agrees("DL03 default", make<sw::dl03::Processor>());
    agrees("DL03 Feedback 8", make<sw::dl03::Processor>({{sw::dl03::Feedback, 8}, {sw::dl03::Time, 200}}));
    agrees("DL04 default", make<sw::dl04::Processor>());
    agrees("DL04 ping-pong, Feedback 60", make<sw::dl04::Processor>({{sw::dl04::PingPong, 1}, {sw::dl04::Feedback, 60}}));
    agrees("DL05 default", make<sw::dl05::Processor>());
    agrees("DL05 Pitch +12", make<sw::dl05::Processor>({{sw::dl05::Pitch, 1}, {sw::dl05::Time, 4}}));
    CHECK(make<sw::dl05::Processor>({{sw::dl05::Freeze, 1}}).tailSeconds() >= 1e8);
    agrees("LV25 default", make<sw::lv25::Processor>());
    agrees("LV25 90 %", make<sw::lv25::Processor>({{sw::lv25::Feedback, 90}, {sw::lv25::Time, 300}}));
}

TEST_CASE("tail: the reverbs (Decay is RT60), Freeze for ever, a pure delay is its delay") {
    agrees("RV01 default", make<sw::rv01::Processor>(), 8.0, 2.0);
    agrees("RV01 Decay 6", make<sw::rv01::Processor>({{sw::rv01::Decay, 6.0}}), 8.0, 2.0);
    CHECK(make<sw::rv01::Processor>({{sw::rv01::Freeze, 1}}).tailSeconds() >= 1e8);
    agrees("RV02 default", make<sw::rv02::Processor>(), 8.0, 2.0);
    agrees("RV03 default", make<sw::rv03::Processor>(), 8.0, 2.0);
    agrees("RV05 default", make<sw::rv05::Processor>(), 8.0, 2.0);
    agrees("RV06 Decay 8", make<sw::rv06::Processor>({{sw::rv06::Decay, 8.0}}), 12.0, 2.0);
    CHECK(make<sw::rv06::Processor>({{sw::rv06::Freeze, 1}}).tailSeconds() >= 1e8);
    agrees("LV24 default", make<sw::lv24::Processor>(), 8.0, 2.0);
    // LV19 / LV14 delay on purpose: the tail is the delay
    agrees("LV19 400 ms", make<sw::lv19::Processor>({{sw::lv19::Delay, 400}}), 2.0, 0.3);
    agrees("LV14 120 ms", make<sw::lv14::Processor>({{sw::lv14::Delay, 120}}), 2.0, 0.3);
}

TEST_CASE("tail: granular (the buffer read back), the strips' sends, the pedalboard adds its pedals up") {
    agrees("CR03 default", make<sw::cr03::Processor>(), 12.0, 3.0);
    agrees("CR03 Scatter, 300 ms grains", make<sw::cr03::Processor>({{sw::cr03::Mode, 1}, {sw::cr03::Grain, 300}}), 12.0, 3.0);
    CHECK(make<sw::cr03::Processor>({{sw::cr03::Freeze, 1}}).tailSeconds() >= 1e8);
    agrees("CR06 Space", make<sw::cr06::Processor>({{sw::cr06::Effect, sw::cr06::Space}, {sw::cr06::Amount, 0.8}}), 8.0, 2.0);
    CHECK(make<sw::cr06::Processor>({{sw::cr06::Effect, sw::cr06::Warm}}).tailSeconds() == 0.0);
    agrees("VO07 Plate and Echo", make<sw::vo07::Processor>({{sw::vo07::Plate, 5}, {sw::vo07::Echo, 5}}), 8.0, 2.0);
    CHECK(make<sw::vo07::Processor>().tailSeconds() == 0.0);
    // GT03: the Delay and Reverb pedals that are On
    auto p = make<sw::gt03::Processor>({{sw::gt03::slotParam(4, sw::gt03::On), 1}});
    const double delayOnly = p.tailSeconds();
    auto q = make<sw::gt03::Processor>({{sw::gt03::slotParam(4, sw::gt03::On), 1}, {sw::gt03::slotParam(5, sw::gt03::On), 1}});
    CHECK(delayOnly > 0.5); CHECK(q.tailSeconds() > delayOnly + 1.0);
    CHECK(make<sw::gt03::Processor>({{sw::gt03::BypassAll, 1}, {sw::gt03::slotParam(5, sw::gt03::On), 1}}).tailSeconds() == 0.0);
    agrees("GT03 Delay on", make<sw::gt03::Processor>({{sw::gt03::slotParam(4, sw::gt03::On), 1}}), 8.0, 2.0);
}

// reset() / prepare() as the adapter calls them when the host stops or jumps: 0.4 s of noise, then the reset, then silence: nothing of the old audio may come out
namespace {
template <class P, class Reset> double afterReset(P& p, Reset doReset) {
    auto l = noise(-20.0, 0.4, 5), r = noise(-20.0, 0.4, 6);
    for (size_t off = 0; off < l.size(); off += 256) { const int m = static_cast<int>(std::min<size_t>(256, l.size() - off)); float* c[2] = {l.data() + off, r.data() + off}; p.process(c, 2, m); }
    doReset(p);
    std::vector<float> a(24064, 0.0f), b(24064, 0.0f);   // (a multiple of 256: the loop below runs whole blocks)
    for (size_t off = 0; off < a.size(); off += 256) { float* c[2] = {a.data() + off, b.data() + off}; p.process(c, 2, 256); }
    double peak = 0; for (size_t i = 960; i < a.size(); ++i) peak = std::max({peak, static_cast<double>(std::fabs(a[i])), static_cast<double>(std::fabs(b[i]))});
    return peak;
}
}  // namespace

TEST_CASE("reset: the delays, reverbs, granular and the pedalboard forget what they held (a prepare() without allocation for most, a reset() for GT03 and the IR products)") {
    auto viaPrepare = [](auto& p) { p.prepare(kFs, 256); };
    { auto p = make<sw::dl01::Processor>({{sw::dl01::Feedback, 70}, {sw::dl01::Time, 300}, {sw::dl01::Sync, 0}}); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::dl02::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::dl04::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::rv01::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::rv02::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::rv06::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::lv24::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    { auto p = make<sw::cr03::Processor>(); CHECK(afterReset(p, viaPrepare) < 1e-4); }
    // GT03: the Delay and Reverb pedals On (slots 5 and 6), the pedals' own audio forgotten without making them again
    { auto p = make<sw::gt03::Processor>({{sw::gt03::slotParam(4, sw::gt03::On), 1}, {sw::gt03::slotParam(5, sw::gt03::On), 1}});
      CHECK(afterReset(p, [](auto& q) { q.reset(); }) < 1e-4); }
}
