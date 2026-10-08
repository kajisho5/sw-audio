// SW AUDIO plug-in layer: the user preset folder and saving into it (plugin/clap/user_presets.hpp), end to end with SW IN07:
// save a sound, find it in the folder, read it back into another engine.
#include "doctest.h"
#include "user_presets.hpp"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;
using namespace sw::in07;

#if !defined(_WIN32) && !defined(__APPLE__)
TEST_CASE("USER PRESETS: the folder follows XDG_DATA_HOME, else ~/.local/share (Linux)") {
    const char* oldXdg = std::getenv("XDG_DATA_HOME");
    const std::string keep = oldXdg ? oldXdg : "";
    setenv("XDG_DATA_HOME", "/data/x", 1);
    CHECK(sw::userpresets::folder("SEVENTHWELL", "SWINGBY") == "/data/x/SEVENTHWELL/SWINGBY/Presets");
    setenv("XDG_DATA_HOME", "relative/path", 1);   // not absolute: ignored (the XDG rule)
    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) CHECK(sw::userpresets::folder("SEVENTHWELL", "SWINGBY") == home + "/.local/share/SEVENTHWELL/SWINGBY/Presets");
    if (oldXdg) setenv("XDG_DATA_HOME", keep.c_str(), 1); else unsetenv("XDG_DATA_HOME");
}
#endif

TEST_CASE("USER PRESETS: a saved sound is found in the folder and loads back the same (names cleaned, no silent overwrite)") {
    const fs::path dir = fs::temp_directory_path() / ("sw_user_presets_" + std::to_string(std::random_device{}())) / "Presets";
    Processor a; a.prepare(48000, 256);
    applyPreset(a, 40);
    a.setParam(lp(0, Cutoff), 777.0);
    std::string err;
    const std::string text = userPresetText(a, {"Deep/../Night: Bass", "BASS", "", ""});
    const std::string path = sw::userpresets::save(dir.u8string(), "Deep/../Night: Bass", text, false, err);
    REQUIRE_MESSAGE(!path.empty(), err);
    CHECK(fs::path(fs::u8path(path)).filename().u8string() == "Deep_Night_Bass.swpreset");
    CHECK(fs::path(fs::u8path(path)).parent_path() == dir);   // never outside the folder
    CHECK(sw::userpresets::save(dir.u8string(), "Deep/../Night: Bass", text, false, err).empty());   // exists: refused
    CHECK_FALSE(err.empty());
    CHECK_FALSE(sw::userpresets::save(dir.u8string(), "Deep/../Night: Bass", text, true, err).empty());   // overwrite when asked
    const auto files = sw::presetfile::listFiles(dir.u8string(), sw::presetfile::kExtension);
    REQUIRE(files.size() == 1);
    std::string back;
    REQUIRE(sw::presetfile::readFile(files[0], back, err));
    Processor b; b.prepare(48000, 256);
    sw::presetfile::Meta m;
    REQUIRE(loadUserPreset(b, back, m, err));
    CHECK(m.name == "Deep/../Night: Bass");   // the name inside the file keeps what the user typed (cleaned of control characters only)
    CHECK(m.category == "BASS");
    for (int j = 0; j < kNumParams; ++j)
        if (j != PresetSelect) CHECK(b.param(j) == doctest::Approx(a.param(j)).epsilon(1e-12).scale(1.0));
    CHECK(sw::userpresets::save("", "x", text, false, err).empty());
    fs::remove_all(dir.parent_path());
}
