// SW AUDIO core: the user preset file (.swpreset). Written by the products, read as untrusted input (users share presets):
// round trip, the limits, values clamped into range, unknown lines skipped, damaged files refused without a crash (random mutations),
// names cleaned for the screen and for the file system, numbers written and read the same in every locale, atomic writes.
#include "doctest.h"
#include "sw/preset_file.hpp"
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace sw;
namespace pf = sw::presetfile;

namespace {
const std::vector<ParamSpec>& table() {
    static const std::vector<ParamSpec> t = {
        {"t01.gain", "Gain", -24.0, 24.0, 0.0, Curve::Lin, 1.0, {}, "dB"},
        {"t01.freq", "Freq", 20.0, 20000.0, 1000.0, Curve::Log, 1.0, {}, "Hz"},
        {"t01.mode", "Mode", 0.0, 2.0, 0.0, Curve::Step, 1.0, {0, 1, 2}, "", {"Poly", "Mono", "Legato"}},
        {"t01.ratio", "Ratio", 1.0, 20.0, 4.0, Curve::Skew, 0.5},
        {"t01.steps", "Steps", 0.0, 64.0, 16.0, Curve::Step, 1.0, {1, 2, 4, 8, 16, 32, 64}},   // stepped without labels
        {"t01.select", "Select", 0.0, 3.0, 0.0, Curve::Step, 1.0, {0, 1, 2, 3}, "", {"Init", "A", "B", "C"}},
    };
    return t;
}
std::vector<double> defaults() { std::vector<double> v; for (const auto& s : table()) v.push_back(s.def); return v; }
std::string sample() {
    std::vector<double> v = {-3.25, 440.0, 2.0, 7.5, 64.0, 2.0};
    return pf::write("t01", {"Night Lead", "LEAD", "someone", "a comment"}, table(), v, 5);
}
double valueOf(const pf::Parsed& p, int i) { for (const auto& x : p.values) if (x.first == i) return x.second; return NAN; }
bool has(const pf::Parsed& p, int i) { for (const auto& x : p.values) if (x.first == i) return true; return false; }
}  // namespace

TEST_CASE("PRESET FILE: a written preset reads back exactly (labels for steps, exact numbers, the meta data)") {
    const std::string s = sample();
    CHECK(s.rfind("SW-PRESET 1\n", 0) == 0);
    CHECK(s.find("t01.mode=Legato\n") != std::string::npos);    // a step with labels is written by its label
    CHECK(s.find("t01.steps=64\n") != std::string::npos);       // a step without labels by its value
    CHECK(s.find("t01.select") == std::string::npos);           // the skipped index (a preset selector) is not written
    pf::Parsed p; std::string err;
    REQUIRE(pf::read(s, "t01", table(), p, err));
    CHECK(err.empty());
    CHECK(p.meta.name == "Night Lead");
    CHECK(p.meta.category == "LEAD");
    CHECK(p.meta.author == "someone");
    CHECK(p.meta.comment == "a comment");
    CHECK(p.values.size() == 5);
    CHECK(valueOf(p, 0) == -3.25);
    CHECK(valueOf(p, 1) == 440.0);
    CHECK(valueOf(p, 2) == 2.0);
    CHECK(valueOf(p, 3) == 7.5);
    CHECK(valueOf(p, 4) == 64.0);
    CHECK_FALSE(has(p, 5));
    CHECK(p.skipped == 0);
    // every double survives (17 significant digits)
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-24.0, 24.0);
    for (int i = 0; i < 200; ++i) {
        std::vector<double> v = defaults(); v[0] = u(rng);
        pf::Parsed q; REQUIRE(pf::read(pf::write("t01", {}, table(), v), "t01", table(), q, err));
        CHECK(valueOf(q, 0) == v[0]);
    }
}

