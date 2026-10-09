// The same input cut into blocks of different sizes must give the same output: the buffer size of a DAW, an offline bounce, and a host that splits a block at every automation point all cut the audio differently.
// worstDb(make, l, r) runs blocks of 256 as the reference and 1, 7, 64, 509, 1024 and a mix of sizes against it, and returns the worst difference in dB relative to the peak of the reference (-400: bit-exact).
// `make` returns a prepared processor. `events` (optional): (sample position, change) pairs; the change is applied at that absolute sample in every run, the block being cut there as the adapter does.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>
namespace bsi {
template <class P, class = void> struct HasSc : std::false_type {};
template <class P> struct HasSc<P, std::void_t<decltype(std::declval<P&>().processWithSidechain(static_cast<float**>(nullptr), 0, 0, static_cast<const float* const*>(nullptr), 0))>> : std::true_type {};
template <class P> using Events = std::vector<std::pair<size_t, std::function<void(P&)>>>;

// `sc` (optional): the two channels of an external sidechain, run through processWithSidechain
template <class P> void runCut(P& p, std::vector<float>& l, std::vector<float>& r, const std::vector<int>& sizes, const Events<P>& events, const std::vector<float>* sc = nullptr) {
    size_t pos = 0, k = 0, ev = 0;
    while (pos < l.size()) {
        int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(sizes[k++ % sizes.size()]), l.size() - pos));
        while (ev < events.size() && events[ev].first <= pos) { events[ev].second(p); ++ev; }
        if (ev < events.size() && events[ev].first < pos + static_cast<size_t>(n)) n = static_cast<int>(events[ev].first - pos);
        float* c[2] = {l.data() + pos, r.data() + pos};
        if constexpr (HasSc<P>::value) {
            if (sc) { const float* s2[2] = {sc[0].data() + pos, sc[1].data() + pos}; p.processWithSidechain(c, 2, n, s2, 2); }
            else p.process(c, 2, n);
        } else {
            p.process(c, 2, n);
        }
        pos += static_cast<size_t>(n);
    }
}

template <class Make, class P = decltype(std::declval<Make>()())>
double worstDb(Make make, const std::vector<float>& l0, const std::vector<float>& r0, const Events<P>& events = {}, const std::vector<float>* sc = nullptr) {
    std::vector<float> rl = l0, rr = r0; { P p = make(); runCut(p, rl, rr, {256}, events, sc); }
    double peak = 1e-9; for (float x : rl) peak = std::max(peak, static_cast<double>(std::fabs(x))); for (float x : rr) peak = std::max(peak, static_cast<double>(std::fabs(x)));
    const std::vector<std::vector<int>> cuts = {{1}, {7}, {64}, {509}, {1024}, {1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987}};
    double worst = -400.0;
    for (const auto& cut : cuts) {
        std::vector<float> l = l0, r = r0; { P p = make(); runCut(p, l, r, cut, events, sc); }
        double err = 0; for (size_t i = 0; i < l.size(); ++i) err = std::max({err, static_cast<double>(std::fabs(l[i] - rl[i])), static_cast<double>(std::fabs(r[i] - rr[i]))});
        worst = std::max(worst, 20.0 * std::log10(std::max(err / peak, 1e-20)));
    }
    return worst;
}
}  // namespace bsi
