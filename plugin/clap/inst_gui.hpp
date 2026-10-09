// SW AUDIO — the instruments' plug-in window without platform or CLAP code (SW IN07 SWINGBY: ui/in07). The adapter (instrument_adapter.hpp)
// builds the page when the host opens the window and answers what the page posts; the platform view (gui_mac.mm, gui_win.cpp) only shows it.
//   page -> native  "s <i> <plain>" a value, "b <i>" / "e <i>" a gesture's begin / end, "n <key> <velocity>" / "o <key>" a note from the
//                   screen, "p" poll (every 50 ms while the page is visible), "r" ready, "c <name> <arg>..." a call
//   native -> page  SW.update(values, info), SW.reply(name, data), SW.asset(i, name, mime, base64)
// The page is trusted no more than a file: messages are parsed strictly and bounded; a call's free text arrives as base64 (UTF-8).
// Numbers go both ways with a dot whatever the host's C locale (sw/preset_file.hpp). The pictures and fonts are not in the page: the page
// asks for them one by one after it is up ("c asset <i>"), so it stays small (a WebView2 page from a string is limited to 2 MB).
// Window settings (motion, theme, size, the author for saved presets) are kept per user in <app data>/SEVENTHWELL/<product>/window.txt.
#pragma once
#include "sw/license.hpp"        // decodeBase64 (strict)
#include "sw/license_state.hpp"  // licenseFolder (the app data folder)
#include "sw/param.hpp"
#include "sw/preset_file.hpp"    // cleanText, parseNumber, formatNumber, readFile, writeFileAtomic
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sw::instgui {

struct Asset { const char* name; const char* mime; const unsigned char* data; unsigned long size; };
struct Ui {
    const unsigned char* css; unsigned long cssSize;
    const unsigned char* js; unsigned long jsSize;
    const Asset* assets; int assetCount;
    unsigned width, height;   // the design's size in CSS pixels at 100 %
};

// ---- text
// a JSON string, safe inside a <script> element (no "<", ">" or "&" as such)
inline std::string jsonString(std::string_view s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '<') o += "\\u003c"; else if (c == '>') o += "\\u003e"; else if (c == '&') o += "\\u0026";
        else if (c < 0x20 || c == 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += static_cast<char>(c);
    }
    // U+2028 / U+2029 are line ends in older JavaScript strings
    for (size_t i; (i = o.find("\xe2\x80\xa8")) != std::string::npos;) o.replace(i, 3, "\\u2028");
    for (size_t i; (i = o.find("\xe2\x80\xa9")) != std::string::npos;) o.replace(i, 3, "\\u2029");
    return o + "\"";
}
inline std::string jsNum(double v) { return std::isfinite(v) ? presetfile::formatNumber(v) : "0"; }

inline std::string base64(const unsigned char* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(d[i]) << 16) | (i + 1 < n ? static_cast<uint32_t>(d[i + 1]) << 8 : 0u) | (i + 2 < n ? d[i + 2] : 0u);
        o += t[(v >> 18) & 63]; o += t[(v >> 12) & 63];
        o += i + 1 < n ? t[(v >> 6) & 63] : '='; o += i + 2 < n ? t[v & 63] : '=';
    }
    return o;
}
// base64 (strict, padded) to bytes, at most maxBytes; false otherwise. "-" is the empty text (a message has no empty words: the page
// sends "-" for an empty argument, e.g. an author cleared to nothing)
inline bool unbase64(std::string_view s, std::string& out, size_t maxBytes) {
    out.clear();
    if (s == "-") return true;
    if (s.size() > (maxBytes + 2) / 3 * 4) return false;
    std::vector<uint8_t> b;
    if (!s.empty() && !license::decodeBase64(s, b)) return false;
    if (b.size() > maxBytes) return false;
    out.assign(b.begin(), b.end());
    return true;
}

// a flat JSON object of strings, booleans and numbers (kept as their text): {"name":"Deep","overwrite":false}. Anything else (nesting,
// arrays, null, a repeated key, text after the object, more than maxBytes) -> false. Strings: the JSON escapes, \u with surrogate pairs.
inline bool readFlatJson(std::string_view s, std::map<std::string, std::string>& out, size_t maxBytes = 16384) {
    out.clear();
    if (s.size() > maxBytes) return false;
    size_t i = 0;
    auto ws = [&] { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; };
    auto hex4 = [&](uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0'); else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10); else return false;
        }
        return true;
    };
    auto utf8 = [](std::string& o, uint32_t cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
        else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    };
    auto str = [&](std::string& o) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
                case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break; case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': {
                    uint32_t v = 0;
                    if (!hex4(v)) return false;
                    if (v >= 0xD800 && v < 0xDC00) {   // a surrogate pair
                        uint32_t lo = 0;
                        if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return false;
                        i += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo >= 0xE000) return false;
                        v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (v >= 0xDC00 && v < 0xE000) return false;
                    utf8(o, v);
                    break;
                }
                default: return false;
            }
        }
        return false;
    };
    ws();
    if (i >= s.size() || s[i] != '{') return false;
    ++i; ws();
    if (i < s.size() && s[i] == '}') { ++i; ws(); return i == s.size(); }
    while (true) {
        std::string k, v;
        ws();
        if (!str(k)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return false;
        ++i; ws();
        if (i >= s.size()) return false;
        if (s[i] == '"') { if (!str(v)) return false; }
        else if (s.compare(i, 4, "true") == 0) { v = "true"; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v = "false"; i += 5; }
        else {
            const size_t a = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
            double d = 0;
            if (i == a || !presetfile::parseNumber(s.substr(a, i - a), d)) return false;
            v = std::string(s.substr(a, i - a));
        }
        if (out.count(k)) return false;
        out.emplace(std::move(k), std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; ws(); return i == s.size(); }
        return false;
    }
}

