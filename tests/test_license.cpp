// SW AUDIO core: licence files (core/include/sw/license.hpp). A perpetual licence (the owner's decision, 2026-10-09: 買い切り) is a small
// text file signed with Ed25519 by the licence server; the plug-in checks it offline with the public key it carries.
// Checked here: standard Ed25519 (a signature made by OpenSSL verifies, so any standard server library can sign), the round trip,
// tampering, the wrong key, products, the major version a perpetual licence covers, machine binding, CRLF, damaged files.
#include "doctest.h"
#include "sw/license.hpp"
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace lic = sw::license;

namespace {
// a throwaway key made with OpenSSL for this test only (its secret key was not kept)
constexpr const char* kOpensslPublic = "52b2ddc7b231042cd53828ed9edba418c8b84c46992f349cd8390fabe56868b7";
constexpr const char* kOpensslMessage = "SW-LICENSE 1\nkey=7\nproduct=in07,studio\nlicense=SWL-TEST-0001\nissued=2026-10-09\nmajor=1\n";
constexpr const char* kOpensslSig = "Fz2O0tCMq4QG+fTwa+C+He24kJWaAG3Ko8GLf7e71SWfsB0MV8badCj61RyaI3dI+KHH3bmm9IyN5PCYaljlCg==";

lic::PublicKey keyFromHex(int id, const char* hex) {
    lic::PublicKey k{};
    k.id = id;
    std::vector<uint8_t> b;
    REQUIRE(lic::decodeHex(hex, b));
    REQUIRE(b.size() == 32);
    std::memcpy(k.key, b.data(), 32);
    return k;
}
// a test key pair from a fixed seed (tests only: the plug-ins never hold a secret key)
struct TestKey { uint8_t secret[64]; lic::PublicKey pub; };
TestKey testKey(int id, uint8_t fill) {
    TestKey t{};
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(fill + i);
    lic::keyPairFromSeed(seed, t.secret, t.pub.key);
    t.pub.id = id;
    return t;
}
lic::License sample(int key) {
    lic::License l;
    l.keyId = key;
    l.products = {"in07"};
    l.id = "SWL-2026-000123";
    l.issued = "2026-10-09";
    l.major = 1;
    return l;
}
}  // namespace

TEST_CASE("LICENSE: a licence signed by a standard Ed25519 implementation (OpenSSL) verifies") {
    const lic::PublicKey k = keyFromHex(7, kOpensslPublic);
    const std::string text = std::string(kOpensslMessage) + "sig=" + kOpensslSig + "\n";
    lic::License got;
    CHECK(lic::check(text, &k, 1, {"in07"}, 1, "", got) == lic::Status::Valid);
    CHECK(got.keyId == 7);
    CHECK(got.products == std::vector<std::string>{"in07", "studio"});
    CHECK(got.id == "SWL-TEST-0001");
    CHECK(got.issued == "2026-10-09");
    CHECK(got.major == 1);
    CHECK(got.machine.empty());
    CHECK(lic::check(text, &k, 1, {"studio"}, 1, "", got) == lic::Status::Valid);   // any listed product
    // one changed character anywhere in the signed part breaks it
    std::string t2 = text; t2[t2.find("0001")] = '9';
    CHECK(lic::check(t2, &k, 1, {"in07"}, 1, "", got) == lic::Status::BadSignature);
}

TEST_CASE("LICENSE: written and checked back; tampering, another key, an unknown key id") {
    const TestKey a = testKey(1, 10), b = testKey(2, 77);
    const std::string text = lic::sign(sample(1), a.secret);
    CHECK(text.rfind("SW-LICENSE 1\n", 0) == 0);
    CHECK(text.find("\nsig=") != std::string::npos);
    lic::License got;
    const lic::PublicKey both[2] = {a.pub, b.pub};
    CHECK(lic::check(text, both, 2, {"in07"}, 1, "", got) == lic::Status::Valid);
    CHECK(got.id == "SWL-2026-000123");
    // every byte of the signed part matters
    const size_t sigAt = text.find("sig=");
    for (size_t i = 0; i < sigAt; i += 3) {
        std::string t = text;
        t[i] = static_cast<char>(t[i] ^ 0x01);
        CHECK_MESSAGE(lic::check(t, both, 2, {"in07"}, 1, "", got) != lic::Status::Valid, "byte " << i);
    }
    // the signature itself
    std::string ts = text; ts[sigAt + 10] = ts[sigAt + 10] == 'A' ? 'B' : 'A';
    CHECK(lic::check(ts, both, 2, {"in07"}, 1, "", got) == lic::Status::BadSignature);
    // signed with key 1 but claiming key 2: B's public key does not verify it
    lic::License l2 = sample(2);
    std::string forged = lic::sign(l2, a.secret);
    CHECK(lic::check(forged, both, 2, {"in07"}, 1, "", got) == lic::Status::BadSignature);
    // a key id the plug-in does not carry
    CHECK(lic::check(lic::sign(sample(9), a.secret), both, 2, {"in07"}, 1, "", got) == lic::Status::UnknownKey);
    // a refused licence gives nothing
    CHECK(got.id.empty());
}

