// The plug-in window's content and its link to the plug-in, without any platform code: the HTML page (screen runtime + the product's parameter table), the text messages the page posts,
// and the script the native side evaluates to bring the page up to date. The platform views (gui_mac.mm, gui_win.cpp) only create a web view, load page() and pass messages to onMessage().
#pragma once
#include "gui_assets.hpp"
#include "gui_presets.hpp"
#include "gui_spectrum.hpp"
#include "sw/param.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <locale>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sw::gui {

inline std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '<') o += "\\u003c"; else if (c == '>') o += "\\u003e"; else if (c == '&') o += "\\u0026";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += static_cast<char>(c);
    }
    return o + "\"";
}
// numbers with a dot whatever the host's C locale (a host may set LC_NUMERIC to one with a decimal comma: "0,5" would break the page)
inline std::string num(double v) {
    if (!(v == v) || v > 1e300 || v < -1e300) v = 0;
    char b[40]; std::snprintf(b, sizeof b, "%.10g", v);
    for (char* c = b; *c; ++c) if (*c == ',') *c = '.';   // %g has no grouping: a comma can only be the decimal point
    return b;
}
inline bool parseNum(const char* p, double& out) {   // the whole text, a dot as the decimal point, finite
    size_t n = 0;
    for (const char* c = p; *c; ++c, ++n) if (!((*c >= '0' && *c <= '9') || *c == '.' || *c == '-' || *c == '+' || *c == 'e' || *c == 'E') || n > 40) return false;
    if (n == 0) return false;
    std::istringstream in{std::string(p)};
    in.imbue(std::locale::classic());
    double v = 0; in >> v;
    if (in.fail() || !std::isfinite(v)) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    out = v;
    return true;
}

inline std::string specsJson(const std::vector<ParamSpec>& v) {
    static const char* cn[] = {"lin", "log", "skew", "step", "fader", "symlog"};
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        const ParamSpec& p = v[i];
        if (i) o += ",";
        o += "{\"id\":" + jsonString(p.id) + ",\"name\":" + jsonString(p.name) + ",\"min\":" + num(p.min) + ",\"max\":" + num(p.max) + ",\"def\":" + num(p.def) + ",\"curve\":\"" + cn[static_cast<int>(p.curve)] + "\",\"skew\":" + num(p.skew)
           + ",\"unit\":" + jsonString(p.unit) + ",\"auto\":" + (p.automatable ? "true" : "false") + ",\"rev\":" + (p.reversed ? "true" : "false") + ",\"maxLabelNorm\":" + num(p.maxLabelNorm);
        if (p.minLabel) o += ",\"minLabel\":" + jsonString(p.minLabel);
        if (p.maxLabel) o += ",\"maxLabel\":" + jsonString(p.maxLabel);
        if (!p.steps.empty()) { o += ",\"steps\":["; for (size_t k = 0; k < p.steps.size(); ++k) o += (k ? "," : "") + num(p.steps[k]); o += "]"; }
        if (!p.labels.empty()) { o += ",\"labels\":["; for (size_t k = 0; k < p.labels.size(); ++k) o += (k ? "," : "") + jsonString(p.labels[k]); o += "]"; }
        o += "}";
    }
    return o + "]";
}

inline std::string metaJson(const std::string& code) {
    for (int i = 0; i < gui_assets::kMetaCount; ++i)
        if (code == gui_assets::kMeta[i].code) return std::string(reinterpret_cast<const char*>(gui_assets::kMeta[i].json), gui_assets::kMeta[i].size);
    return "{\"code\":" + jsonString(code) + ",\"name\":" + jsonString(code) + ",\"line\":\"STUDIO\",\"category\":\"\",\"chassis\":\"\",\"evo\":\"\",\"acc\":\"#f0ad3d\",\"hi\":\"#ffd890\",\"ear\":\"#8a5410\",\"ring\":\"#f0ad3d\"}";
}

