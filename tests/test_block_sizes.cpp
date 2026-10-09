// What a product makes of the audio must not depend on how the host cuts it into blocks (tests/block_helpers.hpp): found by `host_smoke --blocks` on the real plug-ins, fixed one by one here.
#include "doctest.h"
#include "block_helpers.hpp"
#include "tu.hpp"
#include "gt01/gt01.hpp"
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
template <class P> P make(const Set& set = {}) { P p; for (const auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 1024); p.snapToTargets(); return p; }
std::vector<float> stereoNoise(unsigned seed, double dbfs = -20.0, double seconds = 0.7) { return noise(dbfs, seconds, seed); }
}  // namespace

TEST_CASE("GT01 steady settings: the output does not depend on the block size (the control-rate values were local to process(), so a block that did not start on a 32-sample boundary ran with unity gain)") {
    CHECK(bsi::worstDb([] { return make<sw::gt01::Processor>(); }, stereoNoise(1), stereoNoise(2)) < -90.0);
}