TEST_CASE("PRESET FILE: what a reader tolerates (CRLF, a BOM, comments, blank lines, unknown ids, numbers for steps) and what it repairs") {
    pf::Parsed p; std::string err;
    const std::string crlf = "\xEF\xBB\xBFSW-PRESET 1\r\nproduct=t01\r\nname=A\r\n# a comment\r\n\r\nt01.gain=1.5\r\nt01.mode=1\r\nt01.future=3\r\n";
    REQUIRE(pf::read(crlf, "t01", table(), p, err));
    CHECK(p.meta.name == "A");
    CHECK(valueOf(p, 0) == 1.5);
    CHECK(valueOf(p, 2) == 1.0);       // a step given by its value
    CHECK(p.skipped == 1);             // the unknown id (a newer version's parameter) is skipped
    // out of range: clamped into the range; a step that does not exist, a bad label, a number that is not finite: skipped
    const std::string bad = "SW-PRESET 1\nproduct=t01\nt01.gain=99\nt01.freq=-5\nt01.mode=Unison\nt01.steps=3\nt01.ratio=nan\nt01.ratio=inf\nt01.gain2=1\n";
    REQUIRE(pf::read(bad, "t01", table(), p, err));
    CHECK(valueOf(p, 0) == 24.0);
    CHECK(valueOf(p, 1) == 20.0);
    CHECK_FALSE(has(p, 2));
    CHECK_FALSE(has(p, 4));
    CHECK_FALSE(has(p, 3));
    CHECK(p.skipped == 5);
    // the same id twice: the last one counts, once
    REQUIRE(pf::read("SW-PRESET 1\nproduct=t01\nt01.gain=1\nt01.gain=2\n", "t01", table(), p, err));
    CHECK(p.values.size() == 1);
    CHECK(valueOf(p, 0) == 2.0);
    // the preset selector is never taken from a file
    REQUIRE(pf::read("SW-PRESET 1\nproduct=t01\nt01.select=B\n", "t01", table(), p, err, 5));
    CHECK_FALSE(has(p, 5));
    // numbers: strict (no trailing text, no hex, no spaces inside)
    double x = 0;
    CHECK(pf::parseNumber("1.5", x)); CHECK(x == 1.5);
    CHECK(pf::parseNumber("-2e-3", x)); CHECK(x == -0.002);
    CHECK(pf::parseNumber(" 7 ", x)); CHECK(x == 7.0);
    CHECK_FALSE(pf::parseNumber("1.5dB", x));
    CHECK_FALSE(pf::parseNumber("0x10", x));
    CHECK_FALSE(pf::parseNumber("1 2", x));
    CHECK_FALSE(pf::parseNumber("", x));
    CHECK_FALSE(pf::parseNumber("nan", x));
    CHECK_FALSE(pf::parseNumber("1e999", x));
}