// "com.seventh-well.sw-audio.dy08" -> "DY08"
inline std::string codeOf(const char* clapId) {
    std::string s = clapId ? clapId : ""; const size_t d = s.rfind('.');
    std::string c = d == std::string::npos ? s : s.substr(d + 1);
    for (auto& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return c;
}

// the design of the product (css, markup) when the plug-in was built with its skin header; otherwise the generated layout
struct Skin { const unsigned char* css = nullptr; unsigned long cssSize = 0; const unsigned char* html = nullptr; unsigned long htmlSize = 0; };

inline std::string page(const std::string& code, const std::vector<ParamSpec>& specs, bool autoGain, bool delta, bool bypass, const std::vector<double>& plain, double latencyMs, const Skin& skin = {}) {
    std::string h = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><style>";
    h.append(reinterpret_cast<const char*>(gui_assets::kCss), gui_assets::kCssSize);
    h += "body{margin:0;background:#0c0c0d}#app{position:relative}</style></head><body><div id=\"app\"></div><script>";
    h.append(reinterpret_cast<const char*>(gui_assets::kJs), gui_assets::kJsSize);
    h += "</script><script>var SWBOOT={product:" + metaJson(code) + ",params:" + specsJson(specs) + ",traits:{autoGain:" + (autoGain ? "true" : "false") + ",delta:" + (delta ? "true" : "false") + ",bypass:" + (bypass ? "true" : "false") + "},values:[";
    for (size_t i = 0; i < plain.size(); ++i) h += (i ? "," : "") + num(plain[i]);
    h += "],latencyMs:" + num(latencyMs) + "};</script>";
    if (skin.html) {   // text/plain scripts: no escaping needed as long as the markup has no closing script tag (checked when it is generated)
        h += "<script type=\"text/plain\" id=\"swskincss\">"; h.append(reinterpret_cast<const char*>(skin.css), skin.cssSize);
        h += "</script><script type=\"text/plain\" id=\"swskin\">"; h.append(reinterpret_cast<const char*>(skin.html), skin.htmlSize); h += "</script>";
    }
    h += "<script>";
    h.append(reinterpret_cast<const char*>(gui_assets::kHostJs), gui_assets::kHostJsSize);
    h += "</script></body></html>";
    return h;
}

inline std::string updateScript(const std::vector<double>& plain, double latencyMs, double cpu, const double* meters, const double* spectrum = nullptr, const double* readouts = nullptr, int numReadouts = 0, const double* stereo = nullptr, const double* link = nullptr, int numLink = 0) {
    std::string s = "SWHOST.update([";
    for (size_t i = 0; i < plain.size(); ++i) s += (i ? "," : "") + num(plain[i]);
    s += "]," + num(latencyMs) + "," + num(cpu) + ",[";
    for (int i = 0; i < 4; ++i) s += (i ? "," : "") + num(meters[i]);
    s += "]";
    if (spectrum) { s += ",["; for (int i = 0; i < kSpecBands; ++i) { char b[16]; std::snprintf(b, sizeof b, "%.1f", spectrum[i]); s += (i ? "," : ""); s += b; } s += "]"; }
    if (spectrum) { s += ",["; for (int i = 0; i < numReadouts && readouts; ++i) s += (i ? "," : "") + num(readouts[i]); s += "]"; }
    if (spectrum && stereo) { s += ",["; for (int i = 0; i < 1 + 2 * kGonioPts; ++i) { char b[16]; std::snprintf(b, sizeof b, "%.3g", stereo[i]); s += (i ? "," : ""); s += b; } s += "]"; }
    if (spectrum && stereo && link && numLink > 0) { s += ",["; for (int i = 0; i < numLink; ++i) { char b[16]; std::snprintf(b, sizeof b, "%.1f", link[i]); s += (i ? "," : ""); s += b; } s += "]"; }
    return s + ");";
}

struct Message { char type = 0; int index = -1; double value = 0; std::string name, args; };
constexpr size_t kMaxCallMessage = size_t(1) << 20;
inline bool parseMessage(const std::string& m, Message& out) {
    out = Message{};
    // a button call may carry a piece of a file (UT03 reference, base64 text); every other message is short
    if (m.empty() || m.size() > (m[0] == 'c' ? kMaxCallMessage : 4096)) return false;
    const char t = m[0]; if (t != 's' && t != 'b' && t != 'e' && t != 'c' && t != 'p' && t != 'r') return false;
    if (m.size() > 1 && m[1] != ' ') return false;
    out.type = t;
    if (t == 'p' || t == 'r') return m.size() == 1;
    const char* p = m.c_str() + 2; char* end = nullptr;
    if (t == 'c') { const char* sp = std::strchr(p, ' '); out.name = sp ? std::string(p, sp) : std::string(p); out.args = sp ? std::string(sp + 1) : ""; return !out.name.empty(); }
    const long i = std::strtol(p, &end, 10); if (end == p || i < 0 || i > 100000) return false;
    out.index = static_cast<int>(i);
    if (t == 's') { p = end; if (*p != ' ') return false; double v = 0; if (!parseNum(p + 1, v)) return false; out.value = v; }
    else if (*end != 0) return false;
    return true;
}

// A window's session: messages in, a script (or nothing) out. F provides numParams(), plain(i), begin(i), set(i, v), end(i), latencyMs(), cpu(), meter(k) (dBFS: in L, in R, out L, out R), spectrum(double* kSpecBands dB), numReadouts(), readout(i), stereo(double* 1 + 2 * kGonioPts), link(double* 1 + kSpecBands) -> how many values (SW Link: peers, then the others' spectrum), call(name, args).
template <class F>
class Session {
public:
    // presetDir: where the person's presets of this product are kept ("" = no presets: the page's preset menu says so); code = the product code for the file header
    explicit Session(F& f, std::string presetDir = "", std::string code = "") : f_(f), presetDir_(std::move(presetDir)), code_(std::move(code)) {}
    std::string onMessage(const std::string& m) {
        Message x; if (!parseMessage(m, x)) return "";
        const int n = f_.numParams();
        switch (x.type) {
            case 'b': if (x.index < n) f_.begin(x.index); break;
            case 'e': if (x.index < n) f_.end(x.index); break;
            case 's': if (x.index < n) f_.set(x.index, x.value); break;
            case 'c': if (x.name.rfind("preset", 0) == 0) return presetCall(x.name, x.args); f_.call(x.name, x.args); break;
            case 'p': case 'r': return snapshot();
            default: break;
        }
        return "";
    }
    std::string snapshot() { std::vector<double> v(static_cast<size_t>(f_.numParams())); for (size_t i = 0; i < v.size(); ++i) v[i] = f_.plain(static_cast<int>(i)); double m[4]; for (int k = 0; k < 4; ++k) m[k] = f_.meter(k); double sp[kSpecBands]; f_.spectrum(sp); double ro[kMaxReadouts] = {}; const int nro = std::min(kMaxReadouts, f_.numReadouts()); for (int k = 0; k < nro; ++k) ro[k] = f_.readout(k); double st[1 + 2 * kGonioPts]; f_.stereo(st); double lk[1 + kSpecBands] = {}; const int nlk = f_.link(lk); return updateScript(v, f_.latencyMs(), f_.cpu(), m, sp, ro, nro, st, lk, nlk); }
private:
    // the preset menu of the page: presetlist, presetsave <percent-encoded name> <body>, presetload <name>, presetdelete <name>. The page writes and reads the body (id=value pairs); this stores it.
    // Replies (scripts for the page): SWHOST.presets([names], selected), SWHOST.presetLoaded(name, body), SWHOST.presetError(text)
    std::string listScript(const std::string& selected) const {
        std::string s = "SWHOST.presets(["; const auto l = presets::list(presetDir_);
        for (size_t i = 0; i < l.size(); ++i) s += (i ? "," : "") + jsonString(l[i]);
        return s + "]," + jsonString(selected) + ");";
    }
    static std::string errorScript(const std::string& t) { return "SWHOST.presetError(" + jsonString(t) + ");"; }
    // the copy / paste buttons (LV03): one text per product code, shared by every instance of the product in the host's process (the plug-in's own copy of this static)
    static std::string& clipboard(const std::string& code) { static std::map<std::string, std::string> c; return c[code]; }
    std::string presetCall(const std::string& name, const std::string& args) {
        if (name == "presetcopy") { if (!presets::validBody(args)) return errorScript("Could not copy"); clipboard(code_) = args; return ""; }
        if (name == "presetpaste") { const std::string& b = clipboard(code_); return b.empty() ? errorScript("Nothing copied yet") : "SWHOST.presetPasted(" + jsonString(b) + ");"; }
        if (presetDir_.empty()) return errorScript("No folder for presets (no home folder)");
        if (name == "presetlist") return listScript("");
        const size_t sp = args.find(' ');
        const std::string n = presets::percentDecode(sp == std::string::npos ? args : args.substr(0, sp)), body = sp == std::string::npos ? "" : args.substr(sp + 1);
        if (name == "presetsave") return presets::save(presetDir_, n, code_, body) ? listScript(presets::cleanName(n)) : errorScript("Could not save the preset");
        if (name == "presetload") { std::string b; return presets::load(presetDir_, n, b) ? "SWHOST.presetLoaded(" + jsonString(presets::cleanName(n)) + "," + jsonString(b) + ");" : errorScript("Could not read the preset"); }
        if (name == "presetdelete") { presets::remove(presetDir_, n); return listScript(""); }
        return "";
    }
    F& f_;
    std::string presetDir_, code_;
};

}  // namespace sw::gui
