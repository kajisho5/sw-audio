// Writes the plug-in's real window page (gui::page: runtime, fonts, the product's skin and parameter table) to stdout, for tools/gui_page_check.js.
// usage (from the repository root, after the CMake configure has generated build-cmake/gen/ and build-cmake/gen/skins/):
//   g++ -std=c++17 -DSW_SKIN_HEADER='"skin_lv07.hpp"' -DSW_PRODUCT_HEADER='"lv07/lv07.hpp"' -DSW_SPECS=sw::lv07::specs -DSW_CODE='"LV07"' \
//       -Icore/include -Iproducts -Iplugin/clap -Ibuild-cmake/gen -Ibuild-cmake/gen/skins tools/gui_page_dump.cpp products/lv07/*.cpp -o /tmp/dump && /tmp/dump > /tmp/page.html
#include "gui_bridge.hpp"
#include SW_SKIN_HEADER
#include SW_PRODUCT_HEADER
#include <cstdio>
using namespace sw;
int main() {
    const auto& sp = SW_SPECS();
    std::vector<double> v; for (auto& p : sp) v.push_back(p.def);
    gui::Skin sk; sk.css = reinterpret_cast<const unsigned char*>(gui_assets::kSkinCss); sk.cssSize = gui_assets::kSkinCssSize; sk.html = reinterpret_cast<const unsigned char*>(gui_assets::kSkinHtml); sk.htmlSize = gui_assets::kSkinHtmlSize;
    std::string h = gui::page(SW_CODE, sp, true, true, v, 1.5, sk);
    std::fwrite(h.data(), 1, h.size(), stdout);
}
