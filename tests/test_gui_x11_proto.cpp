// Linux window: the plug-in and the helper process (sw-audio-gui, WebKitGTK) talk through two pipes in frames: 1 byte type, 4 bytes little-endian length, the payload.
#include "doctest.h"
#include "gui_x11_proto.hpp"
#include <string>
#include <vector>

using namespace sw::gui::x11;


TEST_CASE("x11 frames: encode and decode one frame, empty payload, binary payload") {
    FrameReader r; Frame f;
    const auto a = encode('M', "s 3 0.5"); r.feed(a.data(), a.size());
    REQUIRE(r.next(f)); CHECK(f.type == 'M'); CHECK(f.payload == "s 3 0.5"); CHECK(!r.next(f)); CHECK(!r.bad());
    const auto e = encode('Q', ""); r.feed(e.data(), e.size());
    REQUIRE(r.next(f)); CHECK(f.type == 'Q'); CHECK(f.payload.empty());
    std::string bin; for (int i = 0; i < 256; ++i) bin += static_cast<char>(i);
    const auto b = encode('S', bin); r.feed(b.data(), b.size());
    REQUIRE(r.next(f)); CHECK(f.type == 'S'); CHECK(f.payload == bin);
    CHECK(a.size() == 5 + 7); CHECK(a[0] == 'M'); CHECK(a[1] == 7); CHECK(a[2] == 0); CHECK(a[3] == 0); CHECK(a[4] == 0);   // little endian
}

TEST_CASE("x11 frames: pieces of any size, several frames in one piece") {
    const auto f1 = encode('S', std::string(1000, 'x')), f2 = encode('Z', "960 550"), f3 = encode('V', "1");
    std::vector<uint8_t> all; for (const auto* f : {&f1, &f2, &f3}) all.insert(all.end(), f->begin(), f->end());
    for (size_t piece : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(64), all.size()}) {
        FrameReader r; Frame f; int n = 0; std::string types;
        for (size_t off = 0; off < all.size(); off += piece) {
            r.feed(all.data() + off, std::min(piece, all.size() - off));
            while (r.next(f)) { ++n; types += static_cast<char>(f.type); if (f.type == 'S') CHECK(f.payload.size() == 1000); if (f.type == 'Z') CHECK(f.payload == "960 550"); }
        }
        CHECK(n == 3); CHECK(types == "SZV"); CHECK(!r.bad());
    }
}

TEST_CASE("x11 frames: a length that is too large is refused, and the reader stays refused") {
    FrameReader r; Frame f;
    std::vector<uint8_t> evil = {'M', 0xff, 0xff, 0xff, 0xff, 'x'};
    r.feed(evil.data(), evil.size());
    CHECK(!r.next(f)); CHECK(r.bad());
    const auto ok = encode('M', "hello"); r.feed(ok.data(), ok.size());
    CHECK(!r.next(f)); CHECK(r.bad());
    // exactly the largest payload is fine; one more is not
    FrameReader r2; std::vector<uint8_t> hdr = {'H', 0, 0, 0, 0};
    const uint32_t big = static_cast<uint32_t>(kMaxPayload) + 1; for (int i = 0; i < 4; ++i) hdr[1 + i] = static_cast<uint8_t>(big >> (8 * i));
    r2.feed(hdr.data(), hdr.size()); CHECK(r2.bad());
    FrameReader r3; const uint32_t okLen = static_cast<uint32_t>(kMaxPayload); for (int i = 0; i < 4; ++i) hdr[1 + i] = static_cast<uint8_t>(okLen >> (8 * i));
    r3.feed(hdr.data(), hdr.size()); CHECK(!r3.bad());
}

TEST_CASE("x11 frames: a type that is not a printable letter is refused") {
    FrameReader r; Frame f; std::vector<uint8_t> bad = {0x01, 0, 0, 0, 0};
    r.feed(bad.data(), bad.size()); CHECK(!r.next(f)); CHECK(r.bad());
}

TEST_CASE("x11 frames: the sizes of the window are parsed with a safe range") {
    unsigned w = 0, h = 0;
    CHECK(parseSize("960 550", w, h)); CHECK(w == 960); CHECK(h == 550);
    CHECK(parseSize("1920 1080", w, h)); CHECK(w == 1920);
    CHECK(!parseSize("", w, h)); CHECK(!parseSize("960", w, h)); CHECK(!parseSize("a b", w, h)); CHECK(!parseSize("0 100", w, h)); CHECK(!parseSize("100 0", w, h));
    CHECK(!parseSize("-5 100", w, h)); CHECK(!parseSize("99999 100", w, h)); CHECK(!parseSize("100 99999", w, h)); CHECK(!parseSize("100 200 300", w, h));
    CHECK(parseSize("16 16", w, h)); CHECK(!parseSize("15 16", w, h));
}

TEST_CASE("x11 helper: where the plug-in looks for the helper next to itself") {
    const auto c = helperCandidates("/home/u/.clap/SW EQ01 Passive.clap");
    REQUIRE(c.size() >= 3);
    CHECK(c[0] == "/home/u/.clap/sw-audio-gui");
    const auto v = helperCandidates("/home/u/.vst3/SW EQ01 Passive.vst3/Contents/x86_64-linux/SW EQ01 Passive.so");
    bool inBundleDir = false, besideBundle = false;
    for (const auto& p : v) { if (p == "/home/u/.vst3/SW EQ01 Passive.vst3/Contents/x86_64-linux/sw-audio-gui") inBundleDir = true; if (p == "/home/u/.vst3/sw-audio-gui") besideBundle = true; }
    CHECK(inBundleDir); CHECK(besideBundle);
    CHECK(helperCandidates("").empty() == false);   // (the fixed places: ~/.local/share/sw-audio, /usr/local/lib/sw-audio, /usr/lib/sw-audio)
}
