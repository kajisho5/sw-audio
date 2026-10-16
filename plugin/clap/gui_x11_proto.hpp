// Linux window, the part both sides share: the plug-in (gui_x11.cpp, inside the host) and the helper process (gui_x11_helper.cpp, GTK + WebKitGTK, one per open window) talk through a socket pair
// in frames: 1 byte type, 4 bytes little-endian length, the payload. No GTK, no X11 here: plain C++ (unit-tested in tests/test_gui_x11_proto.cpp).
//   plug-in -> helper:  H the page (HTML), once, first   S a script to run in the page   Z "<width> <height>" the size of the window   V "1" / "0" shown / hidden   Q quit
//   helper -> plug-in:  M a message of the page (the text the page posts: "s 3 0.5", "c zoom 150", "p" ...)   P the X window id of the helper's window (decimal)   R the page has loaded   E an error text
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace sw::gui::x11 {

constexpr size_t kMaxPayload = size_t(64) << 20;   // the page of a product is a few MB (fonts and design); 64 MB is far above it and far below a runaway

struct Frame { char type = 0; std::string payload; };

inline std::vector<uint8_t> encode(char type, const std::string& payload) {
    std::vector<uint8_t> out; out.reserve(5 + payload.size());
    out.push_back(static_cast<uint8_t>(type));
    const uint32_t n = static_cast<uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// bytes in (any pieces), whole frames out. A type that is not a letter, or a length above kMaxPayload, makes the reader bad for good: the other side is not speaking this protocol.
class FrameReader {
public:
    void feed(const uint8_t* data, size_t n) { if (!bad_) buf_.insert(buf_.end(), data, data + n); check(); }
    bool next(Frame& f) {
        if (bad_ || buf_.size() < 5) return false;
        const size_t len = length();
        if (buf_.size() < 5 + len) return false;
        f.type = static_cast<char>(buf_[0]); f.payload.assign(buf_.begin() + 5, buf_.begin() + 5 + static_cast<std::ptrdiff_t>(len));
        buf_.erase(buf_.begin(), buf_.begin() + 5 + static_cast<std::ptrdiff_t>(len));
        check();
        return true;
    }
    bool bad() const { return bad_; }
private:
    size_t length() const { return static_cast<size_t>(buf_[1]) | static_cast<size_t>(buf_[2]) << 8 | static_cast<size_t>(buf_[3]) << 16 | static_cast<size_t>(buf_[4]) << 24; }
    void check() {   // the header of the next frame, as soon as it is here
        if (bad_ || buf_.empty()) return;
        const uint8_t t = buf_[0];
        if (!((t >= 'A' && t <= 'Z') || (t >= 'a' && t <= 'z'))) { bad_ = true; return; }
        if (buf_.size() >= 5 && length() > kMaxPayload) bad_ = true;
    }
    std::vector<uint8_t> buf_; bool bad_ = false;
};

// "<width> <height>": both whole numbers of 16 .. 16384, nothing else
inline bool parseSize(const std::string& s, unsigned& w, unsigned& h) {
    char* end = nullptr; const char* p = s.c_str();
    const long a = std::strtol(p, &end, 10); if (end == p || *end != ' ') return false;
    const char* q = end + 1; const long b = std::strtol(q, &end, 10); if (end == q || *end != '\0') return false;
    if (a < 16 || a > 16384 || b < 16 || b > 16384) return false;
    w = static_cast<unsigned>(a); h = static_cast<unsigned>(b); return true;
}

// where the plug-in looks for the helper: next to its own file (a .clap), next to or above its own file inside a .vst3 bundle (Contents/x86_64-linux/<name>.so), then the fixed places.
// (SW_AUDIO_GUI_HELPER, the environment variable, is tried first by the caller.)
inline std::vector<std::string> helperCandidates(const std::string& selfPath) {
    std::vector<std::string> out; const std::string name = "sw-audio-gui";
    std::string dir = selfPath;
    for (int up = 0; up < 5 && !dir.empty(); ++up) {
        const size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        dir.resize(slash);
        out.push_back(dir + "/" + name);
    }
    const char* home = std::getenv("HOME");
    if (home && *home) out.push_back(std::string(home) + "/.local/share/sw-audio/" + name);
    out.push_back("/usr/local/lib/sw-audio/" + name);
    out.push_back("/usr/lib/sw-audio/" + name);
    return out;
}

}  // namespace sw::gui::x11
