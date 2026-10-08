// SW AUDIO — where a product keeps its user presets, and saving one there (no CLAP dependency: the unit tests use it directly).
//   Windows: <Documents>\<vendor>\<product>\Presets            (the Documents known folder, also when it was moved, e.g. to OneDrive)
//   macOS:   ~/Library/Audio/Presets/<vendor>/<product>          (where macOS keeps audio plug-in presets)
//   Linux:   $XDG_DATA_HOME/<vendor>/<product>/Presets           (~/.local/share when XDG_DATA_HOME is not set or not absolute)
// Paths are UTF-8. Saving cleans the name into a file name every platform accepts and writes atomically (sw/preset_file.hpp).
#pragma once
#include "sw/preset_file.hpp"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace sw::userpresets {

// the folder (it may not exist yet); empty when the home folder is unknown
inline std::string folder(const char* vendor, const char* product) {
    namespace fs = std::filesystem;
    fs::path base;
#if defined(_WIN32)
    PWSTR docs = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs) == S_OK && docs) base = fs::path(docs);
    if (docs) CoTaskMemFree(docs);
    if (base.empty()) return {};
    return (base / fs::u8path(vendor) / fs::u8path(product) / "Presets").u8string();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    base = fs::u8path(home);
    return (base / "Library" / "Audio" / "Presets" / fs::u8path(vendor) / fs::u8path(product)).u8string();
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg == '/') base = fs::u8path(xdg);
    else {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return {};
        base = fs::u8path(home) / ".local" / "share";
    }
    return (base / fs::u8path(vendor) / fs::u8path(product) / "Presets").u8string();
#endif
}

// saves text as <dir>/<safe name>.swpreset (the folder is created); the path it wrote, or empty with a reason
inline std::string save(const std::string& dir, const std::string& name, const std::string& text, bool overwrite, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    if (dir.empty()) { error = "no preset folder"; return {}; }
    std::error_code ec;
    fs::create_directories(presetfile::pathOf(dir), ec);
    if (ec) { error = "the preset folder could not be created"; return {}; }
    const std::string path = (presetfile::pathOf(dir) / fs::u8path(presetfile::safeFileName(name) + "." + presetfile::kExtension)).u8string();
    if (!presetfile::writeFileAtomic(path, text, overwrite, error)) return {};
    return path;
}

}  // namespace sw::userpresets
