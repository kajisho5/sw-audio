#include "doctest.h"
#include "swlink.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>
using namespace sw;

namespace {
// a ring of the kind the plug-ins export: kN samples of a sine, a write position that can be moved on
struct Ring {
    static constexpr size_t kN = 8192;
    std::vector<std::atomic<float>> data = std::vector<std::atomic<float>>(kN);
    std::atomic<size_t> head{0};
    Ring(double hz, double db, double fs = 48000.0) {
        const double a = std::pow(10.0, db / 20.0);
        for (size_t i = 0; i < kN; ++i) data[i].store(static_cast<float>(a * std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / fs)), std::memory_order_relaxed);
        head.store(kN);
    }
    gui::RingView view() { return {data.data(), &head, kN - 1}; }
    void advance(size_t k = 256) { head.store(head.load() + k); }
};
int peakBand(const double* db) { return static_cast<int>(std::max_element(db, db + gui::kSpecBands) - db); }
}  // namespace

TEST_CASE("SW Link: one registry for the process, found again through the environment") {
    link::Registry* r = link::registry(); REQUIRE(r != nullptr); CHECK(link::valid(r));
    CHECK(link::registry() == r);
    char buf[64]; REQUIRE(link::detail::getEnv(link::kEnv, buf, sizeof buf));
    unsigned long pid = 0; unsigned long long addr = 0; REQUIRE(std::sscanf(buf, "%lu:%llx", &pid, &addr) == 2);
    CHECK(pid == link::detail::processId()); CHECK(addr == static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r)));
    CHECK(link::detail::fromEnv() == r);   // what a second plug-in binary does
    // a value that came from another process (an inherited variable) is not trusted; a registry of another layout is not used
    link::detail::setEnv(link::kEnv, "1:1000"); CHECK(link::detail::fromEnv() == nullptr);
    link::detail::setEnv(link::kEnv, buf); CHECK(link::detail::fromEnv() == r);
    link::Registry other; other.version = link::kVersion + 1; CHECK_FALSE(link::valid(&other)); other.version = link::kVersion; other.slotSize += 8; CHECK_FALSE(link::valid(&other));
    CHECK_FALSE(link::valid(nullptr));
}

TEST_CASE("SW Link: instances see each other, not themselves, and read each other's spectrum") {
    Ring ra(1000.0, -20), rb(4000.0, -26);
    link::Member a, b;
    REQUIRE(a.join(ra.view(), "EQ02")); REQUIRE(b.join(rb.view(), "DY08"));
    CHECK(a.peers() == 1); CHECK(b.peers() == 1);
    double db[gui::kSpecBands];
    CHECK(a.others(db) == 1);   // a hears b: 4 kHz at -26 dB
    { const int k = peakBand(db); const double f = gui::specBandFreq(k); CHECK(f == doctest::Approx(4000.0).epsilon(0.12)); CHECK(db[k] == doctest::Approx(-26.0).epsilon(0.1)); }
    CHECK(b.others(db) == 1);   // b hears a: 1 kHz at -20 dB
    { const int k = peakBand(db); const double f = gui::specBandFreq(k); CHECK(f == doctest::Approx(1000.0).epsilon(0.12)); CHECK(db[k] == doctest::Approx(-20.0).epsilon(0.1)); }
    CHECK(a.peerCodes() == "DY08"); CHECK(b.peerCodes() == "EQ02");
    // a third one: the others are added up (power)
    Ring rc(1000.0, -20); link::Member c; REQUIRE(c.join(rc.view(), "MS01"));
    CHECK(a.peers() == 2); CHECK(b.peers() == 2); CHECK(c.peers() == 2);
    link::Member d; Ring rd(1000.0, -20); REQUIRE(d.join(rd.view(), "LV05"));
    CHECK(c.others(db) == 3); CHECK(d.peers() == 3);
    // a leaves: the others no longer count it, and the slot is free again
    a.leave(); CHECK_FALSE(a.joined()); CHECK(b.peers() == 2);
    link::Member e; Ring re(500.0, -30); REQUIRE(e.join(re.view(), "CR01")); CHECK(b.peers() == 3);
}

