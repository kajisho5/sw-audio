// SW AUDIO core — the user preset file (".swpreset"): a small UTF-8 text file a product writes and reads back.
//
//   SW-PRESET 1                  format and its version (a newer version is refused, not guessed at)
//   product=in07                 the product code (another product's preset is refused)
//   name=Night Lead              meta data: name, category, author, comment (cleaned: valid UTF-8, no control characters, length limits)
//   in07.mode=Legato             one line per parameter: its id, then its plain value; a stepped parameter by its label when it has labels
//   in07.l1.flt.cutoff=2400      numbers are written and read with a dot in every locale, in the shortest text that reads back exactly
//
// Presets are shared between users, so a file is read as untrusted input: at most kMaxFileBytes, kMaxLines lines of kMaxLineBytes;
// a NUL byte or a line without '=' refuses the file; a value that is not a finite number, a step that does not exist or an id this
// version does not know is skipped (counted); a number outside its range is clamped into it. The reader gives the values it found;
// the product starts from its defaults and applies them (so a file that names a few parameters is a variation of Init, and a preset
// saved by an older version, without the parameters added since, loads with those at their defaults).
// File helpers: atomic writes (a temporary file in the same folder, then a rename: a crash leaves the old file or the new one, never half
// of one), reads with a size limit, a listing that does not follow links. Paths are UTF-8 strings.
#pragma once
#include "sw/param.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#if !defined(SW_NO_FILES)   // SW_NO_FILES: the text only (the browser trial's WebAssembly build has no files and no exceptions)
#include <filesystem>
#include <fstream>
#endif
#include <locale>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sw::presetfile {

constexpr size_t kMaxFileBytes = 256 * 1024;
constexpr size_t kMaxLineBytes = 1024;
constexpr int kMaxLines = 8192;
constexpr int kFormatVersion = 1;
constexpr int kMaxNameChars = 64;    // code points
constexpr int kMaxTextChars = 256;   // author, comment, category
constexpr const char* kExtension = "swpreset";

struct Meta { std::string name, category, author, comment; };
struct Parsed {
    Meta meta;
    std::vector<std::pair<int, double>> values;   // (index in the spec table, plain value), each index once
    int skipped = 0;                              // parameter lines ignored (unknown id, bad value)
};

