#include "doctest.h"
#include "sw/loudness.hpp"
#include <cmath>
#include <vector>
using namespace sw;
namespace { const double kPi = 3.14159265358979323846; }

TEST_CASE("K-weighting coefficients at 48 kHz match ITU-R BS.1770 tables") {
    KWeighting k; k.setup(48000.0);
    const auto& s = k.shelf(); const auto& h = k.highpass();
    CHECK(s.b0 == doctest::Approx(1.53512485958697).epsilon(1e-6));
    CHECK(s.b1 == doctest::Approx(-2.69169618940638).epsilon(1e-6));
    CHECK(s.b2 == doctest::Approx(1.19839281085285).epsilon(1e-6));
    CHECK(s.a1 == doctest::Approx(-1.69065929318241).epsilon(1e-6));
    CHECK(s.a2 == doctest::Approx(0.73248077421585).epsilon(1e-6));
    CHECK(h.b0 == doctest::Approx(1.0)); CHECK(h.b1 == doctest::Approx(-2.0)); CHECK(h.b2 == doctest::Approx(1.0));
    CHECK(h.a1 == doctest::Approx(-1.99004745483398).epsilon(1e-6));
    CHECK(h.a2 == doctest::Approx(0.99007225036621).epsilon(1e-6));
}
TEST_CASE("a 997 Hz full-scale sine in one channel reads -3.01 LUFS short-term") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        LoudnessMeter m; m.setup(fs, 2);
        std::vector<float> l(static_cast<size_t>(fs) * 4), r(l.size(), 0.0f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(std::sin(2 * kPi * 997.0 * i / fs));
        const float* ch[2] = {l.data(), r.data()};
        m.process(ch, 2, static_cast<int>(l.size()));
        CHECK(m.shortTerm() == doctest::Approx(-3.01).epsilon(0.02));
        CHECK(m.momentary() == doctest::Approx(-3.01).epsilon(0.02));
    }
}
TEST_CASE("a -20 dBFS 997 Hz sine in both channels reads -20 LUFS") {
    LoudnessMeter m; m.setup(48000.0, 2);
    std::vector<float> x(48000 * 4);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.1 * std::sin(2 * kPi * 997.0 * i / 48000.0));
    const float* ch[2] = {x.data(), x.data()};
    m.process(ch, 2, static_cast<int>(x.size()));
    CHECK(std::abs(m.shortTerm() + 20.0) < 0.05);
}
TEST_CASE("silence reads as very low loudness, not NaN") {
    LoudnessMeter m; m.setup(48000.0, 2);
    std::vector<float> z(48000, 0.0f);
    const float* ch[2] = {z.data(), z.data()};
    m.process(ch, 2, 48000);
    CHECK(m.shortTerm() < -100.0);
    CHECK(std::isfinite(m.shortTerm()));
}