TEST_CASE("SW Link: an instance that is not processing stops counting after the timeout") {
    Ring ra(1000.0, -20), rb(2000.0, -20);
    link::Member a, b; REQUIRE(a.join(ra.view(), "EQ02")); REQUIRE(b.join(rb.view(), "DY08"));
    a.setTimeout(0.05);
    CHECK(a.peers() == 1);                       // just seen
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(a.peers() == 0);                       // its write position has not moved
    rb.advance(); CHECK(a.peers() == 1);         // it is processing again
    double db[gui::kSpecBands]; std::this_thread::sleep_for(std::chrono::milliseconds(120)); CHECK(a.others(db) == 0);
    for (double v : db) CHECK(v == -120.0);      // nothing to add up
}

TEST_CASE("SW Link: all slots taken, then one freed; a ring too short for a spectrum is counted but not analysed") {
    std::vector<std::unique_ptr<Ring>> rings; std::vector<std::unique_ptr<link::Member>> ms;
    for (int i = 0; i < link::kSlots; ++i) { rings.push_back(std::make_unique<Ring>(500.0 + i, -30)); ms.push_back(std::make_unique<link::Member>()); }
    int joined = 0; for (int i = 0; i < link::kSlots; ++i) joined += ms[static_cast<size_t>(i)]->join(rings[static_cast<size_t>(i)]->view(), "UT01") ? 1 : 0;
    // other test cases' members are gone (they left in their destructors), so all of them fit
    CHECK(joined == link::kSlots);
    link::Member extra; Ring re(900.0, -30); CHECK_FALSE(extra.join(re.view(), "UT01")); CHECK_FALSE(extra.joined());
    CHECK(ms[0]->peers() == link::kSlots - 1);
    ms[5]->leave(); CHECK(extra.join(re.view(), "UT01"));
    CHECK(ms[0]->peers() == link::kSlots - 1);
}

TEST_CASE("SW Link: a reader never touches a ring that is being destroyed (join / leave against peers / others on another thread)") {
    link::Member watcher; Ring rw(300.0, -40); REQUIRE(watcher.join(rw.view(), "EQ02")); watcher.setSpectrumInterval(0.0);   // read the rings every time
    std::atomic<bool> stop{false}; std::atomic<long> reads{0};
    std::thread reader([&] { double db[gui::kSpecBands]; while (!stop.load()) { watcher.peers(); watcher.others(db); ++reads; } });
    for (int i = 0; i < 300; ++i) {
        auto ring = std::make_unique<Ring>(800.0 + i, -20); link::Member m;
        if (m.join(ring->view(), "DY08")) { ring->advance(); std::this_thread::yield(); }
        m.leave();      // after this the ring may go
        ring.reset();   // freed while the reader may be about to look at the slot
    }
    stop = true; reader.join();
    CHECK(reads.load() > 0);
    CHECK(watcher.peers() == 0);
}

TEST_CASE("SW Link: a member that never joined, and a ring without data, are harmless") {
    link::Member m; CHECK_FALSE(m.joined()); m.leave(); m.setSampleRate(44100.0);
    CHECK(m.peers() >= 0);
    double db[gui::kSpecBands]; m.others(db);
    gui::RingView none; CHECK_FALSE(m.join(none, "EQ02")); CHECK_FALSE(m.joined());
    CHECK(link::packCode("EQ02") == (('E' << 24) | ('Q' << 16) | ('0' << 8) | '2'));
}

