// Where the person's own files go: the Documents folder (what the person sees as "Documents": on Windows it may be redirected, for example into OneDrive, so the environment's
// USERPROFILE\Documents is not always it). Used for presets, the loudness log export and the recorder's default folder: <Documents>/SW AUDIO/...
#pragma once
#include <cstdlib>
#include <string>
#include "gui_view.hpp"

namespace sw::gui {

// "" when there is none (no home folder)
inline std::string documentsDir() {
#ifdef _WIN32
    const std::string d = nativeDocumentsDir();   // gui_win.cpp
    if (!d.empty()) return d;
#endif
    const char* h = std::getenv("HOME"); if (!h || !*h) h = std::getenv("USERPROFILE");
    return h && *h ? std::string(h) + "/Documents" : std::string();
}

}  // namespace sw::gui
