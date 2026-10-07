// The plug-in window's content and its link to the plug-in, without any platform code: the HTML page (screen runtime + the product's parameter table), the text messages the page posts,
// and the script the native side evaluates to bring the page up to date. The platform views (gui_mac.mm, gui_win.cpp) only create a web view, load page() and pass messages to onMessage().
#pragma once
#include "gui_assets.hpp"
#include "sw/param.hpp"
#include <cctype>
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
inline std::string num(double v) { if (!(v == v) || v > 1e300 || v < -1e300) v = 0; char b[40]; std::snprintf(b, sizeof b, "%.10g", v); return b; }

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

inline std::string page(const std::string& code, const std::vector<ParamSpec>& specs, bool autoGain, bool delta, const std::vector<double>& plain, double latencyMs) {
    std::string h = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><style>";
    h.append(reinterpret_cast<const char*>(gui_assets::kCss), gui_assets::kCssSize);
    h += "#app{width:960px;height:550px;position:relative}</style></head><body><div id=\"app\"></div><script>";
    h.append(reinterpret_cast<const char*>(gui_assets::kJs), gui_assets::kJsSize);
    h += "</script><script>var SWBOOT={product:" + metaJson(code) + ",params:" + specsJson(specs) + ",traits:{autoGain:" + (autoGain ? "true" : "false") + ",delta:" + (delta ? "true" : "false") + "},values:[";
    for (size_t i = 0; i < plain.size(); ++i) h += (i ? "," : "") + num(plain[i]);
    h += "],latencyMs:" + num(latencyMs) + "};</script><script>";
    h.append(reinterpret_cast<const char*>(gui_assets::kHostJs), gui_assets::kHostJsSize);
    h += "</script></body></html>";
    return h;
}

inline std::string updateScript(const std::vector<double>& plain, double latencyMs, double cpu, const double* meters) {
    std::string s = "SWHOST.update([";
    for (size_t i = 0; i < plain.size(); ++i) s += (i ? "," : "") + num(plain[i]);
    s += "]," + num(latencyMs) + "," + num(cpu) + ",[";
    for (int i = 0; i < 4; ++i) s += (i ? "," : "") + num(meters[i]);
    return s + "]);";
}

struct Message { char type = 0; int index = -1; double value = 0; std::string name, args; };
inline bool parseMessage(const std::string& m, Message& out) {
    out = Message{};
    if (m.empty() || m.size() > 4096) return false;
    const char t = m[0]; if (t != 's' && t != 'b' && t != 'e' && t != 'c' && t != 'p' && t != 'r') return false;
    if (m.size() > 1 && m[1] != ' ') return false;
    out.type = t;
    if (t == 'p' || t == 'r') return m.size() == 1;
    const char* p = m.c_str() + 2; char* end = nullptr;
    if (t == 'c') { const char* sp = std::strchr(p, ' '); out.name = sp ? std::string(p, sp) : std::string(p); out.args = sp ? std::string(sp + 1) : ""; return !out.name.empty(); }
    const long i = std::strtol(p, &end, 10); if (end == p || i < 0 || i > 100000) return false;
    out.index = static_cast<int>(i);
    if (t == 's') { p = end; if (*p != ' ') return false; const double v = std::strtod(p + 1, &end); if (end == p + 1 || !(v == v)) return false; out.value = v; }
    else if (*end != 0) return false;
    return true;
}

// A window's session: messages in, a script (or nothing) out. F provides numParams(), plain(i), begin(i), set(i, v), end(i), latencyMs(), cpu(), meter(k) (dBFS: in L, in R, out L, out R), call(name, args).
template <class F>
class Session {
public:
    explicit Session(F& f) : f_(f) {}
    std::string onMessage(const std::string& m) {
        Message x; if (!parseMessage(m, x)) return "";
        const int n = f_.numParams();
        switch (x.type) {
            case 'b': if (x.index < n) f_.begin(x.index); break;
            case 'e': if (x.index < n) f_.end(x.index); break;
            case 's': if (x.index < n) f_.set(x.index, x.value); break;
            case 'c': f_.call(x.name, x.args); break;
            case 'p': case 'r': return snapshot();
            default: break;
        }
        return "";
    }
    std::string snapshot() { std::vector<double> v(static_cast<size_t>(f_.numParams())); for (size_t i = 0; i < v.size(); ++i) v[i] = f_.plain(static_cast<int>(i)); double m[4]; for (int k = 0; k < 4; ++k) m[k] = f_.meter(k); return updateScript(v, f_.latencyMs(), f_.cpu(), m); }
private:
    F& f_;
};

}  // namespace sw::gui