TEST_CASE("SW Link: a reference spectrum is shared by the product that has it (UT03) and found by the one that wants it (EQ05); withdrawn with it; torn reads are dropped") {
    Ring a(300, -20), b(1000, -20);
    link::Member ut, eq, other;
    REQUIRE(ut.join(a.view(), "UT03")); REQUIRE(eq.join(b.view(), "EQ05")); REQUIRE(other.join(b.view(), "DY08"));
    double db[link::kRefBands];
    CHECK(eq.findReference("UT03", db) == 0u);   // nothing published yet
    float v[link::kRefBands]; for (int i = 0; i < link::kRefBands; ++i) v[i] = -30.0f + 0.5f * static_cast<float>(i);
    ut.publishReference(7, v);
    CHECK(eq.findReference("UT03") == 7u);
    CHECK(eq.findReference("UT03", db) == 7u); for (int i = 0; i < link::kRefBands; ++i) CHECK(db[i] == doctest::Approx(-30.0 + 0.5 * i));
    CHECK(eq.findReference("DY08") == 0u);       // another product's reference is asked for by its code
    CHECK(ut.findReference("UT03") == 0u);       // (an instance does not find itself)
    ut.publishReference(8, v); CHECK(eq.findReference("UT03") == 8u);   // a new reference: a new serial
    ut.publishReference(0, nullptr); CHECK(eq.findReference("UT03") == 0u);   // withdrawn
    ut.publishReference(9, v); CHECK(eq.findReference("UT03") == 9u);
    ut.leave(); CHECK(eq.findReference("UT03") == 0u);   // it goes away with its owner
    // a reader against a writer that keeps changing it: what is returned is always one reference, never a mix
    REQUIRE(ut.join(a.view(), "UT03"));
    std::atomic<bool> stop{false};
    std::thread w([&] { float x[link::kRefBands]; uint32_t n = 1; while (!stop) { for (auto& e : x) e = static_cast<float>(n); ut.publishReference(n, x); ++n; std::this_thread::sleep_for(std::chrono::microseconds(30)); } });   // (a new reference every 30 us: a seqlock reader gets a whole one between two writes; a writer that never rests starves it)
    int mixed = 0, reads = 0;
    // (until 2000 whole references were read, or 5 s: on a busy machine the writer may not even have started when a fixed number of attempts is over)
    for (const auto t0 = std::chrono::steady_clock::now(); reads < 2000 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5);) { double d[link::kRefBands]; const uint32_t s = eq.findReference("UT03", d); if (!s) continue; ++reads; for (int i = 1; i < link::kRefBands; ++i) if (d[i] != d[0]) { ++mixed; break; } }
    stop = true; w.join();
    INFO(reads << " reads"); CHECK(mixed == 0); CHECK(reads > 0);
}

TEST_CASE("SW Link: a key signal - the latest samples of another instance's output ring, read on the audio thread; nothing from a ring that does not move, from another product, or from a slot that went away") {
    Ring a(300, -20), b(1000, -20);
    link::Member peer, me, other;
    REQUIRE(peer.join(a.view(), "LV01")); REQUIRE(me.join(b.view(), "LV05")); REQUIRE(other.join(b.view(), "DY08"));
    link::Member::KeyState st; std::vector<float> key(256, 0.0f);
    // the ring of LV01 is a 300 Hz sine at -20 dB: the last 256 samples before the write position come back
    CHECK(me.readKey("LV01", key.data(), 256, st));
    { const size_t h = a.head.load(); double worst = 0; for (size_t k = 0; k < 256; ++k) worst = std::max(worst, std::abs(static_cast<double>(key[k]) - static_cast<double>(a.data[(h - 256 + k) & (Ring::kN - 1)].load()))); CHECK(worst == 0.0); }
    CHECK_FALSE(me.readKey("LV02", key.data(), 256, st));    // no such product
    CHECK_FALSE(peer.readKey("LV01", key.data(), 256, st));  // (an instance is not its own key)
    link::Member::KeyState st2;
    // a ring that moves is a key every time; one that stands still is none after two calls
    int good = 0; for (int k = 0; k < 6; ++k) { a.advance(256); if (me.readKey("LV01", key.data(), 256, st2)) ++good; } CHECK(good == 6);
    int still = 0; for (int k = 0; k < 6; ++k) if (me.readKey("LV01", key.data(), 256, st2)) ++still; CHECK(still <= 2);   // (the first call after the last move still counts)
    a.advance(256); CHECK(me.readKey("LV01", key.data(), 256, st2));                                                         // it moves again: a key again
    // more than the ring holds, or before it holds that much: nothing
    CHECK_FALSE(me.readKey("LV01", key.data(), 100000, st2));
    // the owner goes away: the key is gone (and nothing is read from a withdrawn ring)
    peer.leave(); CHECK_FALSE(me.readKey("LV01", key.data(), 256, st2));
    // two instances of the product: the other one takes over
    Ring c(500, -20); link::Member peer2, peer3; REQUIRE(peer2.join(a.view(), "LV01")); REQUIRE(peer3.join(c.view(), "LV01"));
    link::Member::KeyState st3; CHECK(me.readKey("LV01", key.data(), 256, st3));
    // a reader against a ring that is being written: the samples are always whole samples of the ring (no crash, no garbage), with the owner leaving and joining
    std::atomic<bool> stop{false};
    std::thread w([&] { while (!stop) { peer2.leave(); peer2.join(a.view(), "LV01"); a.advance(64); } });
    int reads = 0; link::Member::KeyState st4;
    for (const auto t0 = std::chrono::steady_clock::now(); reads < 2000 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5);) { if (me.readKey("LV01", key.data(), 256, st4)) { ++reads; for (float v : key) if (!(std::abs(v) <= 0.11f)) { CHECK(std::abs(v) <= 0.11f); break; } } }
    stop = true; w.join();
    INFO(reads << " reads"); CHECK(reads > 0);
}

