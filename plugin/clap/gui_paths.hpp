// Where the person's own files go: the Documents folder (what the person sees as "Documents": on Windows it may be redirected, for example into OneDrive, so the environment's
// USERPROFILE\Documents is not always it). Used for presets, the loudness log export and the recorder's default folder: <Documents>/SW AUDIO/...
#pragma once
#include <cstdlib>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // windows.h must not turn std::min / std::max into macros for the code that includes this
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace sw::gui {

// "" when there is none (no home folder)
inline std::string documentsDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf)) && buf[0]) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) { std::string s(static_cast<size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, buf, -1, &s[0], n, nullptr, nullptr); s.resize(static_cast<size_t>(n) - 1); return s; }
    }
#endif
    const char* h = std::getenv("HOME"); if (!h || !*h) h = std::getenv("USERPROFILE");
    return h && *h ? std::string(h) + "/Documents" : std::string();
}

}  // namespace sw::gui
