// SW AUDIO core — licence files (see sw/license.hpp). Ed25519 and BLAKE2b come from the vendored Monocypher 4.0.3.
#include "sw/license.hpp"
#include "../third_party/monocypher/monocypher-ed25519.h"
#include "../third_party/monocypher/monocypher.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif

namespace sw::license {

// ---- encodings
static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool decodeHex(std::string_view s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 2) return false;
    for (size_t i = 0; i < s.size(); i += 2) {
        const int a = hexVal(s[i]), b = hexVal(s[i + 1]);
        if (a < 0 || b < 0) { out.clear(); return false; }
        out.push_back(static_cast<uint8_t>(a * 16 + b));
    }
    return true;
}
std::string encodeHex(const uint8_t* d, size_t n) {
    static const char* const k = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < n; ++i) { o += k[d[i] >> 4]; o += k[d[i] & 15]; }
    return o;
}
static int b64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
bool decodeBase64(std::string_view s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 4) return false;
    for (size_t i = 0; i < s.size(); i += 4) {
        const bool last = i + 4 == s.size();
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i + static_cast<size_t>(k)];
            if (c == '=') {
                if (!last || k < 2) { out.clear(); return false; }
                ++pad; v[k] = 0;
            } else {
                if (pad) { out.clear(); return false; }   // data after padding
                v[k] = b64Val(c);
                if (v[k] < 0) { out.clear(); return false; }
            }
        }
        const uint32_t n = static_cast<uint32_t>(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
        out.push_back(static_cast<uint8_t>(n >> 16));
        if (pad < 2) out.push_back(static_cast<uint8_t>(n >> 8));
        if (pad < 1) out.push_back(static_cast<uint8_t>(n));
    }
    return true;
}
std::string encodeBase64(const std::vector<uint8_t>& d) {
    static const char* const k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < d.size(); i += 3) {
        const uint32_t n = static_cast<uint32_t>(d[i]) << 16 | (i + 1 < d.size() ? static_cast<uint32_t>(d[i + 1]) << 8 : 0u) | (i + 2 < d.size() ? d[i + 2] : 0u);
        o += k[n >> 18 & 63]; o += k[n >> 12 & 63];
        o += i + 1 < d.size() ? k[n >> 6 & 63] : '=';
        o += i + 2 < d.size() ? k[n & 63] : '=';
    }
    return o;
}

// ---- the file
namespace {
bool parseInt(std::string_view s, int lo, int hi, int& v) {
    if (s.empty() || s.size() > 6) return false;
    int x = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; x = x * 10 + (c - '0'); }
    if (x < lo || x > hi) return false;
    v = x;
    return true;
}
bool tokenChars(std::string_view s, size_t maxLen) {   // product codes, licence ids, dates: [A-Za-z0-9._-]
    if (s.empty() || s.size() > maxLen) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
// the fields of the signed part; false when it is not a licence
bool parseFields(std::string_view msg, License& l) {
    l = License{};
    bool haveKey = false, haveProduct = false, haveMajor = false;
    size_t pos = 0;
    int line = 0;
    while (pos < msg.size()) {
        size_t nl = msg.find('\n', pos);
        if (nl == std::string_view::npos) return false;   // the signed part ends with a newline
        const std::string_view s = msg.substr(pos, nl - pos);
        pos = nl + 1;
        if (line++ == 0) { if (s != "SW-LICENSE 1") return false; continue; }
        const size_t eq = s.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "key") { if (!parseInt(v, 1, 9999, l.keyId)) return false; haveKey = true; }
        else if (k == "major") { if (!parseInt(v, 0, 999, l.major)) return false; haveMajor = true; }
        else if (k == "product") {
            l.products.clear();
            size_t a = 0;
            while (a <= v.size()) {
                size_t c = v.find(',', a);
                if (c == std::string_view::npos) c = v.size();
                const std::string_view p = v.substr(a, c - a);
                if (!tokenChars(p, 32) || l.products.size() >= 64) return false;
                l.products.emplace_back(p);
                a = c + 1;
            }
            haveProduct = true;
        }
        else if (k == "license") { if (!tokenChars(v, 64)) return false; l.id = std::string(v); }
        else if (k == "issued") { if (!tokenChars(v, 32)) return false; l.issued = std::string(v); }
        else if (k == "machine") {
            std::vector<uint8_t> b;
            if (v.size() != 64 || !decodeHex(v, b)) return false;
            l.machine = std::string(v);
            for (auto& c : l.machine) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // other fields (a newer server's): signed, kept out of the decision
    }
    return line > 1 && haveKey && haveProduct && haveMajor;
}
}  // namespace