TEST_CASE("SW Link: a shared setting - the last change of any instance of a product is taken once by the others, not by the one that made it, not by other products; a late joiner takes what is there; no bouncing") {
    Ring a(300, -20), b(1000, -20), c(500, -20);
    link::Member m1, m2, m3, other;
    REQUIRE(m1.join(a.view(), "MT05")); REQUIRE(m2.join(b.view(), "MT05")); REQUIRE(m3.join(c.view(), "MT05")); REQUIRE(other.join(c.view(), "DY08"));
    double v = 0;
    CHECK_FALSE(m2.adoptShared("MT05", v));            // nothing published yet
    m1.publishShared(-14.0);
    CHECK(m2.adoptShared("MT05", v)); CHECK(v == -14.0);
    CHECK_FALSE(m2.adoptShared("MT05", v));            // once
    CHECK(m3.adoptShared("MT05", v)); CHECK(v == -14.0);
    CHECK_FALSE(m1.adoptShared("MT05", v));            // not by the one that made it
    CHECK_FALSE(other.adoptShared("DY08", v));
    // the last change wins, whoever made it; the adopters do not publish, so nothing comes back
    m2.publishShared(-20.0);
    CHECK(m1.adoptShared("MT05", v)); CHECK(v == -20.0);
    CHECK(m3.adoptShared("MT05", v)); CHECK(v == -20.0);
    CHECK_FALSE(m1.adoptShared("MT05", v)); CHECK_FALSE(m2.adoptShared("MT05", v)); CHECK_FALSE(m3.adoptShared("MT05", v));
    // two changes before the others look: they take the newest
    m1.publishShared(-18.0); m3.publishShared(-14.0);
    CHECK(m2.adoptShared("MT05", v)); CHECK(v == -14.0);
    CHECK(m1.adoptShared("MT05", v)); CHECK(v == -14.0);
    CHECK_FALSE(m3.adoptShared("MT05", v));
    // a late joiner takes what is there; when the owner has left there is nothing
    link::Member late; REQUIRE(late.join(a.view(), "MT05")); CHECK(late.adoptShared("MT05", v)); CHECK(v == -14.0);
    late.leave(); m3.leave(); m1.leave(); m2.leave();
    link::Member alone; REQUIRE(alone.join(a.view(), "MT05")); CHECK_FALSE(alone.adoptShared("MT05", v));
}

