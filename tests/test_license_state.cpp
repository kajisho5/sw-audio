// SW AUDIO core: which products a computer is licensed for (core/include/sw/license_state.hpp): the licence files in the user's licence
// folder are checked with the built-in public keys; a product is licensed by its own code, its line (studio / live) or "all".
// A development build (no production key, SW_LICENSE_ENFORCE off) never puts the demo silence in; a test switch only ever turns it on.
#include "doctest.h"
#include "sw/license_state.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace lic = sw::license;

namespace {
struct Key { uint8_t secret[64]; lic::PublicKey pub; };
Key key(int id, uint8_t fill) {
    Key k{};
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(fill * 3 + i);
    lic::keyPairFromSeed(seed, k.secret, k.pub.key);
    k.pub.id = id;
    return k;
}
std::string licence(const Key& k, std::vector<std::string> products, int major = 1, const std::string& machine = "") {
    lic::License l;
    l.keyId = k.pub.id; l.products = std::move(products); l.id = "SWL-T-1"; l.issued = "2026-10-09"; l.major = major; l.machine = machine;
    return lic::sign(l, k.secret);
}
struct TmpDir {
    fs::path p = fs::temp_directory_path() / ("sw_lic_" + std::to_string(std::random_device{}()));
    TmpDir() { fs::create_directories(p); }
    ~TmpDir() { std::error_code ec; fs::remove_all(p, ec); }
    void put(const std::string& name, const std::string& text) { std::ofstream f(p / name, std::ios::binary); f << text; }
};
}  // namespace

TEST_CASE("LICENSE STATE: the licence folder decides; product, line and \"all\" licences; damaged and foreign files are ignored") {
    const Key k = key(1, 4), other = key(2, 9);
    const lic::PublicKey keys[1] = {k.pub};
    auto eval = [&](const TmpDir& d, const char* code, int major = 0, const std::string& machine = "") {
        return lic::evaluate(d.p.u8string(), keys, 1, lic::acceptedProducts(code), major, machine);
    };
    {
        TmpDir d;
        const auto v = eval(d, "in07");
        CHECK_FALSE(v.licensed);
        CHECK(v.detail.find("no licence") != std::string::npos);
    }
    {
        TmpDir d;
        d.put("junk.swlicense", "hello");
        d.put("big.swlicense", std::string(20000, 'x'));
        d.put("foreign.swlicense", licence(other, {"in07"}));   // signed by a key the plug-in does not carry
        d.put("dy.swlicense", licence(k, {"dy01"}));
        d.put("note.txt", licence(k, {"in07"}));                 // not a .swlicense file
        CHECK_FALSE(eval(d, "in07").licensed);
        CHECK(eval(d, "dy01").licensed);
        d.put("mine.swlicense", licence(k, {"in07"}));
        const auto v = eval(d, "in07");
        CHECK(v.licensed);
        CHECK(v.status == lic::Status::Valid);
        CHECK(v.detail.find("SWL-T-1") != std::string::npos);
    }
    {
        TmpDir d;
        d.put("studio.swlicense", licence(k, {"studio"}));
        CHECK(eval(d, "in07").licensed);    // IN07 is in the STUDIO line
        CHECK(eval(d, "dy01").licensed);
        CHECK_FALSE(eval(d, "lv05").licensed);
        d.put("all.swlicense", licence(k, {"all"}));
        CHECK(eval(d, "lv05").licensed);
    }
    {
        TmpDir d;   // the major version and the machine
        d.put("a.swlicense", licence(k, {"in07"}, 1, std::string(64, 'c')));
        CHECK(eval(d, "in07", 1, std::string(64, 'c')).licensed);
        CHECK_FALSE(eval(d, "in07", 1, std::string(64, 'd')).licensed);
        CHECK_FALSE(eval(d, "in07", 2, std::string(64, 'c')).licensed);
    }
    CHECK(lic::acceptedProducts("lv05") == std::vector<std::string>{"lv05", "live", "all"});
    CHECK(lic::acceptedProducts("IN07") == std::vector<std::string>{"in07", "studio", "all"});
}

TEST_CASE("LICENSE STATE: a development build is never in demo; the test switch only turns the demo on") {
    const lic::Verdict v = lic::productState("in07", 0);
    if (!lic::enforced()) {
        CHECK(v.licensed);
        CHECK_FALSE(v.enforced);
    }
    CHECK_FALSE(lic::licenseFolder().empty());
#if !defined(_WIN32)
    setenv("SW_LICENSE_TEST_DEMO", "1", 1);
    CHECK(lic::forcedDemo());
    setenv("SW_LICENSE_TEST_DEMO", "0", 1);
    CHECK_FALSE(lic::forcedDemo());
    unsetenv("SW_LICENSE_TEST_DEMO");
    CHECK_FALSE(lic::forcedDemo());
#endif
}
