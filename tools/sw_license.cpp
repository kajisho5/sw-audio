// SW AUDIO licence tool: make a signing key, sign a licence, check one, print this computer's hash.
// The secret key stays with the owner (or the licence server's key store): never in the repository, never in CI, never in a plug-in.
//   build (Linux / macOS):
//     g++ -std=c++17 -O2 -Icore/include tools/sw_license.cpp core/src/license.cpp core/third_party/monocypher/monocypher.c
//         core/third_party/monocypher/monocypher-ed25519.c -o build/sw_license     (one line; macOS: add -framework IOKit -framework CoreFoundation)
//   build/sw_license keygen <secret-key-file>                    writes a new key (hex, owner-only permissions); prints the public key
//   build/sw_license sign <secret-key-file> <key-id> <products> <licence-id> <major> [machine-hash]   prints a licence (issued today, UTC)
//   build/sw_license verify <public-key-hex> <key-id> <licence-file> <product> <major> [machine-hash]
//   build/sw_license machine                                     this computer's hash (for a licence bound to it)
#include "sw/license.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lic = sw::license;

static bool secureRandom(uint8_t* out, size_t n) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < n) { const ssize_t r = read(fd, out + got, n - got); if (r <= 0) { close(fd); return false; } got += static_cast<size_t>(r); }
    close(fd);
    return true;
#endif
}
static bool readSecret(const char* path, uint8_t secret[64]) {
    std::ifstream in(path);
    std::string hex;
    if (!(in >> hex)) return false;
    std::vector<uint8_t> b;
    if (!lic::decodeHex(hex, b) || b.size() != 64) return false;
    std::memcpy(secret, b.data(), 64);
    return true;
}
static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> o;
    std::stringstream ss(s);
    std::string p;
    while (std::getline(ss, p, ',')) o.push_back(p);
    return o;
}

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "keygen" && argc == 3) {
        uint8_t seed[32], secret[64], pub[32];
        if (!secureRandom(seed, sizeof seed)) { std::fprintf(stderr, "no secure random source\n"); return 1; }
        lic::keyPairFromSeed(seed, secret, pub);
        const std::string hex = lic::encodeHex(secret, 64) + "\n";
#if defined(_WIN32)
        FILE* f = nullptr;
        if (_access(argv[2], 0) == 0 || fopen_s(&f, argv[2], "wb") != 0 || !f) { std::fprintf(stderr, "refusing to overwrite or cannot create %s\n", argv[2]); return 1; }
        std::fwrite(hex.data(), 1, hex.size(), f);
        std::fclose(f);
#else
        const int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);   // never overwrite a key; owner-only
        if (fd < 0) { std::fprintf(stderr, "refusing to overwrite or cannot create %s\n", argv[2]); return 1; }
        if (write(fd, hex.data(), hex.size()) != static_cast<ssize_t>(hex.size())) { close(fd); return 1; }
        close(fd);
#endif
        std::memset(secret, 0, sizeof secret);
        std::printf("public key: %s\n", lic::encodeHex(pub, 32).c_str());
        std::printf("keep %s offline (or in the licence server's key store); put the public key into the plug-ins' key table\n", argv[2]);
        return 0;
    }
    if (cmd == "sign" && (argc == 7 || argc == 8)) {
        uint8_t secret[64];
        if (!readSecret(argv[2], secret)) { std::fprintf(stderr, "cannot read the secret key\n"); return 1; }
        lic::License l;
        l.keyId = std::atoi(argv[3]);
        l.products = split(argv[4]);
        l.id = argv[5];
        l.major = std::atoi(argv[6]);
        if (argc == 8) l.machine = argv[7];
        const std::time_t now = std::time(nullptr);
        char day[16];
        std::strftime(day, sizeof day, "%Y-%m-%d", std::gmtime(&now));
        l.issued = day;
        const std::string text = lic::sign(l, secret);
        std::memset(secret, 0, sizeof secret);
        // check what was written with the matching public key before handing it out
        uint8_t again[64], pub[32];
        if (!readSecret(argv[2], again)) return 1;
        std::memcpy(pub, again + 32, 32);   // Monocypher's secret key = seed || public key
        std::memset(again, 0, sizeof again);
        lic::PublicKey k; k.id = l.keyId; std::memcpy(k.key, pub, 32);
        lic::License got;
        if (lic::check(text, &k, 1, l.products, l.major, l.machine, got) != lic::Status::Valid) { std::fprintf(stderr, "the licence does not check out\n"); return 1; }
        std::fputs(text.c_str(), stdout);
        return 0;
    }
    if (cmd == "verify" && (argc == 7 || argc == 8)) {
        std::vector<uint8_t> b;
        if (!lic::decodeHex(argv[2], b) || b.size() != 32) { std::fprintf(stderr, "public key: 64 hex digits\n"); return 1; }
        lic::PublicKey k; k.id = std::atoi(argv[3]); std::memcpy(k.key, b.data(), 32);
        std::ifstream in(argv[4], std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        lic::License got;
        const lic::Status s = lic::check(text, &k, 1, {argv[5]}, std::atoi(argv[6]), argc == 8 ? argv[7] : "", got);
        std::printf("%s\n", lic::statusText(s));
        return s == lic::Status::Valid ? 0 : 1;
    }
    if (cmd == "machine" && argc == 2) {
        const std::string h = lic::machineHash(lic::kMachineSalt);
        std::printf("%s\n", h.empty() ? "(no machine id)" : h.c_str());
        return h.empty() ? 1 : 0;
    }
    std::fprintf(stderr, "usage: %s keygen <secret-file> | sign <secret-file> <key-id> <products> <licence-id> <major> [machine] | "
                         "verify <public-hex> <key-id> <licence-file> <product> <major> [machine] | machine\n", argv[0]);
    return 2;
}
