#include "doctest.h"
#include "gui_presets.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
using namespace sw::gui;
namespace fs = std::filesystem;
namespace {
std::string tmpDir() { static int n = 0; const fs::path p = fs::temp_directory_path() / ("sw_presets_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(n++)); fs::remove_all(p); return p.string(); }
}

TEST_CASE("presets: the folder, names and the percent-encoding the page uses") {
    CHECK(presets::dirFor("DY08", "/home/me/Documents") == "/home/me/Documents/SW AUDIO/Presets/DY08");
    CHECK(presets::dirFor("DY08", "C:\\Users\\me\\OneDrive\\Documents\\") == "C:\\Users\\me\\OneDrive\\Documents/SW AUDIO/Presets/DY08");
    CHECK(presets::dirFor("DY08", "") == "");
    CHECK(presets::cleanName("  Vocal bus 2  ") == "Vocal bus 2");
    CHECK(presets::cleanName("a/b\\c:d*e?f\"g<h>i|j") == "a-b-c-d-e-f-g-h-i-j");
    CHECK(presets::cleanName("..") == ""); CHECK(presets::cleanName("") == ""); CHECK(presets::cleanName("   ") == "");
    CHECK(presets::cleanName(std::string(200, 'x')).size() == 60);
    CHECK(presets::cleanName("ボーカル 明るめ") == "ボーカル 明るめ");   // UTF-8 stays
    CHECK(presets::percentDecode("Vocal%20bus%202") == "Vocal bus 2");
    CHECK(presets::percentDecode("%E3%83%9C%E3%83%BC") == "ボー");
    CHECK(presets::percentDecode("100%") == "100%");        // a lone percent stays
    CHECK(presets::percentDecode("a%2") == "a%2"); CHECK(presets::percentDecode("%zz") == "%zz");
}
TEST_CASE("presets: save, list (sorted), load, overwrite, remove") {
    const std::string d = tmpDir();
    CHECK(presets::list(d).empty());   // no folder yet
    const std::string body = "dy08.thresh=-18.5;dy08.ratio=4;dy08.attack=0.0001;dy08.mix=100";
    REQUIRE(presets::save(d, "Vocal bus", "DY08", body)); REQUIRE(presets::save(d, "ボーカル", "DY08", body)); REQUIRE(presets::save(d, "apple", "DY08", "dy08.ratio=2"));
    const auto l = presets::list(d); REQUIRE(l.size() == 3); CHECK(l[0] == "apple"); CHECK(l[1] == "Vocal bus"); CHECK(l[2] == "ボーカル");   // case-insensitive order; the rest by bytes
    std::string got; REQUIRE(presets::load(d, "Vocal bus", got)); CHECK(got == body);
    REQUIRE(presets::load(d, "ボーカル", got)); CHECK(got == body);
    REQUIRE(presets::save(d, "Vocal bus", "DY08", "dy08.ratio=8")); REQUIRE(presets::load(d, "Vocal bus", got)); CHECK(got == "dy08.ratio=8");   // overwritten
    CHECK(presets::list(d).size() == 3);
    CHECK_FALSE(presets::load(d, "nothing", got)); CHECK_FALSE(presets::remove(d, "nothing"));
    REQUIRE(presets::remove(d, "apple")); CHECK(presets::list(d).size() == 2); CHECK_FALSE(presets::load(d, "apple", got));
    // the file is plain text with a header; a file of another product or a damaged one is not loaded
    { std::ifstream f(fs::u8path(d) / fs::u8path("Vocal bus.swpreset"), std::ios::binary); std::string l1; std::getline(f, l1); CHECK(l1 == "SWPRESET 1 DY08"); }
    { std::ofstream f(fs::u8path(d) / fs::u8path("broken.swpreset"), std::ios::binary); f << "garbage\nx=1\n"; }
    CHECK_FALSE(presets::load(d, "broken", got));
    { std::ofstream f(fs::u8path(d) / fs::u8path("notapreset.txt"), std::ios::binary); f << "x"; }
    CHECK(presets::list(d).size() == 3);   // broken is listed (it is a .swpreset file), the .txt is not
    fs::remove_all(d);
}
TEST_CASE("presets: bodies and names that must be refused") {
    const std::string d = tmpDir();
    CHECK_FALSE(presets::save(d, "", "DY08", "a=1")); CHECK_FALSE(presets::save(d, "..", "DY08", "a=1")); CHECK_FALSE(presets::save("", "x", "DY08", "a=1"));
    CHECK_FALSE(presets::save(d, "x", "DY08", "a=1\nb=2"));         // the body is one line: ids, numbers, ';' only
    CHECK_FALSE(presets::save(d, "x", "DY08", "a=<script>"));
    CHECK_FALSE(presets::save(d, "x", "DY08", std::string(70000, 'a')));
    CHECK_FALSE(presets::save(d, "x", "DY/08", "a=1"));              // the product code is letters and digits
    CHECK(presets::list(d).empty());
    CHECK(presets::save(d, "../escape", "DY08", "a=1"));              // the name is cleaned: it stays inside the folder
    CHECK(presets::list(d).size() == 1); CHECK(fs::exists(fs::u8path(d) / fs::u8path("..-escape.swpreset"))); CHECK_FALSE(fs::exists(fs::u8path(d) / ".." / "escape.swpreset"));
    fs::remove_all(d);
}
