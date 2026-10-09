#include "doctest.h"
#include "swlink.hpp"
#include <algorithm>
#include <atomic>
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