Status check(std::string_view text, const PublicKey* keys, size_t numKeys, const std::vector<std::string>& accepted, int pluginMajor,
             const std::string& machine, License& out) {
    out = License{};
    if (text.size() > kMaxBytes) return Status::NotALicense;
    std::string t;
    t.reserve(text.size());
    for (char c : text) { if (c == '\0') return Status::NotALicense; if (c != '\r') t += c; }
    while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) t.pop_back();   // trailing blank lines
    // the signature line is the last line
    const size_t lineStart = t.rfind('\n');
    if (lineStart == std::string::npos) return Status::NotALicense;
    const std::string_view sigLine = std::string_view(t).substr(lineStart + 1);
    if (sigLine.substr(0, 4) != "sig=") return Status::NotALicense;
    const std::string_view msg = std::string_view(t).substr(0, lineStart + 1);
    License l;
    if (!parseFields(msg, l)) return Status::NotALicense;
    const PublicKey* key = nullptr;
    for (size_t i = 0; i < numKeys; ++i) if (keys[i].id == l.keyId) key = &keys[i];
    if (!key) return Status::UnknownKey;
    std::vector<uint8_t> sig;
    if (!decodeBase64(sigLine.substr(4), sig) || sig.size() != 64) return Status::BadSignature;
    if (crypto_ed25519_check(sig.data(), key->key, reinterpret_cast<const uint8_t*>(msg.data()), msg.size()) != 0) return Status::BadSignature;
    out = l;
    bool product = false;
    for (const auto& p : l.products) for (const auto& a : accepted) product = product || p == a;
    if (!product) return Status::WrongProduct;
    if (pluginMajor > l.major) return Status::NewerMajor;
    if (!l.machine.empty() && l.machine != machine) return Status::WrongMachine;
    return Status::Valid;
}

const char* statusText(Status s) {
    switch (s) {
        case Status::Valid: return "licensed";
        case Status::NotALicense: return "not a licence file";
        case Status::UnknownKey: return "licence from an unknown key (a newer licence for an older plug-in?)";
        case Status::BadSignature: return "the licence was changed or damaged";
        case Status::WrongProduct: return "the licence is for another product";
        case Status::NewerMajor: return "the licence is for an earlier major version";
        case Status::WrongMachine: return "the licence is bound to another computer";
    }
    return "";
}

// ---- this computer
static std::string rawMachineId() {
#if defined(_WIN32)
    wchar_t buf[128];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    std::string o;
    for (const wchar_t* p = buf; *p && o.size() < 120; ++p) o += static_cast<char>(*p < 128 ? *p : '?');   // a GUID: ASCII
    return o;
#elif defined(__APPLE__)
    std::string o;
    io_service_t svc = IOServiceGetMatchingService(MACH_PORT_NULL /* the default main port */, IOServiceMatching("IOPlatformExpertDevice"));
    if (!svc) return o;
    CFTypeRef v = IORegistryEntryCreateCFProperty(svc, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
    IOObjectRelease(svc);
    if (v) {
        char buf[128];
        if (CFGetTypeID(v) == CFStringGetTypeID() && CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof buf, kCFStringEncodingUTF8)) o = buf;
        CFRelease(v);
    }
    return o;
#else
    for (const char* f : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream in(f);
        std::string s;
        if (in && std::getline(in, s)) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
            if (!s.empty() && s.size() < 128) return s;
        }
    }
    return {};
#endif
}
std::string machineHash(const char* salt) {
    const std::string raw = rawMachineId();
    if (raw.empty()) return {};
    std::string in = salt ? salt : "";
    in += '\0';
    in += raw;
    uint8_t h[32];
    crypto_blake2b(h, sizeof h, reinterpret_cast<const uint8_t*>(in.data()), in.size());
    return encodeHex(h, sizeof h);
}

// ---- signing
void keyPairFromSeed(const uint8_t seed[32], uint8_t secret[64], uint8_t publicKey[32]) {
    uint8_t s[32];
    std::memcpy(s, seed, 32);   // Monocypher wipes the seed it is given
    crypto_ed25519_key_pair(secret, publicKey, s);
}
std::string sign(const License& l, const uint8_t secret[64]) {
    auto clean = [](const std::string& s) { std::string o; for (char c : s) if (c != '\n' && c != '\r' && c != ',') o += c; return o; };
    std::string m = "SW-LICENSE 1\nkey=" + std::to_string(l.keyId) + "\nproduct=";
    for (size_t i = 0; i < l.products.size(); ++i) m += (i ? "," : "") + clean(l.products[i]);
    m += "\nlicense=" + clean(l.id) + "\n";
    if (!l.machine.empty()) m += "machine=" + clean(l.machine) + "\n";
    m += "issued=" + clean(l.issued) + "\nmajor=" + std::to_string(l.major) + "\n";
    uint8_t sig[64];
    crypto_ed25519_sign(sig, secret, reinterpret_cast<const uint8_t*>(m.data()), m.size());
    return m + "sig=" + encodeBase64(std::vector<uint8_t>(sig, sig + 64)) + "\n";
}

}  // namespace sw::license
