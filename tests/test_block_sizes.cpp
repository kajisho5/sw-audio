// What a product makes of the audio must not depend on how the host cuts it into blocks (tests/block_helpers.hpp): found by `host_smoke --blocks` on the real plug-ins, fixed one by one here.
#include "doctest.h"
#include "block_helpers.hpp"
#include "tu.hpp"
#include "gt01/gt01.hpp"
#include "lv07/lv07.hpp"
#include "lv05/lv05.hpp"
#include "lv08/lv08.hpp"
#include "lv29/lv29.hpp"
#include "ms01/ms01.hpp"
#include "ms02/ms02.hpp"
#include "ms06/ms06.hpp"
#include "sa02/sa02.hpp"
#include "sa05/sa05.hpp"
#include "vo05/vo05.hpp"
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
template <class P> P make(const Set& set = {}) { P p; for (const auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 1024); p.snapToTargets(); return p; }
std::vector<float> stereoNoise(unsigned seed, double dbfs = -20.0, double seconds = 0.7) { return noise(dbfs, seconds, seed); }
// a 220 Hz tone in bursts (0.12 s on, 0.1 s off) over a faint noise floor: what gates, detectors and pitch trackers need to act
std::vector<float> bursts(unsigned seed, double dbfs = -12.0, double seconds = 1.5) {
    auto x = noise(-60.0, seconds, seed); const double a = std::pow(10.0, dbfs / 20.0);
    for (size_t i = 0; i < x.size(); ++i) if (std::fmod(static_cast<double>(i) / kFs, 0.22) < 0.12) x[i] += static_cast<float>(a * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / kFs));
    return x;
}
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

TEST_CASE("LV08: the voice detector is read at the frame (it was fed the whole host block first), so the output does not depend on the block size") {
    CHECK(bsi::worstDb([] { return make<sw::lv08::Processor>(); }, bursts(21), bursts(22)) < -90.0);
}

TEST_CASE("SA05 with Auto fill: the 100 ms analysis happens at the same sample whatever the block size is") {
    using P = sw::sa05::Processor;
    CHECK(bsi::worstDb([] { return make<P>({{sw::sa05::AutoFill, 1.0}}); }, bursts(23), bursts(24)) < -90.0);
}

TEST_CASE("LV05: the key is judged on a grid of the stream, so the ducking does not depend on the block size (sidechain key, and the voice detector)") {
    using P = sw::lv05::Processor;
    const std::vector<float> key[2] = {bursts(25, -10.0), bursts(26, -10.0)};
    CHECK(bsi::worstDb([] { return make<P>(); }, stereoNoise(27, -20.0, 1.5), stereoNoise(28, -20.0, 1.5), {}, key) < -90.0);
    CHECK(bsi::worstDb([] { return make<P>({{sw::lv05::VoiceOnly, 1.0}}); }, stereoNoise(27, -20.0, 1.5), stereoNoise(28, -20.0, 1.5), {}, key) < -90.0);
}

TEST_CASE("LV29: the interpreter's activity is read on a grid of the stream (the detector was fed the whole host block first), so the mix does not depend on the block size") {
    using P = sw::lv29::Processor;
    const std::vector<float> interp[2] = {bursts(29, -10.0), bursts(30, -10.0)};
    CHECK(bsi::worstDb([] { return make<P>(); }, stereoNoise(31, -30.0, 1.5), stereoNoise(32, -30.0, 1.5), {}, interp) < -90.0);
}
