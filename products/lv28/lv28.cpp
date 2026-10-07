#include "lv28/lv28.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <random>

namespace sw::lv28 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv28.allow",   "Allow control",      0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv28.pin",     "Require PIN",        0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv28.perm",    "Default permission", 0, 1, 0, Curve::Step, 1, {0, 1}, "", {"View only", "Control"}},
    };
    return s;
}

namespace {
bool parseV4(const std::string& s, int out[4]) {
    int v[4]; int pos = 0; size_t i = 0;
    for (int part = 0; part < 4; ++part) {
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        int val = 0, digits = 0; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { val = val * 10 + (s[i] - '0'); ++i; if (++digits > 3 || val > 255) return false; }
        v[part] = val; pos = part;
        if (part < 3) { if (i >= s.size() || s[i] != '.') return false; ++i; }
    }
    (void)pos; if (i != s.size()) return false;
    for (int k = 0; k < 4; ++k) out[k] = v[k];
    return true;
}
bool lanV4(const int a[4]) { return a[0] == 10 || a[0] == 127 || (a[0] == 192 && a[1] == 168) || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 169 && a[1] == 254); }
bool constantEq(const std::string& a, const std::string& b) { unsigned d = static_cast<unsigned>(a.size() ^ b.size()); const size_t n = std::max(a.size(), b.size()); for (size_t i = 0; i < n; ++i) d |= static_cast<unsigned>((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0)); return d == 0; }
}

bool isLanAddress(const std::string& ip) {
    int a[4];
    if (parseV4(ip, a)) return lanV4(a);
    std::string s = ip; std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.find(':') == std::string::npos) return false;
    for (char c : s) if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.')) return false;
    if (s.rfind("::ffff:", 0) == 0) { return parseV4(s.substr(7), a) && lanV4(a); }
    if (s == "::1") return true;
    const size_t colon = s.find(':'); if (colon == std::string::npos || colon == 0 || colon > 4) return false;
    const unsigned first = static_cast<unsigned>(std::stoul(s.substr(0, colon), nullptr, 16));
    if (s.substr(0, colon).size() < 1) return false;
    return (first & 0xFE00u) == 0xFC00u || (first & 0xFFC0u) == 0xFE80u;   // fc00::/7, fe80::/10
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

std::string Processor::randomHex(int bytes) {
    std::random_device rd; std::string s; char t[3];
    for (int i = 0; i < bytes; ++i) { std::snprintf(t, sizeof t, "%02x", static_cast<unsigned>(rd() & 0xFF)); s += t; }
    return s;
}
void Processor::rotatePin() {
    std::random_device rd; pin_.clear();
    for (int i = 0; i < kPinDigits; ++i) pin_ += static_cast<char>('0' + rd() % 10);
}
void Processor::prepare(double, int) { rotatePin(); sessions_.clear(); fails_.clear(); locked_ = false; }
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

double Processor::lockedUntil(const std::string& ip) const { const auto it = fails_.find(ip); return it == fails_.end() ? 0.0 : it->second.until; }

AuthReply Processor::authenticate(const std::string& device, const std::string& ip, const std::string& pin, double now) {
    AuthReply r;
    if (target_[AllowControl] < 0.5) { r.result = ControlOff; return r; }
    if (!isLanAddress(ip) || device.empty() || device.size() > 100) { r.result = Refused; return r; }
    Fail& f = fails_[ip];
    if (now < f.until) { r.result = LockedOut; r.retryAfter = f.until - now; return r; }
    if (target_[RequirePin] > 0.5) {
        if (pin_.empty() || !constantEq(pin, pin_)) {
            if (++f.count >= kMaxAttempts) { f.lastLock = f.lastLock <= 0 ? kFirstLockSeconds : std::min(kMaxLockSeconds, f.lastLock * 2.0); f.until = now + f.lastLock; r.retryAfter = f.lastLock; }
            r.result = WrongPin; return r;
        }
    }
    f.count = 0; f.until = 0; f.lastLock = 0;
    Session s; s.device = device; const auto dp = devicePerm_.find(device); s.permission = dp != devicePerm_.end() ? dp->second : static_cast<int>(target_[DefaultPermission] + 0.5); s.expires = now + kTokenSeconds;
    std::string token; do { token = randomHex(16); } while (sessions_.count(token));
    sessions_[token] = s; r.result = Ok; r.token = token; return r;
}

bool Processor::validate(const std::string& token, double now, Session* out) {
    const auto it = sessions_.find(token); if (it == sessions_.end()) return false;
    if (now >= it->second.expires) { sessions_.erase(it); return false; }
    if (out) *out = it->second; return true;
}
bool Processor::canControl(const std::string& token, double now) {
    Session s; if (!validate(token, now, &s)) return false;
    const auto dp = devicePerm_.find(s.device); const int perm = dp != devicePerm_.end() ? dp->second : s.permission;
    return target_[AllowControl] > 0.5 && perm == Control && !locked_;
}
bool Processor::setPermission(const std::string& device, int permission) {
    if (device.empty() || (permission != ViewOnly && permission != Control)) return false;
    devicePerm_[device] = permission; for (auto& kv : sessions_) if (kv.second.device == device) kv.second.permission = permission; return true;
}
void Processor::revoke(const std::string& token) { sessions_.erase(token); }

}  // namespace sw::lv28
