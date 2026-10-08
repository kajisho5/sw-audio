// SW AUDIO core — licence files. Sales are perpetual (買い切り, the owner's decision 2026-10-09): a licence has no end date and covers
// every update of the major version it was bought for. The licence server signs a small text file with Ed25519 (RFC 8032, the
// standard scheme: any server library can sign it); the plug-in checks it offline with the public keys it carries. No secret key is
// ever in a plug-in or in this repository (sign() and keyPairFromSeed() are for the signing tool and the tests).
//
//   SW-LICENSE 1
//   key=1                     which public key (keys can be rotated: a plug-in carries several)
//   product=in07,studio       the products it unlocks (lower-case codes or bundle names)
//   license=SWL-2026-000123   the licence id (from the shop's order; no name or e-mail in the file)
//   machine=<64 hex>          optional: bound to one computer (machineHash); absent = any computer of the owner
//   issued=2026-10-09
//   major=1                   the major version covered
//   sig=<base64, 64 bytes>    Ed25519 over every byte before this line ("\r" removed first, so a file that went through
//                             Windows line ends still verifies); nothing may follow it
//
// The file is read as untrusted input: at most kMaxBytes, strict fields, strict base64. Fields are only trusted after the
// signature checks out.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::license {

constexpr size_t kMaxBytes = 8192;
constexpr const char* kMachineSalt = "SW AUDIO licence v1";   // the salt of machineHash() for licences (the server never sees the raw id)

struct PublicKey { int id = 0; uint8_t key[32] = {}; };
struct License {
    int keyId = 0;
    std::vector<std::string> products;
    std::string id, machine, issued;
    int major = 0;
};
enum class Status { Valid, NotALicense, UnknownKey, BadSignature, WrongProduct, NewerMajor, WrongMachine };

// Valid when the signature checks out with the key the file names, one of its products is accepted, the plug-in's major version is
// covered and, for a bound licence, machine matches. out is filled once the signature checks out (also for WrongProduct,
// NewerMajor and WrongMachine, so a screen can say what the licence is for), and cleared otherwise.
Status check(std::string_view text, const PublicKey* keys, size_t numKeys, const std::vector<std::string>& accepted, int pluginMajor,
             const std::string& machine, License& out);
const char* statusText(Status s);   // a short English reason (the screen translates)

// this computer, as a salted BLAKE2b-256 hash in hex (the raw id never leaves the computer); empty when the OS gives none.
// Windows: MachineGuid; macOS: IOPlatformUUID; Linux: /etc/machine-id
std::string machineHash(const char* salt);

// signing (the signing tool and the tests)
void keyPairFromSeed(const uint8_t seed[32], uint8_t secret[64], uint8_t publicKey[32]);
std::string sign(const License& l, const uint8_t secret[64]);

bool decodeHex(std::string_view s, std::vector<uint8_t>& out);
std::string encodeHex(const uint8_t* d, size_t n);
bool decodeBase64(std::string_view s, std::vector<uint8_t>& out);   // standard alphabet, padded, strict
std::string encodeBase64(const std::vector<uint8_t>& d);

}  // namespace sw::license