// ---- the window's settings (per user)
struct Settings {
    std::string motion, theme = "dark", zoom = "100", author;   // motion: empty until chosen (the page follows the OS's reduce-motion)
    static constexpr const char* kZooms[] = {"75", "90", "100", "115", "130"};
    // a known key with an allowed value: kept (true); anything else changes nothing (false)
    bool set(std::string_view k, std::string_view v) {
        if (k == "motion") { if (v == "60" || v == "30" || v == "off") { motion = std::string(v); return true; } return false; }
        if (k == "theme") { if (v == "dark" || v == "light") { theme = std::string(v); return true; } return false; }
        if (k == "zoom") { for (const char* z : kZooms) if (v == z) { zoom = std::string(v); return true; } return false; }
        if (k == "author") { author = presetfile::cleanText(v, presetfile::kMaxNameChars); return true; }
        return false;
    }
    double zoomFactor() const { double z = 100; presetfile::parseNumber(zoom, z); return z / 100.0; }
    std::string text() const { return "motion=" + motion + "\ntheme=" + theme + "\nzoom=" + zoom + "\nauthor=" + author + "\n"; }
    void read(std::string_view t) {
        size_t a = 0;
        for (int lines = 0; a < t.size() && lines < 64; ++lines) {
            size_t b = t.find('\n', a);
            if (b == std::string_view::npos) b = t.size();
            std::string_view line = t.substr(a, b - a);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const size_t eq = line.find('=');
            if (eq != std::string_view::npos) set(line.substr(0, eq), line.substr(eq + 1));
            a = b + 1;
        }
    }
    std::string json() const {
        return "{\"motion\":" + jsonString(motion) + ",\"theme\":" + jsonString(theme) + ",\"zoom\":" + jsonString(zoom) + ",\"author\":" + jsonString(author) + "}";
    }
};
// <app data>/SEVENTHWELL/<product>/window.txt (next to the licence folder); empty when the home folder is unknown
inline std::string settingsPath(const char* product) {
    const std::string lic = license::licenseFolder();
    if (lic.empty()) return {};
    return (presetfile::pathOf(lic).parent_path() / std::filesystem::u8path(product) / "window.txt").u8string();
}
inline Settings loadSettings(const std::string& path) {
    Settings s;
    std::string text, err;
    if (!path.empty() && presetfile::readFile(path, text, err, 4096)) s.read(text);
    return s;
}
inline bool saveSettings(const std::string& path, const Settings& s) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(presetfile::pathOf(path).parent_path(), ec);
    std::string err;
    return presetfile::writeFileAtomic(path, s.text(), true, err);
}

// ---- the page
inline std::string specsJson(const std::vector<ParamSpec>& v) {
    static const char* cn[] = {"lin", "log", "skew", "step", "fader", "symlog"};
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        const ParamSpec& p = v[i];
        if (i) o += ",";
        o += "{\"id\":" + jsonString(p.id) + ",\"name\":" + jsonString(p.name) + ",\"min\":" + jsNum(p.min) + ",\"max\":" + jsNum(p.max) + ",\"def\":" + jsNum(p.def)
           + ",\"curve\":\"" + cn[static_cast<int>(p.curve)] + "\",\"skew\":" + jsNum(p.skew) + ",\"unit\":" + jsonString(p.unit ? p.unit : "")
           + ",\"auto\":" + (p.automatable ? "true" : "false") + ",\"rev\":" + (p.reversed ? "true" : "false") + ",\"maxLabelNorm\":" + jsNum(p.maxLabelNorm);
        if (p.minLabel) o += ",\"minLabel\":" + jsonString(p.minLabel);
        if (p.maxLabel) o += ",\"maxLabel\":" + jsonString(p.maxLabel);
        if (!p.steps.empty()) { o += ",\"steps\":["; for (size_t k = 0; k < p.steps.size(); ++k) o += (k ? "," : "") + jsNum(p.steps[k]); o += "]"; }
        if (!p.labels.empty()) { o += ",\"labels\":["; for (size_t k = 0; k < p.labels.size(); ++k) o += (k ? "," : "") + jsonString(p.labels[k]); o += "]"; }
        o += "}";
    }
    return o + "]";
}
inline std::string assetsJson(const Ui& ui) {
    std::string o = "[";
    for (int i = 0; i < ui.assetCount; ++i) o += (i ? "," : "") + jsonString(ui.assets[i].name);
    return o + "]";
}
inline std::string valuesJson(const std::vector<double>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += ","; o += jsNum(v[i]); }
    return o + "]";
}
// the whole page: the stylesheet, the boot data, the scripts (the boot JSON is safe inside a script: jsonString escapes "<")
inline std::string page(const Ui& ui, const std::string& bootJson) {
    std::string h = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><style>";
    h.append(reinterpret_cast<const char*>(ui.css), ui.cssSize);
    h += "</style></head><body><div id=\"sw\" class=\"sw-dark\"></div><script>window.SWBOOT=" + bootJson + ";</script><script>";
    h.append(reinterpret_cast<const char*>(ui.js), ui.jsSize);
    h += "</script></body></html>";
    return h;
}
inline std::string assetScript(const Ui& ui, int i) {
    if (i < 0 || i >= ui.assetCount) return {};
    const Asset& a = ui.assets[i];
    return "SW.asset(" + std::to_string(i) + "," + jsonString(a.name) + "," + jsonString(a.mime) + ",\"" + base64(a.data, a.size) + "\");";
}
inline std::string replyScript(const char* name, const std::string& dataJson) { return "SW.reply(" + jsonString(name) + "," + dataJson + ");"; }

