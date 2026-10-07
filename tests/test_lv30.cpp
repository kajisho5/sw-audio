#include "doctest.h"
#include "lv30/lv30.hpp"
#include "tu.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace sw;
using namespace sw::lv30;
using namespace tu;
namespace fs = std::filesystem;
namespace {
using Set = std::vector<std::pair<int, double>>;
struct Tmp { fs::path dir; Tmp() { dir = fs::temp_directory_path() / ("sw_lv30_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); fs::create_directories(dir); } ~Tmp() { std::error_code ec; fs::remove_all(dir, ec); } };
Processor& prep(Processor& p, Set set = {}) { for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
std::vector<fs::path> files(const fs::path& d, const char* ext) { std::vector<fs::path> v; for (auto& e : fs::directory_iterator(d)) if (e.path().extension() == ext) v.push_back(e.path()); std::sort(v.begin(), v.end()); return v; }
std::vector<uint8_t> slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()}; }
uint32_t u32(const std::vector<uint8_t>& b, size_t o) { return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24); }
uint64_t u64(const std::vector<uint8_t>& b, size_t o) { return u32(b, o) | (static_cast<uint64_t>(u32(b, o + 4)) << 32); }
uint16_t u16(const std::vector<uint8_t>& b, size_t o) { return static_cast<uint16_t>(b[o] | (b[o + 1] << 8)); }
// feed `x` (both channels) in blocks of 256 at roughly real time is not needed: the ring takes 5 s; feed in pieces and let the writer drain
void feed(Processor& p, const std::vector<float>& x) {
    for (size_t off = 0; off < x.size(); off += 256) { const int n = static_cast<int>(std::min<size_t>(256, x.size() - off)); std::vector<float> l(x.begin() + off, x.begin() + off + n), r = l; float* c[2] = {l.data(), r.data()}; p.process(c, 2, n); if ((off / 256) % 200 == 199) std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
}
}

TEST_CASE("LV30 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[Container].labels == std::vector<std::string>{"WAV", "FLAC"}); CHECK(s[Rate].labels == std::vector<std::string>{"44.1 kHz", "48 kHz"}); CHECK(s[Rate].def == 1);
    CHECK(s[Bits].labels == std::vector<std::string>{"16 bit", "24 bit", "32 bit float"}); CHECK(s[Bits].def == 1); CHECK(s[AutoStart].def == 1); CHECK(s[SplitHourly].def == 1);
    Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV30 the sound passes untouched; nothing is written without a folder, even with Auto start On") {
    Processor p; prep(p); const auto x = noise(-20, 1.0, 3); const auto y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    CHECK_FALSE(p.recording()); CHECK_FALSE(p.start());
}
TEST_CASE("LV30 records a 24-bit WAV that reads back") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); prep(p, {{AutoStart, 0}}); CHECK_FALSE(p.recording()); REQUIRE(p.start()); CHECK(p.recording());
    const auto x = sine(-12, 3.0, 440); feed(p, x); p.stop(); CHECK_FALSE(p.recording());
    const auto w = files(t.dir, ".wav"); REQUIRE(w.size() == 1); const auto b = slurp(w[0]);
    REQUIRE(b.size() > 100); CHECK(std::memcmp(b.data(), "RIFF", 4) == 0); CHECK(std::memcmp(b.data() + 8, "WAVE", 4) == 0); CHECK(std::memcmp(b.data() + 12, "JUNK", 4) == 0); CHECK(std::memcmp(b.data() + 48, "fmt ", 4) == 0);
    CHECK(u16(b, 56) == 1); CHECK(u16(b, 58) == 2); CHECK(u32(b, 60) == 48000); CHECK(u16(b, 70) == 24); CHECK(std::memcmp(b.data() + 72, "data", 4) == 0);
    const uint32_t dataBytes = u32(b, 76); CHECK(dataBytes == 3u * 48000u * 2u * 3u); CHECK(u32(b, 4) == b.size() - 8);
    double maxErr = 0; for (size_t i = 5000; i < x.size(); i += 97) { const size_t o = 80 + i * 6; int v = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16); if (v & 0x800000) v -= 0x1000000; maxErr = std::max(maxErr, std::abs(v / 8388607.0 - x[i])); }
    CHECK(maxErr < 2e-7); CHECK(p.droppedFrames() == 0); CHECK(p.filesWritten() == 1);
}
TEST_CASE("LV30 16-bit and 32-bit float") {
    for (int bits : {Pcm16, Float32}) {
        Tmp t; Processor p; p.setFolder(t.dir.string()); prep(p, {{AutoStart, 0}, {Bits, static_cast<double>(bits)}}); REQUIRE(p.start()); const auto x = sine(-12, 1.0, 330); feed(p, x); p.stop();
        const auto w = files(t.dir, ".wav"); REQUIRE(w.size() == 1); const auto b = slurp(w[0]); CHECK(u16(b, 56) == (bits == Float32 ? 3 : 1)); CHECK(u16(b, 70) == (bits == Float32 ? 32 : 16));
        const size_t bps = bits == Float32 ? 4 : 2; CHECK(u32(b, 76) == 48000u * 2u * bps);
        for (size_t i = 3000; i < x.size(); i += 211) { const size_t o = 80 + i * 2 * bps; double v; if (bits == Float32) { float f; std::memcpy(&f, &b[o], 4); v = f; } else v = static_cast<int16_t>(u16(b, o)) / 32767.0; REQUIRE(std::abs(v - x[i]) < 5e-5); }
    }
}
TEST_CASE("LV30 the header is kept up to date while recording: a copy taken before stop is playable") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); prep(p, {{AutoStart, 0}}); REQUIRE(p.start()); feed(p, sine(-12, 2.0, 440)); std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const auto w = files(t.dir, ".wav"); REQUIRE(w.size() == 1); auto copy = slurp(w[0]); p.stop();
    REQUIRE(copy.size() > 80); const uint32_t d = u32(copy, 76); CHECK(d > 48000u * 6u); CHECK(d <= 2u * 48000u * 6u + 4u);   // the crash case: what the last header update saw is whole
}
TEST_CASE("LV30 marks go into the CSV and into the file as cue points") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); prep(p, {{AutoStart, 0}}); REQUIRE(p.start()); feed(p, sine(-12, 1.0, 440)); std::this_thread::sleep_for(std::chrono::milliseconds(100)); p.mark("Chorus"); feed(p, sine(-12, 1.0, 440)); std::this_thread::sleep_for(std::chrono::milliseconds(100)); p.mark(); feed(p, sine(-12, 0.5, 440)); p.stop();
    const auto c = files(t.dir, ".csv"); REQUIRE(c.size() == 1); const auto csv = slurp(c[0]); const std::string text(csv.begin(), csv.end());
    CHECK(text.rfind("elapsed_seconds,file,label\n", 0) == 0); CHECK(text.find(",Chorus\n") != std::string::npos); int lines = 0; for (char ch : text) if (ch == '\n') ++lines; CHECK(lines == 3);
    const auto w = files(t.dir, ".wav"); const auto b = slurp(w[0]); size_t cue = std::string::npos; for (size_t i = 80 + u32(b, 76); i + 4 < b.size(); ++i) if (std::memcmp(&b[i], "cue ", 4) == 0) { cue = i; break; }
    REQUIRE(cue != std::string::npos); CHECK(u32(b, cue + 8) == 2); const uint32_t pos1 = u32(b, cue + 12 + 4), pos2 = u32(b, cue + 12 + 24 + 4); CHECK(std::abs(static_cast<double>(pos1) - 48000.0) < 6000.0); CHECK(std::abs(static_cast<double>(pos2) - 96000.0) < 6000.0);
    const std::string all(b.begin(), b.end()); CHECK(all.find("Chorus") != std::string::npos); CHECK(all.find("Mark 2") != std::string::npos); CHECK(u32(b, 4) == b.size() - 8);
}
TEST_CASE("LV30 Split hourly starts a new file (here every 1.5 s)") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); p.setSplitSeconds(1.5); prep(p, {{AutoStart, 0}}); REQUIRE(p.start()); feed(p, sine(-12, 4.0, 440)); p.stop();
    const auto w = files(t.dir, ".wav"); REQUIRE(w.size() == 3); CHECK(p.filesWritten() == 3); CHECK(u32(slurp(w[0]), 76) == 72000u * 6u); CHECK(u32(slurp(w[2]), 76) == 48000u * 6u - 0u + 0u * 0u - 0u + 0u);   // 1.5 s, 1.5 s, 1 s
    auto off = Tmp{}; Processor q; q.setFolder(off.dir.string()); q.setSplitSeconds(1.0); prep(q, {{AutoStart, 0}, {SplitHourly, 0}}); REQUIRE(q.start()); feed(q, sine(-12, 3.0, 440)); q.stop(); CHECK(files(off.dir, ".wav").size() == 1);
}
TEST_CASE("LV30 over the limit the header becomes RF64 with a ds64 chunk") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); p.setRf64Threshold(200000); prep(p, {{AutoStart, 0}}); REQUIRE(p.start()); feed(p, sine(-12, 2.0, 440)); p.stop();
    const auto w = files(t.dir, ".wav"); REQUIRE(w.size() == 1); const auto b = slurp(w[0]); CHECK(std::memcmp(b.data(), "RF64", 4) == 0); CHECK(u32(b, 4) == 0xFFFFFFFFu); CHECK(std::memcmp(b.data() + 12, "ds64", 4) == 0);
    CHECK(u64(b, 20) == b.size() - 8); CHECK(u64(b, 28) == 2u * 48000u * 6u); CHECK(u64(b, 36) == 2u * 48000u); CHECK(u32(b, 76) == 0xFFFFFFFFu);
}
TEST_CASE("LV30 FLAC and a rate that differs are reported, not hidden") {
    Processor p; prep(p, {{Container, Flac}, {Rate, 0}}); CHECK(p.flacFallback()); CHECK(p.rateFallback()); Processor q; prep(q); CHECK_FALSE(q.flacFallback()); CHECK_FALSE(q.rateFallback());
}
TEST_CASE("LV30 the folder is part of the saved state; start refuses a bad folder; the destructor stops the thread") {
    Processor p; p.setFolder("/some/where/Recordings"); std::vector<uint8_t> st; p.saveExtra(st); Processor q; q.loadExtra(st.data(), st.size()); CHECK(q.folder() == "/some/where/Recordings"); q.loadExtra(nullptr, 0); std::vector<uint8_t> bad = {255, 255, 1}; q.loadExtra(bad.data(), bad.size()); CHECK(q.folder() == "/some/where/Recordings");
    Tmp blocked; { std::ofstream f(blocked.dir / "afile", std::ios::binary); f << "x"; }
    Processor r; prep(r); r.setFolder((blocked.dir / "afile" / "sub").string()); CHECK_FALSE(r.start());   // a folder under a regular file cannot be made on any OS
    Tmp t; { Processor s; s.setFolder(t.dir.string()); prep(s, {{AutoStart, 1}}); CHECK(s.recording()); feed(s, sine(-12, 0.5, 440)); }   // Auto start with a folder starts at prepare; leaving scope stops and closes
    CHECK(files(t.dir, ".wav").size() == 1);
}
TEST_CASE("LV30 mono input goes to both channels; odd blocks; before prepare") {
    Tmp t; Processor p; p.setFolder(t.dir.string()); prep(p, {{AutoStart, 0}}); REQUIRE(p.start()); std::vector<float> l = sine(-12, 0.5, 440), keep = l; for (size_t off = 0; off < l.size(); off += 77) { const int n = static_cast<int>(std::min<size_t>(77, l.size() - off)); float* c[1] = {l.data() + off}; p.process(c, 1, n); } CHECK(l == keep); p.stop();
    const auto b = slurp(files(t.dir, ".wav")[0]); for (size_t i = 3000; i < 20000; i += 211) { const size_t o = 80 + i * 6; CHECK(b[o] == b[o + 3]); }
    Processor z; std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f); z.mark("x"); z.stop();
}