TEST_CASE("LICENSE: products, the major version a perpetual licence covers, the machine") {
    const TestKey a = testKey(1, 10);
    lic::License got;
    lic::License l = sample(1);
    const std::string text = lic::sign(l, a.secret);
    CHECK(lic::check(text, &a.pub, 1, {"dy01"}, 1, "", got) == lic::Status::WrongProduct);
    CHECK(lic::check(text, &a.pub, 1, {"dy01", "in07"}, 1, "", got) == lic::Status::Valid);
    // perpetual: no end date; every update of the major version it was bought for, not the next major version
    CHECK(lic::check(text, &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::Valid);
    CHECK(lic::check(text, &a.pub, 1, {"in07"}, 0, "", got) == lic::Status::Valid);
    CHECK(lic::check(text, &a.pub, 1, {"in07"}, 2, "", got) == lic::Status::NewerMajor);
    // bound to a machine
    l.machine = std::string(64, 'a');
    const std::string bound = lic::sign(l, a.secret);
    CHECK(lic::check(bound, &a.pub, 1, {"in07"}, 1, std::string(64, 'a'), got) == lic::Status::Valid);
    CHECK(lic::check(bound, &a.pub, 1, {"in07"}, 1, std::string(64, 'b'), got) == lic::Status::WrongMachine);
    CHECK(lic::check(bound, &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::WrongMachine);   // this machine unknown
    CHECK(std::string(lic::statusText(lic::Status::WrongMachine)).size() > 5);
}

TEST_CASE("LICENSE: a licence that went through Windows line ends still verifies; damaged files are refused without a crash") {
    const TestKey a = testKey(1, 10);
    const std::string text = lic::sign(sample(1), a.secret);
    std::string crlf;
    for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
    lic::License got;
    CHECK(lic::check(crlf, &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::Valid);
    CHECK(lic::check("", &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::NotALicense);
    CHECK(lic::check("SW-LICENSE 1\nkey=1\n", &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::NotALicense);   // no signature
    CHECK(lic::check("SW-LICENSE 2\n" + text.substr(13), &a.pub, 1, {"in07"}, 1, "", got) != lic::Status::Valid);
    CHECK(lic::check(text + "extra=1\n", &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::NotALicense);       // nothing after the signature
    CHECK(lic::check(std::string(20000, 'x'), &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::NotALicense);
    std::mt19937 rng(5);
    int valid = 0;
    for (int it = 0; it < 3000; ++it) {
        std::string s = text;
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int e = 0; e < edits && !s.empty(); ++e) {
            const size_t at = rng() % s.size();
            switch (rng() % 4) {
                case 0: s[at] = static_cast<char>(rng() & 0xFF); break;
                case 1: s.erase(at, 1 + rng() % 8); break;
                case 2: s.insert(at, 1, static_cast<char>(rng() & 0xFF)); break;
                default: s.resize(at); break;
            }
        }
        if (lic::check(s, &a.pub, 1, {"in07"}, 1, "", got) == lic::Status::Valid) ++valid;
    }
    MESSAGE(valid << " of 3000 damaged licences passed (only a removed last newline, trailing whitespace or an inserted CR can: CRs are dropped by design)");
    CHECK(valid < 60);
}

TEST_CASE("LICENSE: hex and base64 are strict") {
    std::vector<uint8_t> b;
    CHECK(lic::decodeHex("00ff10", b)); CHECK(b == std::vector<uint8_t>{0x00, 0xFF, 0x10});
    CHECK_FALSE(lic::decodeHex("0g", b));
    CHECK_FALSE(lic::decodeHex("abc", b));
    CHECK(lic::decodeBase64("TWFu", b)); CHECK(b == std::vector<uint8_t>{'M', 'a', 'n'});
    CHECK(lic::decodeBase64("TWE=", b)); CHECK(b == std::vector<uint8_t>{'M', 'a'});
    CHECK_FALSE(lic::decodeBase64("TWE", b));
    CHECK_FALSE(lic::decodeBase64("TW=E", b));
    CHECK_FALSE(lic::decodeBase64("T*Fu", b));
    CHECK(lic::encodeBase64({'M', 'a'}) == "TWE=");
}

TEST_CASE("LICENSE: this machine's hash is stable, salted, and hides the raw id") {
    const std::string h1 = lic::machineHash("SW AUDIO test"), h2 = lic::machineHash("SW AUDIO test"), h3 = lic::machineHash("other salt");
#if defined(__linux__)
    REQUIRE_FALSE(h1.empty());   // /etc/machine-id
#endif
    if (!h1.empty()) {
        CHECK(h1.size() == 64);
        CHECK(h1 == h2);
        CHECK(h1 != h3);
    } else {
        MESSAGE("no machine id here");
    }
}
