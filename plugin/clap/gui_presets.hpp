// User presets of a plug-in window: a preset is a text file (the page writes its body, this only stores it) in the person's <Documents>/SW AUDIO/Presets/<CODE> folder.
// File: "SWPRESET 1 <CODE>" then the body on one line (parameter id=value pairs joined by ';'). No platform code, no CLAP.
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace sw::gui::presets {

constexpr size_t kMaxName = 60, kMaxBody = 65536;

inline std::filesystem::path pathOf(const std::string& s) { return std::filesystem::u8path(s); }
inline bool isCode(const std::string& c) { if (c.empty() || c.size() > 8) return false; for (char x : c) if (!std::isalnum(static_cast<unsigned char>(x))) return false; return true; }

// <Documents>/SW AUDIO/Presets/<CODE> ("" without a Documents folder or with a code that is not letters and digits); see gui_paths.hpp
inline std::string dirFor(const std::string& code, const std::string& documents) {
    if (documents.empty() || !isCode(code)) return "";
    std::string h = documents; while (!h.empty() && (h.back() == '/' || h.back() == '\\')) h.pop_back();
    return h + "/SW AUDIO/Presets/" + code;
}

// a file name: the characters a file system refuses become '-', trimmed, at most kMaxName bytes (never cut inside a UTF-8 character); "" when nothing usable is left
inline std::string cleanName(const std::string& n) {
    std::string o;
    for (unsigned char c : n) o += (c < 0x20 || c == 0x7F || std::string("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string::npos) ? '-' : static_cast<char>(c);
    if (o.size() > kMaxName) { size_t k = kMaxName; while (k > 0 && (static_cast<unsigned char>(o[k]) & 0xC0) == 0x80) --k; o.resize(k); }
    const size_t a = o.find_first_not_of(' '), b = o.find_last_not_of(' ');
    o = a == std::string::npos ? "" : o.substr(a, b - a + 1);
    if (o.find_first_not_of('.') == std::string::npos) return "";   // "", "." and ".."
    return o;
}

inline std::string percentDecode(const std::string& s) {
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) { o += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])); i += 2; }
        else o += s[i];
    }
    return o;
}

inline std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

// the preset names in the folder, case-insensitive order
inline std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(pathOf(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".swpreset") continue;
        out.push_back(it->path().stem().u8string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { const std::string la = lower(a), lb = lower(b); return la != lb ? la < lb : a < b; });
    return out;
}

// the body: one line of ids, numbers and ';' (what the page writes); nothing else is stored
inline bool validBody(const std::string& b) {
    if (b.empty() || b.size() > kMaxBody) return false;
    for (char c : b) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '=' || c == '+' || c == ';' || c == '-')) return false;
    return true;
}

inline bool save(const std::string& dir, const std::string& name, const std::string& code, const std::string& body) {
    const std::string n = cleanName(name);
    if (dir.empty() || n.empty() || !isCode(code) || !validBody(body)) return false;
    std::error_code ec; std::filesystem::create_directories(pathOf(dir), ec); if (ec) return false;
    std::ofstream f(pathOf(dir) / pathOf(n + ".swpreset"), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "SWPRESET 1 " << code << "\n" << body << "\n";
    return static_cast<bool>(f);
}

inline bool load(const std::string& dir, const std::string& name, std::string& body) {
    const std::string n = cleanName(name);
    if (dir.empty() || n.empty()) return false;
    std::ifstream f(pathOf(dir) / pathOf(n + ".swpreset"), std::ios::binary);
    std::string head, line;
    if (!f || !std::getline(f, head) || head.rfind("SWPRESET 1 ", 0) != 0 || !std::getline(f, line)) return false;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (!validBody(line)) return false;
    body = line;
    return true;
}

inline bool remove(const std::string& dir, const std::string& name) {
    const std::string n = cleanName(name);
    if (dir.empty() || n.empty()) return false;
    std::error_code ec; return std::filesystem::remove(pathOf(dir) / pathOf(n + ".swpreset"), ec) && !ec;
}

}  // namespace sw::gui::presets