// ---- text
namespace detail {
// the next code point of valid UTF-8 at s[i] (length 1..4), or 0 when the bytes there are not valid UTF-8 (overlong, surrogate, > U+10FFFF, cut)
inline int utf8Len(std::string_view s, size_t i, uint32_t& cp) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    if (c < 0x80) { cp = c; return 1; }
    int n = 0; uint32_t min = 0;
    if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1Fu; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0Fu; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07u; min = 0x10000; }
    else return 0;
    if (i + static_cast<size_t>(n) > s.size()) return 0;
    for (int k = 1; k < n; ++k) {
        const unsigned char d = b(i + static_cast<size_t>(k));
        if ((d & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (d & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return n;
}
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
}  // namespace detail

// valid UTF-8 only (invalid bytes dropped), control characters as spaces, runs of spaces as one, trimmed, at most maxChars code points.
// Cleaning a clean text changes nothing.
inline std::string cleanText(std::string_view s, int maxChars) {
    std::string o;
    o.reserve(std::min<size_t>(s.size(), 4 * static_cast<size_t>(std::max(0, maxChars))));
    int chars = 0;
    for (size_t i = 0; i < s.size() && chars < maxChars;) {
        uint32_t cp = 0;
        const int n = detail::utf8Len(s, i, cp);
        if (n == 0) { ++i; continue; }
        const bool control = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || cp == 0x2028 || cp == 0x2029;
        if (control || cp == ' ') {
            if (!o.empty() && o.back() != ' ') { o += ' '; ++chars; }
        } else {
            o.append(s.data() + i, static_cast<size_t>(n));
            ++chars;
        }
        i += static_cast<size_t>(n);
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

// a file name (without the extension) every platform accepts: no path separators or reserved characters, no ".." pieces, no leading dot,
// no trailing dot or space, not a Windows device name, at most 64 code points and 180 bytes; "Untitled" when nothing is left
inline std::string safeFileName(std::string_view name) {
    const std::string clean = cleanText(name, kMaxNameChars);
    std::vector<std::string> pieces;
    std::string cur;
    auto flush = [&] {
        std::string_view p = cur;
        while (!p.empty() && (p.front() == '.' || p.front() == ' ')) p.remove_prefix(1);   // no hidden files, no ".." pieces
        while (!p.empty() && (p.back() == '.' || p.back() == ' ')) p.remove_suffix(1);
        if (!p.empty()) pieces.emplace_back(p);
        cur.clear();
    };
    for (char c : clean) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') flush();
        else cur += c;
    }
    flush();
    std::string o;
    for (size_t i = 0; i < pieces.size(); ++i) o += (i ? "_" : "") + pieces[i];
    // at most 180 bytes, cut at a character
    if (o.size() > 180) {
        size_t cut = 0;
        for (size_t i = 0; i < o.size();) { uint32_t cp; const int n = detail::utf8Len(o, i, cp); if (i + static_cast<size_t>(n) > 180) break; i += static_cast<size_t>(n); cut = i; }
        o.resize(cut);
    }
    while (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();
    if (o.empty()) return "Untitled";
    // Windows device names, with or without an extension (CON, con.txt, COM1, LPT9.x)
    std::string base = o.substr(0, o.find('.'));
    for (auto& c : base) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    const bool device = base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
                        (base.size() == 4 && (base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0) && base[3] >= '1' && base[3] <= '9');
    if (device) o += '_';
    return o;
}

// numbers: a dot in every locale; the shortest text (15..17 significant digits) that reads back to the same double
inline bool parseNumber(std::string_view s, double& out) {
    s = detail::trim(s);
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) return false;   // no hex, inf, nan, commas
    std::istringstream in{std::string(s)};
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail() || !std::isfinite(v)) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    out = v;
    return true;
}
inline std::string formatNumber(double v) {
    if (!std::isfinite(v)) v = 0.0;
    for (int prec = 15; prec <= 17; ++prec) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(prec);
        o << v;
        double back = 0.0;
        if (prec == 17 || (parseNumber(o.str(), back) && back == v)) return o.str();
    }
    return "0";
}

// ---- write and read
// every parameter but skipIndex (a preset selector): a stepped one with labels by its label, the others by their number
inline std::string write(const char* product, const Meta& meta, const std::vector<ParamSpec>& specs, const std::vector<double>& plain, int skipIndex = -1) {
    std::string o = "SW-PRESET " + std::to_string(kFormatVersion) + "\n";
    o += std::string("product=") + product + "\n";
    o += "name=" + cleanText(meta.name, kMaxNameChars) + "\n";
    o += "category=" + cleanText(meta.category, kMaxTextChars) + "\n";
    o += "author=" + cleanText(meta.author, kMaxTextChars) + "\n";
    o += "comment=" + cleanText(meta.comment, kMaxTextChars) + "\n";
    for (size_t i = 0; i < specs.size() && i < plain.size(); ++i) {
        if (static_cast<int>(i) == skipIndex) continue;
        const ParamSpec& s = specs[i];
        o += s.id; o += '=';
        if (s.curve == Curve::Step && !s.labels.empty() && s.labels.size() == s.steps.size()) {
            const double n = s.toNorm(plain[i]);
            const size_t k = static_cast<size_t>(std::lround(n * (s.numSteps() - 1)));
            o += s.labels[std::min(k, s.labels.size() - 1)];
        } else {
            o += formatNumber(plain[i]);
        }
        o += '\n';
    }
    return o;
}

// false (with a reason) for a file that is not this product's preset in this format; otherwise the values it holds (see the top)
inline bool read(std::string_view text, const char* product, const std::vector<ParamSpec>& specs, Parsed& out, std::string& error, int skipIndex = -1) {
    out = Parsed{};
    error.clear();
    auto fail = [&](const char* why) { out = Parsed{}; error = why; return false; };
    if (text.size() > kMaxFileBytes) return fail("the file is too large for a preset");
    if (text.find('\0') != std::string_view::npos) return fail("not a preset file (binary data)");
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos <= text.size();) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        if (nl - pos > kMaxLineBytes) return fail("a line is too long");
        lines.push_back(text.substr(pos, nl - pos));
        if (static_cast<int>(lines.size()) > kMaxLines) return fail("too many lines");
        pos = nl + 1;
    }
    if (lines.empty() || detail::trim(lines[0]).substr(0, 10) != "SW-PRESET ") return fail("not a preset file");
    {
        double ver = 0;
        if (!parseNumber(detail::trim(lines[0]).substr(10), ver) || ver != std::floor(ver) || ver < 1) return fail("not a preset file (no format version)");
        if (ver > kFormatVersion) return fail("this preset was written by a newer version of the plug-in");
    }
    bool productSeen = false;
    std::vector<int> slot(specs.size(), -1);   // index -> position in out.values
    for (size_t li = 1; li < lines.size(); ++li) {
        const std::string_view line = detail::trim(lines[li]);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) return fail("not a preset file (a line without '=')");
        const std::string_view key = detail::trim(line.substr(0, eq)), val = detail::trim(line.substr(eq + 1));
        if (key == "product") {
            if (val != product) return fail("this preset belongs to another product");
            productSeen = true;
            continue;
        }
        if (key == "name") { out.meta.name = cleanText(val, kMaxNameChars); continue; }
        if (key == "category") { out.meta.category = cleanText(val, kMaxTextChars); continue; }
        if (key == "author") { out.meta.author = cleanText(val, kMaxTextChars); continue; }
        if (key == "comment") { out.meta.comment = cleanText(val, kMaxTextChars); continue; }
        int idx = -1;
        for (size_t i = 0; i < specs.size(); ++i) if (key == specs[i].id) { idx = static_cast<int>(i); break; }
        if (idx < 0 || idx == skipIndex) { ++out.skipped; continue; }
        const ParamSpec& s = specs[static_cast<size_t>(idx)];
        double v = 0.0;
        bool ok = false;
        if (s.curve == Curve::Step) {
            for (size_t k = 0; k < s.labels.size() && k < s.steps.size(); ++k) if (val == s.labels[k]) { v = s.steps[k]; ok = true; break; }
            double x = 0.0;
            if (!ok && parseNumber(val, x))
                for (double st : s.steps) if (std::abs(st - x) <= 1e-9 * std::max(1.0, std::abs(st))) { v = st; ok = true; break; }
        } else if (parseNumber(val, v)) {
            v = std::clamp(v, std::min(s.min, s.max), std::max(s.min, s.max));
            ok = true;
        }
        if (!ok) { ++out.skipped; continue; }
        int& at = slot[static_cast<size_t>(idx)];
        if (at < 0) { at = static_cast<int>(out.values.size()); out.values.push_back({idx, v}); }
        else out.values[static_cast<size_t>(at)].second = v;   // the last line counts
    }
    if (!productSeen) return fail("not a preset file (no product)");
    return true;
}