TEST_CASE("PRESET FILE: what a reader refuses (another product, another format, a newer format, too large, binary)") {
    pf::Parsed p; std::string err;
    CHECK_FALSE(pf::read("", "t01", table(), p, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(pf::read("hello\n", "t01", table(), p, err));
    CHECK_FALSE(pf::read("SW-PRESET 1\nproduct=dy01\nt01.gain=1\n", "t01", table(), p, err));    // another product's preset
    CHECK_FALSE(pf::read("SW-PRESET 1\nt01.gain=1\n", "t01", table(), p, err));                  // no product line
    CHECK_FALSE(pf::read("SW-PRESET 2\nproduct=t01\n", "t01", table(), p, err));                 // written by a newer version
    CHECK(err.find("newer") != std::string::npos);
    CHECK_FALSE(pf::read("SW-PRESET x\nproduct=t01\n", "t01", table(), p, err));
    std::string big = "SW-PRESET 1\nproduct=t01\n"; big += "# " + std::string(pf::kMaxFileBytes, 'a') + "\n";
    CHECK_FALSE(pf::read(big, "t01", table(), p, err));
    std::string longLine = "SW-PRESET 1\nproduct=t01\nname=" + std::string(pf::kMaxLineBytes + 10, 'b') + "\n";
    CHECK_FALSE(pf::read(longLine, "t01", table(), p, err));
    std::string many = "SW-PRESET 1\nproduct=t01\n"; for (int i = 0; i < pf::kMaxLines + 5; ++i) many += "\n";
    CHECK_FALSE(pf::read(many, "t01", table(), p, err));
    CHECK_FALSE(pf::read(std::string("SW-PRESET 1\nproduct=t01\nname=a\0b\n", 33), "t01", table(), p, err));   // a NUL byte: binary
    CHECK_FALSE(pf::read("SW-PRESET 1\nproduct=t01\nno equals sign\n", "t01", table(), p, err));
    CHECK(p.values.empty());   // a refused file gives nothing
}

TEST_CASE("PRESET FILE: damaged files never crash the reader, and whatever it accepts is in range (random mutations)") {
    const std::string good = sample();
    std::mt19937 rng(1234);
    int accepted = 0;
    for (int it = 0; it < 4000; ++it) {
        std::string s = good;
        const int edits = 1 + static_cast<int>(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            if (s.empty()) break;
            const size_t at = rng() % s.size();
            switch (rng() % 6) {
                case 0: s[at] = static_cast<char>(rng() & 0xFF); break;                                       // a byte
                case 1: s.erase(at, 1 + rng() % 16); break;                                                    // a cut
                case 2: s.insert(at, std::string(1 + rng() % 8, static_cast<char>(rng() & 0xFF))); break;     // inserted bytes
                case 3: s.resize(at); break;                                                                   // truncated
                case 4: { const size_t from = rng() % s.size(); s.insert(at, s.substr(from, 1 + rng() % 40)); break; }   // a piece copied
                default: s.insert(at, (rng() & 1) ? "=1e308\n" : "\xC3\x28=\xFF\n"); break;
            }
        }
        pf::Parsed p; std::string err;
        if (!pf::read(s, "t01", table(), p, err)) { CHECK_FALSE(err.empty()); continue; }
        ++accepted;
        for (const auto& v : p.values) {
            REQUIRE(v.first >= 0); REQUIRE(v.first < static_cast<int>(table().size()));
            const ParamSpec& sp = table()[static_cast<size_t>(v.first)];
            CHECK(std::isfinite(v.second));
            CHECK(v.second >= sp.min); CHECK(v.second <= sp.max);
            if (sp.curve == Curve::Step) CHECK(std::find(sp.steps.begin(), sp.steps.end(), v.second) != sp.steps.end());
        }
        CHECK(pf::cleanText(p.meta.name, pf::kMaxNameChars) == p.meta.name);   // the meta data is clean
        CHECK(pf::cleanText(p.meta.author, pf::kMaxTextChars) == p.meta.author);
    }
    MESSAGE("accepted " << accepted << " of 4000 damaged files");
    CHECK(accepted > 100);   // the mutations are not all fatal (the reader tolerates what it can)
}

TEST_CASE("PRESET FILE: names are cleaned for the screen and for the file system") {
    CHECK(pf::cleanText("  Night\tLead\n ", 64) == "Night Lead");                 // control characters become spaces, then trimmed and collapsed
    CHECK(pf::cleanText("a\xC3\x28" "b", 64) == "a(b");                          // a lead byte without its continuation removed (the "(" stays)
    CHECK(pf::cleanText("\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88", 64) == "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");   // テスト kept
    CHECK(pf::cleanText("\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88", 2) == "\xE3\x83\x86\xE3\x82\xB9");                 // cut at a character, not a byte
    CHECK(pf::cleanText("\xF0\x9F\x8E\xB9 Keys", 64) == "\xF0\x9F\x8E\xB9 Keys");   // 4-byte characters kept
    CHECK(pf::cleanText("\xED\xA0\x80x", 64) == "x");                            // a UTF-16 surrogate encoded in UTF-8 is not valid
    CHECK(pf::cleanText("\xC0\xAF", 64) == "");                                  // an overlong encoding (of '/') is not valid
    CHECK(pf::cleanText("a\x7F" "b", 64) == "a b");
    CHECK(pf::safeFileName("Night Lead") == "Night Lead");
    CHECK(pf::safeFileName("../../etc/passwd") == "etc_passwd");
    CHECK(pf::safeFileName("a<b>c:d\"e/f\\g|h?i*j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(pf::safeFileName("CON") == "CON_");
    CHECK(pf::safeFileName("com1") == "com1_");
    CHECK(pf::safeFileName("lpt9.txt") == "lpt9.txt_");
    CHECK(pf::safeFileName("name. . ") == "name");
    CHECK(pf::safeFileName("...") == "Untitled");
    CHECK(pf::safeFileName("") == "Untitled");
    CHECK(pf::safeFileName("\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88") == "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");
    CHECK(pf::safeFileName(std::string(300, 'x')).size() == 64);
}

TEST_CASE("PRESET FILE: numbers are written and read with a dot in every locale") {
    const char* tried[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "German_Germany.1252"};
    const std::string before = std::setlocale(LC_NUMERIC, nullptr);
    bool any = false;
    for (const char* l : tried) if (std::setlocale(LC_NUMERIC, l)) { any = true; break; }
    if (!any) MESSAGE("no locale with a decimal comma here: checked in the C locale only");
    CHECK(pf::formatNumber(1.5) == "1.5");
    CHECK(pf::formatNumber(-0.1) == "-0.1");                    // the shortest text that reads back exactly
    CHECK(pf::formatNumber(1.0 / 3.0) == "0.3333333333333333");
    double x = 0; CHECK(pf::parseNumber("2.25", x)); CHECK(x == 2.25);
    CHECK_FALSE(pf::parseNumber("2,25", x));
    std::setlocale(LC_NUMERIC, before.c_str());
}

TEST_CASE("PRESET FILE: files are written whole or not at all, read with a size limit, and listed") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sw_preset_test_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir / "Pads");
    std::string err;
    const std::string a = (dir / "A.swpreset").u8string();
    REQUIRE(pf::writeFileAtomic(a, sample(), false, err));
    std::string back;
    REQUIRE(pf::readFile(a, back, err));
    CHECK(back == sample());
    CHECK_FALSE(pf::writeFileAtomic(a, "other", false, err));   // no overwrite unless asked
    CHECK_FALSE(err.empty());
    REQUIRE(pf::readFile(a, back, err)); CHECK(back == sample());
    REQUIRE(pf::writeFileAtomic(a, "other", true, err));
    REQUIRE(pf::readFile(a, back, err)); CHECK(back == "other");
    // no temporary files left behind
    int n = 0; for (const auto& e : fs::directory_iterator(dir)) n += e.is_regular_file(); CHECK(n == 1);
    // a file over the limit is not read
    { std::ofstream f(dir / "big.swpreset", std::ios::binary); f << std::string(pf::kMaxFileBytes + 1, 'x'); }
    CHECK_FALSE(pf::readFile((dir / "big.swpreset").u8string(), back, err));
    CHECK_FALSE(pf::readFile((dir / "missing.swpreset").u8string(), back, err));
    // a UTF-8 name works on every platform
    const std::string jp = (dir / fs::u8path("\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.swpreset")).u8string();
    REQUIRE(pf::writeFileAtomic(jp, sample(), false, err));
    { std::ofstream f(dir / "Pads" / "B.swpreset"); f << "x"; }
    { std::ofstream f(dir / "notes.txt"); f << "x"; }
    const auto files = pf::listFiles(dir.u8string(), "swpreset");
    CHECK(files.size() == 4);   // A, big, テスト, Pads/B (not notes.txt)
    for (const auto& f : files) CHECK(f.size() > 9);
    CHECK(pf::listFiles((dir / "nothing").u8string(), "swpreset").empty());
    fs::remove_all(dir);
}
