// SW AUDIO core — the licence state of a product on this computer (see sw/license_state.hpp).
#include "sw/license_state.hpp"
#include "sw/preset_file.hpp"   // readFile / listFiles (bounded, no links followed)
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace sw::license {

namespace {
// The public keys the plug-ins accept (key id, 32 bytes). Empty: no production key has been made yet. The owner makes it with
// tools/sw_license.cpp ("keygen") on their own computer or in the licence server's key store, and puts the PUBLIC key here; the
// secret key never enters this repository. Keep old keys listed when a new one is added (licences already issued keep working).
const std::vector<PublicKey>& builtInKeys() {
    static const std::vector<PublicKey> k = {};
    return k;
}
constexpr size_t kMaxFiles = 64;
}  // namespace

std::vector<std::string> acceptedProducts(const std::string& code) {
    std::string c = code;
    for (auto& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const std::string line = c.rfind("lv", 0) == 0 ? "live" : "studio";
    return {c, line, "all"};
}

Verdict evaluate(const std::string& folder, const PublicKey* keys, size_t numKeys, const std::vector<std::string>& accepted, int pluginMajor,
                 const std::string& machine) {
    Verdict v;
    v.enforced = true;
    v.licensed = false;
    const auto files = presetfile::listFiles(folder, "swlicense", 1, kMaxFiles);
    if (files.empty()) { v.detail = "no licence file in " + folder; return v; }
    // the most telling refusal when nothing is valid: one the signature checked out for, then a bad signature, then the rest
    auto rank = [](Status s) { return s == Status::WrongProduct || s == Status::NewerMajor || s == Status::WrongMachine ? 3 : s == Status::BadSignature ? 2 : s == Status::UnknownKey ? 1 : 0; };
    int best = -1;
    for (const auto& f : files) {
        std::string text, err;
        if (!presetfile::readFile(f, text, err, kMaxBytes)) continue;
        License l;
        const Status s = check(text, keys, numKeys, accepted, pluginMajor, machine, l);
        if (s == Status::Valid) {
            v.licensed = true;
            v.status = s;
            v.detail = "licence " + l.id + " (";
            for (size_t i = 0; i < l.products.size(); ++i) v.detail += (i ? ", " : "") + l.products[i];
            v.detail += ")";
            return v;
        }
        if (rank(s) > best) { best = rank(s); v.status = s; v.detail = std::string(statusText(s)) + ": " + presetfile::pathOf(f).filename().u8string(); }
    }
    if (best < 0) v.detail = "no readable licence file in " + folder;
    return v;
}

bool enforced() {
#if defined(SW_LICENSE_ENFORCE) && SW_LICENSE_ENFORCE
    return !builtInKeys().empty();
#else
    return false;
#endif
}

bool forcedDemo() {
    const char* e = std::getenv("SW_LICENSE_TEST_DEMO");
    return e && e[0] == '1' && e[1] == '\0';
}

std::string licenseFolder() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    PWSTR p = nullptr;
    fs::path base;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p) == S_OK && p) base = fs::path(p);
    if (p) CoTaskMemFree(p);
    if (base.empty()) return {};
    return (base / "SEVENTHWELL" / "Licenses").u8string();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return (fs::u8path(home) / "Library" / "Application Support" / "SEVENTHWELL" / "Licenses").u8string();
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    fs::path base;
    if (xdg && *xdg == '/') base = fs::u8path(xdg);
    else {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return {};
        base = fs::u8path(home) / ".local" / "share";
    }
    return (base / "SEVENTHWELL" / "Licenses").u8string();
#endif
}

Verdict productState(const std::string& code, int pluginMajor) {
    if (!enforced()) {
        Verdict v;
        v.enforced = false;
        v.licensed = true;
        v.detail = "development build: licences are not checked";
        return v;
    }
    static std::mutex m;
    static std::string machine;
    static bool haveMachine = false;
    {
        std::lock_guard<std::mutex> l(m);
        if (!haveMachine) { machine = machineHash(kMachineSalt); haveMachine = true; }
    }
    const auto& keys = builtInKeys();
    return evaluate(licenseFolder(), keys.data(), keys.size(), acceptedProducts(code), pluginMajor, machine);
}

}  // namespace sw::license
