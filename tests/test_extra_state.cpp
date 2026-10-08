// SW AUDIO: the products' extra state blocks (saveExtra / loadExtra: fixed filters, scene maps, a recording folder, an IR, kept values,
// a seed) come from project files, which people share: random and damaged blocks never crash, never make the sound NaN or blow up,
// and values that are not numbers do not get in.
#include "doctest.h"
#include "tu.hpp"
#include "lv02/lv02.hpp"
#include "lv27/lv27.hpp"
#include "lv30/lv30.hpp"
#include "rv04/rv04.hpp"
#include "sa02/sa02.hpp"
#include "ut01/ut01.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <vector>

namespace {
template <class P> bool finiteAfter(P& p, unsigned seed) {
    auto x = tu::noise(-20.0, 0.25, seed);
    const auto y = tu::run(p, x);
    for (float v : y) if (!std::isfinite(v) || std::abs(v) > 1e4f) return false;
    return true;
}
template <class P> void fuzzExtra(unsigned seed, bool header4 = false, const char* magic = nullptr) {
    std::mt19937 rng(seed);
    for (int it = 0; it < 60; ++it) {
        std::vector<uint8_t> b(rng() % 64 + (rng() % 4 == 0 ? rng() % 4000 : 0));
        for (auto& c : b) c = static_cast<uint8_t>(rng() & 0xFF);
        if (header4 && magic && b.size() >= 4) std::memcpy(b.data(), magic, 4);
        P p;
        p.loadExtra(b.data(), b.size());        // before prepare (a state loaded before activation)
        p.prepare(48000, 256);
        p.loadExtra(b.data(), b.size());        // and after
        CHECK(finiteAfter(p, seed + static_cast<unsigned>(it)));
        std::vector<uint8_t> out;
        p.saveExtra(out);                       // what it keeps can be saved again
    }
}
}  // namespace

TEST_CASE("EXTRA STATE: random blocks never crash and leave the sound finite (LV02, LV27, RV04, SA02, UT01)") {
    fuzzExtra<sw::lv02::Processor>(11);
    fuzzExtra<sw::lv27::Processor>(12);
    fuzzExtra<sw::rv04::Processor>(13, true, "R4IR");
    fuzzExtra<sw::sa02::Processor>(14);
    fuzzExtra<sw::ut01::Processor>(15);
}

TEST_CASE("EXTRA STATE: RV04 — an IR with values that are not numbers, or an impossible sample rate, plays finite and stays bounded") {
    for (float rate : {std::nanf(""), 1e-30f, -48000.0f, 0.0f, 1e30f, INFINITY}) {
        std::vector<uint8_t> b = {'R', '4', 'I', 'R', 2, 64, 0, 0, 0};
        uint8_t rb[4]; std::memcpy(rb, &rate, 4); b.insert(b.end(), rb, rb + 4);
        for (int i = 0; i < 128; ++i) {
            const float v = i == 0 ? 1.0f : (i % 3 == 0 ? std::nanf("") : (i % 3 == 1 ? 1e30f : -INFINITY));
            uint8_t vb[4]; std::memcpy(vb, &v, 4); b.insert(b.end(), vb, vb + 4);
        }
        sw::rv04::Processor p;
        p.setParam(sw::rv04::Category, sw::rv04::Custom);
        p.prepare(48000, 256);
        p.loadExtra(b.data(), b.size());
        auto x = tu::noise(-20.0, 1.5, 3);
        const auto y = tu::run(p, x);
        bool ok = true; for (float v : y) ok = ok && std::isfinite(v) && std::abs(v) < 1e4f;
        CHECK_MESSAGE(ok, "rate " << rate);
    }
}

TEST_CASE("EXTRA STATE: UT01 — kept values that are not numbers are not kept") {
    std::vector<uint8_t> b;
    for (int k = 0; k < 16; ++k) { b.push_back(1); const float v = std::nanf(""); uint8_t vb[4]; std::memcpy(vb, &v, 4); b.insert(b.end(), vb, vb + 4); }
    sw::ut01::Processor p;
    p.loadExtra(b.data(), b.size());
    std::vector<uint8_t> out;
    p.saveExtra(out);
    for (size_t k = 0; k + 5 <= out.size(); k += 5) { float f; std::memcpy(&f, out.data() + k + 1, 4); CHECK(std::isfinite(f)); }
}

TEST_CASE("EXTRA STATE: LV30 — a recording folder from a project must be an absolute path without '..' or control characters") {
    namespace fs = std::filesystem;
    auto load = [](const std::string& s) {
        sw::lv30::Processor p;
        std::vector<uint8_t> b = {static_cast<uint8_t>(s.size() & 255), static_cast<uint8_t>(s.size() >> 8)};
        b.insert(b.end(), s.begin(), s.end());
        p.loadExtra(b.data(), b.size());
        return p.folder();
    };
    const std::string ok = (fs::temp_directory_path() / "SW Recordings").u8string();
    CHECK(load(ok) == ok);
    CHECK(load("relative/folder").empty());
    CHECK(load(ok + "/../../elsewhere").empty());
    CHECK(load(ok + std::string("\n/x")).empty());
    CHECK(load(std::string("/tmp/a\0b", 8)).empty());
    CHECK(load("").empty());
}
