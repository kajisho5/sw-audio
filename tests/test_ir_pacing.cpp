// RV04 / ST05 / GT02: a new IR is built a step at a time, on the stream's own grid (a grid point every 64 samples, a step every 4; sw/grid_clock.hpp): how long it takes, in samples, and the sample at which
// it arrives do not depend on how the host cuts the audio
#include "doctest.h"
#include "gt02/gt02.hpp"
#include "rv04/rv04.hpp"
#include "st05/st05.hpp"
#include "sw/grid_clock.hpp"
#include "tu.hpp"
using namespace tu;
namespace {
// samples (in blocks of `block`) until a new IR has been built after a change, counted at the end of the call that finished it
template <class P> long samplesToBuild(int block, int change) {
    P p; p.prepare(kFs, 256); p.snapToTargets();
    std::vector<float> l(static_cast<size_t>(block), 0.05f), r = l;
    for (int k = 0; k < 20; ++k) { float* c[2] = {l.data(), r.data()}; p.process(c, 2, block); }   // (settled)
    if constexpr (std::is_same_v<P, sw::rv04::Processor>) p.setParam(change ? sw::rv04::Length : sw::rv04::Category, change ? 60.0 : 2.0);
    else if constexpr (std::is_same_v<P, sw::st05::Processor>) p.setParam(sw::st05::Room, 2.0);
    else p.setParam(sw::gt02::Cab, 5.0);
    long samples = 0;
    for (int k = 0; k < 200000 && (samples == 0 || p.rebuilding()); ++k) { float* c[2] = {l.data(), r.data()}; p.process(c, 2, block); samples += block; }
    return samples;
}
template <class P> void sameWhateverTheBlock(const char* name, int change) {
    long s[5]; const int blocks[5] = {32, 64, 256, 1024, 4096};
    for (int i = 0; i < 5; ++i) s[i] = samplesToBuild<P>(blocks[i], change);
    INFO(name << ": " << s[0] << " / " << s[1] << " / " << s[2] << " / " << s[3] << " / " << s[4] << " samples for blocks of 32 / 64 / 256 / 1024 / 4096");
    // the same number of 256-sample steps, seen at the end of a call: differences of at most the block
    for (int i = 0; i < 4; ++i) CHECK(std::labs(s[i] - s[2]) <= std::max(blocks[i], 256) + 256);
    CHECK(s[0] > 256);   // (it is a job of several steps, not an instant one)
}
}  // namespace

TEST_CASE("GridClock: a grid point every 64 samples of the stream, whatever the blocks") {
    for (const int block : {1, 7, 64, 100, 256, 1000}) {
        sw::GridClock c; long points = 0, samples = 0; int firstAt = -1;
        while (samples < 6400) {
            const int len = std::min(block, 6400 - static_cast<int>(samples));
            for (int done = 0; done < len;) { const int seg = std::min(len - done, c.toNext()); done += seg; if (c.advance(seg)) { ++points; if (firstAt < 0) firstAt = static_cast<int>(samples) + done; } }
            samples += len;
        }
        INFO("block " << block); CHECK(points == 100); CHECK(c.pos == 6400);
        if (block == 1 || block == 64) CHECK(firstAt == 64);
    }
}

TEST_CASE("RV04 / ST05 / GT02: the time a new IR takes (in samples) does not depend on the host's block size") {
    sameWhateverTheBlock<sw::rv04::Processor>("RV04 Category", 0);
    sameWhateverTheBlock<sw::rv04::Processor>("RV04 Length", 1);
    sameWhateverTheBlock<sw::st05::Processor>("ST05 Room", 0);
    sameWhateverTheBlock<sw::gt02::Processor>("GT02 Cab", 0);
}

namespace {
// the output of the plug-in's core for noise cut in blocks of `block`, with a parameter change at sample 3000 (the host cuts the audio there first): the same samples whatever the block
template <class P, class F> std::vector<float> outputWith(int block, F change, size_t total) {
    P p; p.prepare(kFs, 256); p.snapToTargets();
    const auto x = noise(-20.0, static_cast<double>(total) / kFs, 17); std::vector<float> y(total);
    auto run = [&](size_t from, size_t to) {
        for (size_t off = from; off < to;) { const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), to - off)); std::vector<float> l(x.begin() + static_cast<std::ptrdiff_t>(off), x.begin() + static_cast<std::ptrdiff_t>(off) + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); std::copy(l.begin(), l.end(), y.begin() + static_cast<std::ptrdiff_t>(off)); off += static_cast<size_t>(n); }
    };
    run(0, 3000); change(p); run(3000, total);
    return y;
}
template <class P, class F> void identicalWhateverTheBlock(const char* name, F change) {
    const size_t total = 60000;
    const auto ref = outputWith<P>(256, change, total);
    for (const int block : {32, 100, 64, 1024}) {
        const auto y = outputWith<P>(block, change, total);
        double worst = 0; for (size_t i = 0; i < total; ++i) worst = std::max(worst, static_cast<double>(std::abs(y[i] - ref[i])));
        INFO(name << ": blocks of " << block << " against 256: the largest difference " << worst); CHECK(worst < 1e-6);
    }
}
}  // namespace

TEST_CASE("RV04 / ST05 / GT02: the output is the same, sample for sample, whatever the blocks - also across the switch to a new IR") {
    identicalWhateverTheBlock<sw::rv04::Processor>("RV04", [](sw::rv04::Processor& p) { p.setParam(sw::rv04::Category, 2.0); p.setParam(sw::rv04::Length, 70.0); });
    identicalWhateverTheBlock<sw::st05::Processor>("ST05", [](sw::st05::Processor& p) { p.setParam(sw::st05::Room, 2.0); });
    identicalWhateverTheBlock<sw::gt02::Processor>("GT02", [](sw::gt02::Processor& p) { p.setParam(sw::gt02::Cab, 5.0); });
}