// ---- messages
struct Message {
    char type = 0;
    int index = -1;          // s, b, e: the parameter; n, o: the key
    double value = 0;        // s: the plain value; n: the velocity
    std::string name;        // c
    std::vector<std::string> args;
};
constexpr size_t kMaxMessage = 65536;   // a licence file (8 KiB) as base64 fits with room
inline bool parseInt(std::string_view s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 6) return false;
    int v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
    if (v < lo || v > hi) return false;
    out = v;
    return true;
}
inline bool parseMessage(std::string_view m, int numParams, Message& out) {
    out = Message{};
    if (m.empty() || m.size() > kMaxMessage) return false;
    std::vector<std::string_view> w;
    size_t a = 0;
    while (a <= m.size() && w.size() < 8) {
        size_t b = m.find(' ', a);
        if (b == std::string_view::npos) b = m.size();
        w.push_back(m.substr(a, b - a));
        a = b + 1;
    }
    if (a <= m.size()) return false;   // more than 8 words
    for (const auto& x : w) if (x.empty()) return false;   // no double spaces, no trailing space
    if (w[0].size() != 1) return false;
    out.type = w[0][0];
    switch (out.type) {
        case 'p': case 'r': return w.size() == 1;
        case 'b': case 'e': return w.size() == 2 && parseInt(w[1], 0, numParams - 1, out.index);
        case 's': return w.size() == 3 && parseInt(w[1], 0, numParams - 1, out.index) && presetfile::parseNumber(w[2], out.value);
        case 'n': return w.size() == 3 && parseInt(w[1], 0, 127, out.index) && presetfile::parseNumber(w[2], out.value) && out.value > 0.0 && out.value <= 1.0;
        case 'o': return w.size() == 2 && parseInt(w[1], 0, 127, out.index);
        case 'c': {
            if (w.size() < 2 || w[1].size() > 16) return false;
            for (char c : w[1]) if (c < 'a' || c > 'z') return false;
            out.name = std::string(w[1]);
            for (size_t k = 2; k < w.size(); ++k) out.args.emplace_back(w[k]);
            return true;
        }
        default: return false;
    }
}

// A window's session: what the page posts in, a script (or nothing) out. F provides
//   numParams(), plain(i), begin(i), set(i, plain), end(i), noteOn(key, velocity), noteOff(key),
//   infoJson() (the page's SW.info: bpm, playing, beat, note, held, demo), call(name, args) -> a script (or empty),
//   pollExtra() -> a script after a poll's update (or empty: e.g. the preset in use changed outside the window).
// The values are sent whole on every poll, but composed again only when one changed.
template <class F>
class Session {
public:
    explicit Session(F& f) : f_(f) {}
    std::string onMessage(std::string_view m) {
        Message x;
        if (!parseMessage(m, f_.numParams(), x)) return {};
        switch (x.type) {
            case 'b': f_.begin(x.index); return {};
            case 'e': f_.end(x.index); return {};
            case 's': f_.set(x.index, x.value); return {};
            case 'n': f_.noteOn(x.index, x.value); return {};
            case 'o': f_.noteOff(x.index); return {};
            case 'c': return f_.call(x.name, x.args);
            case 'p': case 'r': return update(x.type == 'r');
            default: return {};
        }
    }
    std::string update(bool whole = false) {
        const int n = f_.numParams();
        bool changed = whole || static_cast<int>(last_.size()) != n;
        last_.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double v = f_.plain(i);
            if (v != last_[static_cast<size_t>(i)]) { last_[static_cast<size_t>(i)] = v; changed = true; }
        }
        if (changed || values_.empty()) values_ = valuesJson(last_);
        return "SW.update(" + values_ + "," + f_.infoJson() + ");" + f_.pollExtra();
    }
private:
    F& f_;
    std::vector<double> last_;
    std::string values_;
};

}  // namespace sw::instgui