TEST_CASE("SW Link: a key with two instances of the product - the one that moves is the key, a stopped one is never taken for a key (no flipping between slots when the moving one stops)") {
    Ring a(300, -20), c(500, -20), me_ring(1000, -20);
    link::Member pa, pc, me;
    REQUIRE(pa.join(a.view(), "LV01")); REQUIRE(pc.join(c.view(), "LV01")); REQUIRE(me.join(me_ring.view(), "LV05"));
    link::Member::KeyState st; std::vector<float> key(256, 0.0f);
    auto fromRing = [&](Ring& r) { const size_t h = r.head.load(); double worst = 0; for (size_t k = 0; k < 256; ++k) worst = std::max(worst, std::abs(static_cast<double>(key[k]) - static_cast<double>(r.data[(h - 256 + k) & (Ring::kN - 1)].load()))); return worst == 0.0; };
    // the first calls see both (nothing is known yet); then only the one that moves is a key, whichever slot comes first
    for (int k = 0; k < 6; ++k) { c.advance(256); (void)me.readKey("LV01", key.data(), 256, st); }
    for (int k = 0; k < 20; ++k) { c.advance(256); CHECK(me.readKey("LV01", key.data(), 256, st)); CHECK(fromRing(c)); }
    // c stops: after two calls there is no key, and none for as long as both stand still
    int gone = 0; for (int k = 0; k < 40; ++k) if (!me.readKey("LV01", key.data(), 256, st)) ++gone; CHECK(gone >= 38);
    int late = 0; for (int k = 0; k < 40; ++k) if (me.readKey("LV01", key.data(), 256, st)) ++late;
    CHECK(late == 0);
    // the other one starts moving: it is the key at once (within a block)
    a.advance(256); (void)me.readKey("LV01", key.data(), 256, st);
    for (int k = 0; k < 10; ++k) { a.advance(256); CHECK(me.readKey("LV01", key.data(), 256, st)); CHECK(fromRing(a)); }
    // both move: one of them, steadily (the one that was the key stays the key)
    int fromA = 0, fromC = 0; for (int k = 0; k < 20; ++k) { a.advance(256); c.advance(256); if (me.readKey("LV01", key.data(), 256, st)) { if (fromRing(a)) ++fromA; else if (fromRing(c)) ++fromC; } }
    CHECK(fromA + fromC == 20); CHECK(std::min(fromA, fromC) == 0);
}

TEST_CASE("SW Link: a key looked for by the tag (LO03: the instance whose Role is Kick) - only an instance that says it is that, and only while its ring moves; the tag goes with the slot") {
    Ring kick(55, -20), bass(80, -20), both(70, -20), mine(1000, -20);
    link::Member pk, pb, pt, me;
    REQUIRE(pk.join(kick.view(), "LO03")); REQUIRE(pb.join(bass.view(), "LO03")); REQUIRE(pt.join(both.view(), "LO03")); REQUIRE(me.join(mine.view(), "LO03"));
    link::Member::KeyState st; std::vector<float> key(256, 0.0f);
    auto fromRing = [&](Ring& r) { const size_t h = r.head.load(); double worst = 0; for (size_t k = 0; k < 256; ++k) worst = std::max(worst, std::abs(static_cast<double>(key[k]) - static_cast<double>(r.data[(h - 256 + k) & (Ring::kN - 1)].load()))); return worst == 0.0; };
    auto step = [&] { kick.advance(256); bass.advance(256); both.advance(256); };
    // nobody has said what it is: no instance with the tag 1
    for (int k = 0; k < 4; ++k) { step(); CHECK_FALSE(me.readKey("LO03", key.data(), 256, st, 1)); }
    pk.setTag(1); pb.setTag(2); pt.setTag(3); me.setTag(2);
    for (int k = 0; k < 4; ++k) { step(); (void)me.readKey("LO03", key.data(), 256, st, 1); }
    for (int k = 0; k < 10; ++k) { step(); CHECK(me.readKey("LO03", key.data(), 256, st, 1)); CHECK(fromRing(kick)); }     // the Kick, not the Bass or the Both
    link::Member::KeyState st2;
    for (int k = 0; k < 4; ++k) { step(); (void)me.readKey("LO03", key.data(), 256, st2, 3); }
    for (int k = 0; k < 4; ++k) { step(); CHECK(me.readKey("LO03", key.data(), 256, st2, 3)); CHECK(fromRing(both)); }
    // the Kick becomes a Bass: no Kick left, so no key; it is a Kick again: a key once it moves
    pk.setTag(2); for (int k = 0; k < 4; ++k) { step(); CHECK_FALSE(me.readKey("LO03", key.data(), 256, st, 1)); }
    pk.setTag(1); step(); (void)me.readKey("LO03", key.data(), 256, st, 1); step(); CHECK(me.readKey("LO03", key.data(), 256, st, 1));
    // a tag of 0 asks for any instance of the product; the instance itself is never its own key
    link::Member::KeyState st3; step(); CHECK(me.readKey("LO03", key.data(), 256, st3));
    // the slot is freed: the next instance in it does not inherit the tag
    pk.leave(); link::Member again; REQUIRE(again.join(kick.view(), "LO03"));
    for (int k = 0; k < 6; ++k) { step(); CHECK_FALSE(me.readKey("LO03", key.data(), 256, st, 1)); }
}
