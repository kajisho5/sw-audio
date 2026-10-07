#include "doctest.h"
#include "lv28/lv28.hpp"
#include "tu.hpp"
using namespace sw;
using namespace sw::lv28;
using namespace tu;
namespace {
using Set = std::vector<std::pair<int, double>>;
Processor make(Set set = {}) { Processor p; for (auto& s : set) p.setParam(s.first, s.second); p.prepare(kFs, 256); return p; }
}

TEST_CASE("LV28 table follows the spec") {
    const auto& s = specs(); REQUIRE(s.size() == static_cast<size_t>(kNumParams));
    CHECK(s[AllowControl].def == 1); CHECK(s[RequirePin].def == 1); CHECK(s[DefaultPermission].labels == std::vector<std::string>{"View only", "Control"}); CHECK(s[DefaultPermission].def == 0);
    CHECK(kDefaultPort == 8640); Processor q; CHECK(q.latencySamples() == 0);
}
TEST_CASE("LV28 the sound passes untouched") {
    auto p = make(); const auto x = noise(-20, 0.5, 3), y = run(p, x); for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}
TEST_CASE("LV28 only private-network peers are accepted") {
    for (const char* ok : {"192.168.0.10", "10.1.2.3", "172.16.0.1", "172.31.255.254", "127.0.0.1", "169.254.3.4", "::1", "fe80::1ff:fe23:4567:890a", "fd12:3456:789a::1", "::ffff:192.168.1.5"}) CHECK(isLanAddress(ok));
    for (const char* bad : {"8.8.8.8", "172.15.0.1", "172.32.0.1", "192.169.0.1", "11.0.0.1", "2001:db8::1", "::ffff:8.8.8.8", "", "abc", "192.168.0", "192.168.0.256", "192.168.0.1.5", "1.2.3.4:80", "fe80", "g::1", "192.168.0.1 "}) CHECK_FALSE(isLanAddress(bad));
}
TEST_CASE("LV28 PIN: six random digits, a right PIN gets a token, a wrong one does not") {
    auto p = make(); CHECK(p.pin().size() == 6); for (char c : p.pin()) CHECK(std::isdigit(static_cast<unsigned char>(c)));
    const std::string old = p.pin(); bool changed = false; for (int i = 0; i < 20 && !changed; ++i) { p.rotatePin(); changed = p.pin() != old; } CHECK(changed);
    const auto good = p.authenticate("iPad", "192.168.1.20", p.pin(), 0.0); CHECK(good.result == Ok); CHECK(good.token.size() == 32);
    const auto bad = p.authenticate("iPad", "192.168.1.20", "000000" == p.pin() ? "111111" : "000000", 1.0); CHECK(bad.result == WrongPin); CHECK(bad.token.empty());
    CHECK(p.authenticate("iPad", "8.8.8.8", p.pin(), 2.0).result == Refused);   // not the LAN: refused even with the right PIN
    CHECK(p.authenticate("", "192.168.1.20", p.pin(), 2.0).result == Refused);
}
TEST_CASE("LV28 five wrong PINs lock the address, the lock doubles, a right PIN clears the count") {
    auto p = make(); const std::string wrong = p.pin() == "123456" ? "654321" : "123456"; double t = 0;
    for (int i = 0; i < 5; ++i) { CHECK(p.authenticate("a", "10.0.0.5", wrong, t).result == WrongPin); t += 1; }
    auto r = p.authenticate("a", "10.0.0.5", p.pin(), t); CHECK(r.result == LockedOut); CHECK(r.retryAfter > 20.0); CHECK(r.retryAfter <= 30.0);   // even the right PIN is refused while locked
    CHECK(p.authenticate("b", "10.0.0.6", p.pin(), t).result == Ok);   // another address is not affected
    t += 31.0; CHECK(p.authenticate("a", "10.0.0.5", wrong, t).result == WrongPin);   // after the lock the count starts at... one more wrong: the 6th failure locks again, longer
    for (int i = 0; i < 4; ++i) { t += 1; p.authenticate("a", "10.0.0.5", wrong, t); }
    CHECK(p.lockedUntil("10.0.0.5") > t + 40.0);   // 60 s: doubled
    t += 61.0; CHECK(p.authenticate("a", "10.0.0.5", p.pin(), t).result == Ok);
    CHECK(p.authenticate("a", "10.0.0.5", wrong, t + 1).result == WrongPin); CHECK(p.lockedUntil("10.0.0.5") == 0.0);   // the count was cleared
    double up = 0; auto q = make(); for (int round = 0; round < 12; ++round) { for (int i = 0; i < 5; ++i) q.authenticate("a", "10.0.0.7", wrong, up + i); up = q.lockedUntil("10.0.0.7") + 0.1; }
    CHECK(q.lockedUntil("10.0.0.7") - (up - 0.1) <= 15.0 * 60.0 + 1e-6);   // never more than 15 minutes
}
TEST_CASE("LV28 tokens expire after 8 h, can be revoked, are bound to a device") {
    auto p = make(); const auto a = p.authenticate("iPad", "192.168.1.2", p.pin(), 100.0), b = p.authenticate("Phone", "192.168.1.3", p.pin(), 100.0); CHECK(a.token != b.token);
    Session s; CHECK(p.validate(a.token, 200.0, &s)); CHECK(s.device == "iPad"); CHECK(p.validate(a.token, 100.0 + 8 * 3600.0 - 1.0)); CHECK_FALSE(p.validate(a.token, 100.0 + 8 * 3600.0 + 1.0)); CHECK(p.sessionCount() == 1);
    p.revoke(b.token); CHECK_FALSE(p.validate(b.token, 200.0)); CHECK(p.sessionCount() == 0); CHECK_FALSE(p.validate("", 0.0)); CHECK_FALSE(p.validate("not a token", 0.0));
    auto q = make(); q.authenticate("x", "10.0.0.1", q.pin(), 0.0); q.authenticate("y", "10.0.0.1", q.pin(), 0.0); q.revokeAll(); CHECK(q.sessionCount() == 0);
}
TEST_CASE("LV28 permissions: View only by default, Control per device, nothing while Allow control is Off or all is locked") {
    auto p = make(); const auto t = p.authenticate("iPad", "192.168.1.2", p.pin(), 0.0).token; CHECK(p.validate(t, 1.0)); CHECK_FALSE(p.canControl(t, 1.0));
    CHECK(p.setPermission("iPad", Control)); CHECK(p.canControl(t, 1.0));
    p.lockAll(true); CHECK(p.locked()); CHECK_FALSE(p.canControl(t, 1.0)); CHECK(p.validate(t, 1.0)); p.lockAll(false); CHECK(p.canControl(t, 1.0));
    p.setParam(AllowControl, 0); CHECK_FALSE(p.canControl(t, 1.0)); CHECK(p.authenticate("new", "192.168.1.9", p.pin(), 2.0).result == ControlOff); p.setParam(AllowControl, 1); CHECK(p.canControl(t, 1.0));
    CHECK_FALSE(p.setPermission("", Control)); CHECK_FALSE(p.setPermission("x", 5));
    auto d = make({{DefaultPermission, Control}}); const auto t2 = d.authenticate("Tab", "192.168.1.4", d.pin(), 0.0).token; CHECK(d.canControl(t2, 1.0));
    auto s = make(); const auto t3 = s.authenticate("Tab", "192.168.1.4", s.pin(), 0.0).token; s.setPermission("Tab", Control); CHECK(s.canControl(t3, 0.5)); s.setPermission("Tab", ViewOnly); CHECK_FALSE(s.canControl(t3, 0.5));
}
TEST_CASE("LV28 Require PIN Off lets a LAN device in without the PIN, still only the LAN") {
    auto p = make({{RequirePin, 0}}); CHECK(p.authenticate("iPad", "192.168.1.2", "", 0.0).result == Ok); CHECK(p.authenticate("iPad", "1.2.3.4", "", 0.0).result == Refused);
}
TEST_CASE("LV28 before prepare nothing is let in; process does nothing") {
    Processor z; CHECK(z.pin().empty()); const auto r0 = z.authenticate("a", "10.0.0.1", "", 0.0); CHECK(r0.token.empty() == (r0.result != Ok));
    std::vector<float> a(256, 0.3f); float* c[1] = {a.data()}; z.process(c, 1, 256); CHECK(a[0] == 0.3f);
}
