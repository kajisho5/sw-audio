// SW AUDIO core — base64 decoding for data the screen sends as text (a reference file in pieces).
#pragma once
#include <cstdint>
#include <vector>

namespace sw {

// Appends the bytes of `text` (standard alphabet, '=' padding; spaces and line breaks are skipped) to `out`. False for any other character or a bad length (the bytes decoded so far stay in `out`).
inline bool base64Decode(const char* text, std::vector<uint8_t>& out) {
    if (!text) return true;
    uint32_t acc = 0; int bits = 0, pad = 0, n = 0;
    for (const char* p = text; *p; ++p) {
        const char c = *p; int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') { ++pad; ++n; continue; }
        else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        else return false;
        if (pad) return false;   // data after the padding
        ++n; acc = (acc << 6) | static_cast<uint32_t>(v); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF)); }
    }
    return n % 4 == 0;
}

}  // namespace sw
