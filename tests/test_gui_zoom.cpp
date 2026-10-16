// The window's zoom (the design's "100%" chip): the steps, the size the host is asked for, the size a dragged window edge snaps to, and the remembered choice.
#include "doctest.h"
#include "gui_zoom.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace sw::gui;

TEST_CASE("zoom: the chip steps through 75 / 100 / 125 / 150 / 200 % and wraps") {
    CHECK(zoom::nextPercent(100) == 125);
    CHECK(zoom::nextPercent(125) == 150);
    CHECK(zoom::nextPercent(150) == 200);
    CHECK(zoom::nextPercent(200) == 75);
    CHECK(zoom::nextPercent(75) == 100);
    // a window dragged to a size between the steps goes to the next step above it; beyond the last, back to the first
    CHECK(zoom::nextPercent(110) == 125);
    CHECK(zoom::nextPercent(126) == 150);
    CHECK(zoom::nextPercent(60) == 75);
    CHECK(zoom::nextPercent(500) == 75);
    CHECK(zoom::nextPercent(0) == 75);
    CHECK(zoom::nextPercent(-40) == 75);
}

TEST_CASE("zoom: the factor stays within 50 % .. 200 % and survives garbage") {
    CHECK(zoom::clamp(1.0) == doctest::Approx(1.0));
    CHECK(zoom::clamp(0.1) == doctest::Approx(0.5));
    CHECK(zoom::clamp(7.0) == doctest::Approx(2.0));
    CHECK(zoom::clamp(std::nan("")) == doctest::Approx(1.0));
    CHECK(zoom::clamp(-3.0) == doctest::Approx(0.5));
    CHECK(zoom::clamp(INFINITY) == doctest::Approx(2.0));
}

TEST_CASE("zoom: the size of the window for a factor, and the factor of a size the host proposes") {
    const double bw = 1180, bh = 722;
    uint32_t w = 0, h = 0;
    zoom::sizeFor(1.0, bw, bh, w, h); CHECK(w == 1180); CHECK(h == 722);
    zoom::sizeFor(1.5, bw, bh, w, h); CHECK(w == 1770); CHECK(h == 1083);
    zoom::sizeFor(0.75, bw, bh, w, h); CHECK(w == 885); CHECK(h == 542);   // 541.5 rounds up
    zoom::sizeFor(9.0, bw, bh, w, h); CHECK(w == 2360); CHECK(h == 1444);  // clamped to 200 %
    // the host's proposal: the smaller of the two ratios keeps the whole design visible
    CHECK(zoom::fromSize(1770, 1083, bw, bh) == doctest::Approx(1.5).epsilon(0.001));
    CHECK(zoom::fromSize(2000, 722, bw, bh) == doctest::Approx(1.0).epsilon(0.001));    // wide: the height decides
    CHECK(zoom::fromSize(1180, 2000, bw, bh) == doctest::Approx(1.0).epsilon(0.001));   // tall: the width decides
    CHECK(zoom::fromSize(100, 100, bw, bh) == doctest::Approx(0.5));                    // too small: 50 %
    CHECK(zoom::fromSize(9000, 9000, bw, bh) == doctest::Approx(2.0));                  // too large: 200 %
    CHECK(zoom::fromSize(0, 0, bw, bh) == doctest::Approx(0.5));
    // a size made from a factor reads back as that factor, for every step (so a host that calls set_size with what we asked for changes nothing)
    for (int pct : {50, 75, 100, 125, 150, 175, 200}) {
        zoom::sizeFor(pct / 100.0, bw, bh, w, h);
        CHECK(zoom::fromSize(w, h, bw, bh) == doctest::Approx(pct / 100.0).epsilon(0.002));
    }
    // a host-side scale (Windows, 125 % display): the base size is the design's times that scale, and the factor is the same
    const double k = 1.25;
    zoom::sizeFor(1.5, bw * k, bh * k, w, h); CHECK(w == 2213); CHECK(h == 1354);   // 2212.5 and 1353.75
    CHECK(zoom::fromSize(w, h, bw * k, bh * k) == doctest::Approx(1.5).epsilon(0.002));
}

TEST_CASE("zoom: the choice is kept in <Documents>/SW AUDIO/zoom.txt for every product") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sw_zoom_test";
    std::error_code ec; fs::remove_all(dir, ec);
    CHECK(zoom::load(dir.string()) == 100);               // nothing there yet: 100 %
    CHECK(zoom::load("") == 100);                         // no Documents folder
    CHECK(zoom::save(dir.string(), 150));
    CHECK(zoom::load(dir.string()) == 150);
    CHECK(zoom::save(dir.string(), 75));
    CHECK(zoom::load(dir.string()) == 75);
    CHECK(!zoom::save("", 125));                          // nowhere to write: false, no crash
    // a file that is not a number, or a number out of range, is not trusted
    auto put = [&](const char* text) { std::ofstream f(fs::path(dir) / "SW AUDIO" / "zoom.txt", std::ios::binary | std::ios::trunc); f << text; };
    put("banana\n"); CHECK(zoom::load(dir.string()) == 100);
    put("9000\n");   CHECK(zoom::load(dir.string()) == 200);
    put("3\n");      CHECK(zoom::load(dir.string()) == 50);
    put("");         CHECK(zoom::load(dir.string()) == 100);
    put("125 and more text"); CHECK(zoom::load(dir.string()) == 125);
    fs::remove_all(dir, ec);
}
