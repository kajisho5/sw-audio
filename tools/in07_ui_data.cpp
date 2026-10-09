// SWINGBY: the window's boot data for a browser preview of ui/in07 (tools/in07_ui_preview.py): the parameter table, the values of a
// factory preset (or Init) and the factory list, as JSON on stdout.
//   g++ -std=c++17 -O1 -Icore/include -Iproducts -Iplugin/clap -Ibuild-cmake/gen tools/in07_ui_data.cpp products/in07/*.cpp -o build/in07_ui_data
//   build/in07_ui_data [preset index, -1 = Init] > build/in07_ui/boot.json
#include "gui_bridge.hpp"
#include "in07/in07.hpp"
#include "in07/presets.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    using namespace sw::in07;
    const int index = argc > 1 ? std::atoi(argv[1]) : 0;
    Processor p;
    if (index >= 0) applyPreset(p, index); else applyInit(p);
    if (index >= 0) p.setParam(PresetSelect, index + 1);
    std::string j = "{\"params\":" + sw::gui::specsJson(specs()) + ",\"values\":[";
    for (int i = 0; i < kNumParams; ++i) j += (i ? "," : "") + sw::gui::num(p.param(i));
    j += "],\"presets\":[";
    const auto& P = factoryPresets();
    for (size_t i = 0; i < P.size(); ++i) j += (i ? "," : "") + std::string("{\"name\":") + sw::gui::jsonString(P[i].name) + ",\"category\":" + sw::gui::jsonString(P[i].category) + "}";
    j += "]}";
    std::fwrite(j.data(), 1, j.size(), stdout);
    return 0;
}