#if !defined(SW_NO_FILES)
// ---- files (UTF-8 paths). A path that is not valid UTF-8 (or, on Windows, a file name with a lone surrogate) makes the standard
// library throw (MSVC's u8path / u8string): these functions catch it and fail like any other unreadable file, so nothing reaches the host.
inline std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::u8path(utf8); }

inline bool readFile(const std::string& path, std::string& out, std::string& error, size_t maxBytes = kMaxFileBytes) try {
    namespace fs = std::filesystem;
    out.clear(); error.clear();
    std::error_code ec;
    const fs::path p = pathOf(path);
    if (!fs::is_regular_file(p, ec)) { error = "no such file"; return false; }
    const auto size = fs::file_size(p, ec);
    if (ec) { error = "the file could not be read"; return false; }
    if (size > maxBytes) { error = "the file is too large for a preset"; return false; }
    std::ifstream in(p, std::ios::binary);
    if (!in) { error = "the file could not be opened"; return false; }
    out.resize(static_cast<size_t>(size));
    in.read(out.data(), static_cast<std::streamsize>(size));
    out.resize(static_cast<size_t>(in.gcount()));
    if (in.peek() != std::ifstream::traits_type::eof()) { out.clear(); error = "the file changed while it was read"; return false; }
    return true;
} catch (const std::exception&) {
    out.clear(); error = "the file could not be read";
    return false;
}

// the whole text or nothing: a temporary file next to the target, then a rename. overwrite = false refuses an existing file.
inline bool writeFileAtomic(const std::string& path, const std::string& text, bool overwrite, std::string& error) try {
    namespace fs = std::filesystem;
    error.clear();
    std::error_code ec;
    const fs::path p = pathOf(path);
    if (!overwrite && fs::exists(p, ec)) { error = "a preset with this name already exists"; return false; }
    if (fs::exists(p, ec) && !fs::is_regular_file(p, ec)) { error = "the name belongs to something that is not a preset file"; return false; }
    std::random_device rd;
    char tag[24];
    std::snprintf(tag, sizeof tag, ".tmp%08x", static_cast<unsigned>(rd()));
    fs::path tmp = p; tmp += tag;
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) { error = "the preset folder cannot be written to"; return false; }
        o.write(text.data(), static_cast<std::streamsize>(text.size()));
        o.flush();
        if (!o) { o.close(); fs::remove(tmp, ec); error = "the preset could not be written (disk full?)"; return false; }
    }
    if (!overwrite && fs::exists(p, ec)) { fs::remove(tmp, ec); error = "a preset with this name already exists"; return false; }
    fs::rename(tmp, p, ec);   // replaces an existing file (POSIX rename, MoveFileEx with REPLACE_EXISTING)
    if (ec) { fs::remove(tmp, ec); error = "the preset could not be saved"; return false; }
    return true;
} catch (const std::exception&) {
    error = "the file could not be written";
    return false;
}

// the files with the extension in dir and its subfolders (maxDepth levels), sorted, at most maxFiles; links are not followed
inline std::vector<std::string> listFiles(const std::string& dir, const char* ext, int maxDepth = 2, size_t maxFiles = 5000) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const std::string dotExt = std::string(".") + ext;
    std::vector<std::pair<fs::path, int>> todo;
    try { todo.push_back({pathOf(dir), 1}); } catch (const std::exception&) { return out; }
    std::error_code ec;
    while (!todo.empty() && out.size() < maxFiles) {
        const auto [d, depth] = todo.back();
        todo.pop_back();
        fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end;
        if (ec) continue;
        for (; it != end && out.size() < maxFiles; it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& e = *it;
            if (e.is_symlink(ec)) continue;
            if (e.is_directory(ec)) { if (depth < maxDepth) todo.push_back({e.path(), depth + 1}); continue; }
            if (!e.is_regular_file(ec)) continue;
            try {   // a name that cannot be said in UTF-8 (Windows: a lone surrogate) is skipped
                std::string ex = e.path().extension().u8string();
                for (auto& c : ex) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ex == dotExt) out.push_back(e.path().u8string());
            } catch (const std::exception&) { continue; }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

#endif  // SW_NO_FILES

}  // namespace sw::presetfile
