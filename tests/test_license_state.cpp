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

TEST_CASE("LICENSE STATE: a licence file from the window is checked first, then kept in the folder under its id (2026-10-09)") {
    const Key k = key(1, 4), other = key(2, 9);
    const lic::PublicKey keys[1] = {k.pub};
    const std::string me(64, 'c');
    auto install = [&](const TmpDir& d, const std::string& text, const char* code, lic::License& out, std::string& err) {
        return lic::install(d.p.u8string(), text, keys, 1, lic::acceptedProducts(code), 1, me, out, err);
    };
    lic::License l;
    std::string err;
    {
        TmpDir d;
        CHECK(install(d, "hello", "in07", l, err) == lic::Status::NotALicense);
        CHECK(install(d, licence(other, {"in07"}), "in07", l, err) == lic::Status::UnknownKey);
        CHECK(install(d, licence(k, {"dy01"}), "in07", l, err) == lic::Status::WrongProduct);   // valid, but not for this product
        CHECK(install(d, licence(k, {"in07"}, 1, std::string(64, 'd')), "in07", l, err) == lic::Status::WrongMachine);
        CHECK(install(d, std::string(lic::kMaxBytes + 1, 'x'), "in07", l, err) == lic::Status::NotALicense);
        CHECK(lic::evaluate(d.p.u8string(), keys, 1, lic::acceptedProducts("in07"), 1, me).detail.find("no licence") != std::string::npos);   // nothing written
        const std::string good = licence(k, {"in07", "studio"}, 1, me);
        CHECK(install(d, good, "in07", l, err) == lic::Status::Valid);
        CHECK(l.id == "SWL-T-1");
        CHECK(fs::exists(d.p / "SWL-T-1.swlicense"));
        CHECK(lic::evaluate(d.p.u8string(), keys, 1, lic::acceptedProducts("in07"), 1, me).licensed);
        CHECK(install(d, good, "in07", l, err) == lic::Status::Valid);   // again: the same file, replaced
        size_t n = 0; for (const auto& e : fs::directory_iterator(d.p)) { (void)e; ++n; }
        CHECK(n == 1);
    }
    {
        TmpDir d;   // an id is [A-Za-z0-9._-] (check refuses the rest), cleaned into a file name: never outside the folder, never a device
        for (const char* id : {"..", "CON", ".hidden"}) {
            lic::License x; x.keyId = 1; x.products = {"in07"}; x.id = id; x.issued = "2026-10-09"; x.major = 1;
            CHECK(install(d, lic::sign(x, k.secret), "in07", l, err) == lic::Status::Valid);
        }
        lic::License x; x.keyId = 1; x.products = {"in07"}; x.id = "../evil"; x.issued = "2026-10-09"; x.major = 1;
        CHECK(install(d, lic::sign(x, k.secret), "in07", l, err) == lic::Status::NotALicense);
        size_t n = 0; for (const auto& e : fs::directory_iterator(d.p)) { CHECK(e.path().parent_path() == d.p); CHECK(e.path().extension() == ".swlicense"); ++n; }
        CHECK(n == 3);
        CHECK(fs::exists(d.p / "Untitled.swlicense"));
        CHECK(fs::exists(d.p / "CON_.swlicense"));
        CHECK(fs::exists(d.p / "hidden.swlicense"));
    }
    {
        TmpDir d;   // a folder that does not exist yet is made
        const fs::path sub = d.p / "SEVENTHWELL" / "Licenses";
        CHECK(lic::install(sub.u8string(), licence(k, {"all"}), keys, 1, lic::acceptedProducts("in07"), 1, me, l, err) == lic::Status::Valid);
        CHECK(fs::exists(sub / "SWL-T-1.swlicense"));
    }
    CHECK(lic::thisMachine() == lic::machineHash(lic::kMachineSalt));
}
