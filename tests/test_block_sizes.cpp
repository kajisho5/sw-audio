// What a product makes of the audio must not depend on how the host cuts it into blocks (tests/block_helpers.hpp): found by `host_smoke --blocks` on the real plug-ins, fixed one by one here.
#include "doctest.h"
#include "block_helpers.hpp"
#include "tu.hpp"
#include "gt01/gt01.hpp"
#include "lv07/lv07.hpp"
#include "ms01/ms01.hpp"
#include "ms02/ms02.hpp"
#include "ms06/ms06.hpp"
#include "sa02/sa02.hpp"
#include "vo05/vo05.hpp"
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
template <class P> P make(const Set& set = {}) { P p; for (const auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 1024); p.snapToTargets(); return p; }
std::vector<float> stereoNoise(unsigned seed, double dbfs = -20.0, double seconds = 0.7) { return noise(dbfs, seconds, seed); }
}  // namespace

TEST_CASE("GT01 steady settings: the output does not depend on the block size (the control-rate values were local to process(), so a block that did not start on a 32-sample boundary ran with unity gain)") {
    CHECK(bsi::worstDb([] { return make<sw::gt01::Processor>(); }, stereoNoise(1), stereoNoise(2)) < -90.0);
}

TEST_CASE("MS01 and MS02 with loud input: the Auto release (fast for short reductions, slow for sustained ones) does not depend on the block size (it was decided once per block)") {
    CHECK(bsi::worstDb([] { return make<sw::ms02::Processor>(); }, stereoNoise(9, -4.0), stereoNoise(10, -4.0)) < -90.0);
    CHECK(bsi::worstDb([] { return make<sw::ms01::Processor>(); }, stereoNoise(11, -4.0), stereoNoise(12, -4.0)) < -90.0);
}

TEST_CASE("MS06 steady settings, quiet and loud: the output (Gain match, Auto release, the ramps) does not depend on the block size") {
    CHECK(bsi::worstDb([] { return make<sw::ms06::Processor>(); }, stereoNoise(3), stereoNoise(4)) < -90.0);
    CHECK(bsi::worstDb([] { return make<sw::ms06::Processor>(); }, stereoNoise(13, -4.0), stereoNoise(14, -4.0)) < -90.0);
}

TEST_CASE("LV07 steady settings: the gate, the near / far features and the gain are decided on a grid of the stream, so the output does not depend on the block size") {
    CHECK(bsi::worstDb([] { return make<sw::lv07::Processor>(); }, stereoNoise(5, -30.0, 1.5), stereoNoise(6, -30.0, 1.5)) < -90.0);
    CHECK(bsi::worstDb([] { return make<sw::lv07::Processor>(); }, stereoNoise(15, -4.0), stereoNoise(16, -4.0)) < -90.0);
}

TEST_CASE("SA02: with the same seed (the per-instance component tolerances and noise) the output does not depend on the block size") {
    auto mk = [](double noise) { return [noise] { auto p = make<sw::sa02::Processor>({{sw::sa02::Noise, noise}}); p.setSeed(4242); return p; }; };
    CHECK(bsi::worstDb(mk(0.0), stereoNoise(7), stereoNoise(8)) < -90.0);
    CHECK(bsi::worstDb(mk(0.7), stereoNoise(17, -4.0), stereoNoise(18, -4.0)) < -90.0);
}

TEST_CASE("VO05: a ride set by the host in the middle of the stream is ramped in on the stream's own grid, so the output does not depend on the block size") {
    using P = sw::vo05::Processor;
    bsi::Events<P> ev = {{5000, [](P& p) { p.setParam(sw::vo05::Ride, -6.0); }}, {9001, [](P& p) { p.setParam(sw::vo05::Ride, 2.5); }}};
    CHECK(bsi::worstDb([] { return make<P>(); }, stereoNoise(19), stereoNoise(20), ev) < -90.0);
}
