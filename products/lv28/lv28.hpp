// SW LV28 Remote Hub — remote control from a tablet (spec: 仕様書 v1.0「LV28 Remote Hub」). The sound passes untouched (bit for bit, delay 0, no Auto gain, no Delta).
//   **What is here is the security core the spec asks for (its 要確認), without any network code**: the HTTP / WebSocket server (default port kDefaultPort = 8640) and the certificate question (encryption inside the LAN is undecided) are not built.
//   Rules (design values, written down here because the spec only lists the topics):
//   - Only private-network peers are accepted: 10/8, 172.16/12, 192.168/16, 127/8, 169.254/16, ::1, fc00::/7, fe80::/10 (and IPv4-mapped ones); anything else is refused before the PIN is even looked at.
//   - PIN: kPinDigits = 6 random decimal digits (a new one with rotatePin(), at every start). Require PIN Off lets a LAN device in without it (it then gets the default permission).
//   - Attempts: 5 wrong PINs from one address lock that address for 30 s; every further failure doubles the lock (up to 15 min); a right PIN clears the count. Comparison takes the same time whatever digit is wrong.
//   - Token: 128 random bits (hex), made by std::random_device; valid kTokenSeconds = 8 h, bound to the device; revoke(token) / revokeAll(). Every request carries it; nothing but the token is stored per session.
//   - Permissions: per device Control or View only (the default is the parameter "Default permission", View only). Control commands are refused while Allow control is Off, for a View-only device, and while lockAll(true) is on (the spec's "Lock all"):
//     View requests are always answered. authenticate / validate / canControl take the time as an argument (seconds, any monotonic clock) so that the rules are testable.
#pragma once
#include "sw/param.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace sw::lv28 {

enum ParamId { AllowControl, RequirePin, DefaultPermission, kNumParams };
enum Permission { ViewOnly = 0, Control = 1 };
enum AuthResult { Ok = 0, Refused = 1, WrongPin = 2, LockedOut = 3, ControlOff = 4 };
constexpr int kDefaultPort = 8640, kPinDigits = 6, kMaxAttempts = 5;
constexpr double kTokenSeconds = 8.0 * 3600.0, kFirstLockSeconds = 30.0, kMaxLockSeconds = 15.0 * 60.0;

const std::vector<ParamSpec>& specs();
bool isLanAddress(const std::string& ip);

struct Session { std::string device; int permission = ViewOnly; double expires = 0; };
struct AuthReply { int result = Refused; std::string token; double retryAfter = 0; };

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float**, int, int) {}
    int latencySamples() const { return 0; }
    const std::string& pin() const { return pin_; }
    void rotatePin();
    AuthReply authenticate(const std::string& device, const std::string& ip, const std::string& pin, double now);
    bool validate(const std::string& token, double now, Session* out = nullptr);
    bool canControl(const std::string& token, double now);
    bool setPermission(const std::string& device, int permission);
    void revoke(const std::string& token);
    void revokeAll() { sessions_.clear(); }
    int sessionCount() const { return static_cast<int>(sessions_.size()); }
    void lockAll(bool on) { locked_ = on; }
    bool locked() const { return locked_; }
    double lockedUntil(const std::string& ip) const;

private:
    std::string randomHex(int bytes);
    std::array<double, kNumParams> target_{};
    std::string pin_;
    bool locked_ = false;
    std::map<std::string, Session> sessions_;      // token -> session
    std::map<std::string, int> devicePerm_;        // device -> permission
    struct Fail { int count = 0; double until = 0; double lastLock = 0; };
    std::map<std::string, Fail> fails_;            // address -> failures
};

}  // namespace sw::lv28
