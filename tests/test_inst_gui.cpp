// SW AUDIO plug-in layer: the instruments' window without a platform (plugin/clap/inst_gui.hpp): the messages the page posts, the
// scripts that answer them, the page, the window settings, base64 and the small JSON reader. The page is treated as untrusted input.
#include "doctest.h"
#include "inst_gui.hpp"
#include <clocale>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace ig = sw::instgui;
namespace fs = std::filesystem;

namespace {
struct Fake {
    std::vector<double> v = std::vector<double>(5, 0.0);
    std::vector<std::string> log;
    int numParams() { return static_cast<int>(v.size()); }
    double plain(int i) { return v[static_cast<size_t>(i)]; }
    void begin(int i) { log.push_back("b" + std::to_string(i)); }
    void end(int i) { log.push_back("e" + std::to_string(i)); }
    void set(int i, double x) { v[static_cast<size_t>(i)] = x; log.push_back("s" + std::to_string(i) + "=" + ig::jsNum(x)); }
    void noteOn(int k, double vel) { log.push_back("n" + std::to_string(k) + "@" + ig::jsNum(vel)); }
    void noteOff(int k) { log.push_back("o" + std::to_string(k)); }
    std::string infoJson() { return "{\"bpm\":120}"; }
    std::string extra;
    std::string pollExtra() { std::string e; e.swap(extra); return e; }
    std::string call(const std::string& name, const std::vector<std::string>& args) {
        std::string s = "c:" + name;
        for (const auto& a : args) s += "|" + a;
        log.push_back(s);
        return name == "lic" ? ig::replyScript("licence", "{}") : std::string();
    }
};
}  // namespace

TEST_CASE("INST GUI: messages are parsed strictly; values, gestures, notes and calls reach the plug-in") {
    Fake f;
    ig::Session<Fake> s(f);
    CHECK(s.onMessage("b 2").empty());
    CHECK(s.onMessage("s 2 0.25").empty());
    CHECK(s.onMessage("s 2 -1.5e2").empty());
    CHECK(s.onMessage("e 2").empty());
    CHECK(s.onMessage("n 60 0.8").empty());
    CHECK(s.onMessage("o 60").empty());
    CHECK(s.onMessage("c lic") == "SW.reply(\"licence\",{});");
    s.onMessage("c save QUJD");
    CHECK(f.log == std::vector<std::string>{"b2", "s2=0.25", "s2=-150", "e2", "n60@0.8", "o60", "c:lic", "c:save|QUJD"});
    f.log.clear();
    // refused: out of range, malformed, extra words, double spaces, other letters, huge
    for (const char* bad : {"s 5 1", "s -1 1", "s 2", "s 2 nan", "s 2 inf", "s 2 0x10", "s 2 1,5", "s 2 1 2", "s  2 1", "s 2 1 ", "b", "b 99999999", "e x",
                            "n 128 0.5", "n 60 0", "n 60 1.5", "o", "o 60 1", "c", "c LIC", "c li-c", "c abcdefghijklmnopq", "x 1", "pp", "p 1", "",
                            "c a 1 2 3 4 5 6 7"}) {
        CHECK_MESSAGE(s.onMessage(bad).empty(), bad);
    }
    CHECK(f.log.empty());
    CHECK(s.onMessage(std::string(ig::kMaxMessage + 1, 'p')).empty());
}

TEST_CASE("INST GUI: a poll answers with every value and the info; numbers with a dot whatever the C locale") {
    Fake f;
    f.v = {0.5, -12, 1000, 0, 3};
    ig::Session<Fake> s(f);
    CHECK(s.onMessage("p") == "SW.update([0.5,-12,1000,0,3],{\"bpm\":120});");
    f.extra = "SW.reply(\"loaded\",{});";
    CHECK(s.onMessage("p") == "SW.update([0.5,-12,1000,0,3],{\"bpm\":120});SW.reply(\"loaded\",{});");
    CHECK(s.onMessage("p") == "SW.update([0.5,-12,1000,0,3],{\"bpm\":120});");
    const char* old = std::setlocale(LC_NUMERIC, nullptr);
    const std::string keep = old ? old : "C";
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") || std::setlocale(LC_NUMERIC, "fr_FR.UTF-8")) {
        f.v[0] = 0.25;
        CHECK(s.onMessage("r") == "SW.update([0.25,-12,1000,0,3],{\"bpm\":120});");
        s.onMessage("s 1 -6.5");
        CHECK(f.v[1] == -6.5);
    } else {
        MESSAGE("no comma locale installed: the locale part is not run");
    }
    std::setlocale(LC_NUMERIC, keep.c_str());
}

TEST_CASE("INST GUI: base64 both ways (strict), the flat JSON reader") {
    const std::string t = "SWINGBY — 星 \x01\xff";
    const std::string b = ig::base64(reinterpret_cast<const unsigned char*>(t.data()), t.size());
    std::string back;
    CHECK(ig::unbase64(b, back, 1000));
    CHECK(back == t);
    CHECK(ig::base64(reinterpret_cast<const unsigned char*>("ab"), 2) == "YWI=");
    CHECK_FALSE(ig::unbase64("YWI", back, 100));        // no padding
    CHECK_FALSE(ig::unbase64("YW*=", back, 100));
    CHECK_FALSE(ig::unbase64(b, back, 3));               // larger than allowed
    std::map<std::string, std::string> m;
    CHECK(ig::readFlatJson("{\"name\":\"Deep \\\"Bass\\\" \\u00e9\\ud83c\\udf19\",\"overwrite\":false,\"n\":-1.5e3}", m));
    CHECK(m["name"] == "Deep \"Bass\" \xc3\xa9\xf0\x9f\x8c\x99");
    CHECK(m["overwrite"] == "false");
    CHECK(m["n"] == "-1.5e3");
    CHECK(ig::readFlatJson(" { } ", m));
    CHECK(m.empty());
    for (const char* bad : {"", "[]", "{\"a\":{}}", "{\"a\":[1]}", "{\"a\":null}", "{\"a\":1,\"a\":2}", "{\"a\":1} x", "{\"a\":\"\\ud83c\"}",
                            "{\"a\":\"\\udf19\"}", "{\"a\":\"x\ny\"}", "{\"a\":\"\\q\"}", "{a:1}", "{\"a\":1,}", "{\"a\":nan}", "{\"a\":\"x\""}) {
        CHECK_MESSAGE(!ig::readFlatJson(bad, m), bad);
    }
    CHECK_FALSE(ig::readFlatJson("{\"a\":\"" + std::string(20000, 'x') + "\"}", m));
}

TEST_CASE("INST GUI: the page carries the boot data safely; the pictures go one by one") {
    static const unsigned char css[] = "body{}";
    static const unsigned char js[] = "SW=1";
    static const unsigned char pic[] = {1, 2, 3, 250};
    static const ig::Asset assets[] = {{"core", "image/webp", pic, 4}};
    const ig::Ui ui{css, 6, js, 4, assets, 1, 1280, 860};
    std::vector<sw::ParamSpec> specs = {{"x.a", "A </script> & <b>", 0, 1, 0.5}, {"x.b", "B", 0, 2, 1, sw::Curve::Step, 1, {0, 1, 2}, "", {"Off", "On", "</script>"}}};
    const std::string boot = "{\"params\":" + ig::specsJson(specs) + ",\"name\":" + ig::jsonString("evil</script><script>alert(1)") + ",\"assets\":" + ig::assetsJson(ui) + "}";
    const std::string h = ig::page(ui, boot);
    size_t closes = 0;
    for (size_t i = 0; (i = h.find("</script>", i)) != std::string::npos; ++i) ++closes;
    CHECK(closes == 2);                                  // the boot script and the page script: nothing in the data closes one
    CHECK(h.find("SW=1") != std::string::npos);
    CHECK(h.find("\"assets\":[\"core\"]") != std::string::npos);
    CHECK(ig::assetScript(ui, 0) == "SW.asset(0,\"core\",\"image/webp\",\"AQID+g==\");");
    CHECK(ig::assetScript(ui, 1).empty());
    CHECK(ig::assetScript(ui, -1).empty());
    CHECK(ig::jsonString("a\xe2\x80\xa8" "b") == "\"a\\u2028b\"");
}

TEST_CASE("INST GUI: window settings keep only known keys and allowed values, and survive a damaged file") {
    ig::Settings s;
    CHECK(s.set("motion", "off"));
    CHECK_FALSE(s.set("motion", "120"));
    CHECK(s.set("theme", "light"));
    CHECK_FALSE(s.set("theme", "pink"));
    CHECK(s.set("zoom", "115"));
    CHECK_FALSE(s.set("zoom", "500"));
    CHECK(s.zoomFactor() == doctest::Approx(1.15));
    CHECK(s.set("author", "  Kai\nSato  "));
    CHECK(s.author == "Kai Sato");
    CHECK_FALSE(s.set("path", "/etc"));
    const fs::path dir = fs::temp_directory_path() / ("sw_inst_gui_" + std::to_string(std::random_device{}()));
    const std::string path = (dir / "SWINGBY" / "window.txt").u8string();
    CHECK(ig::saveSettings(path, s));
    const ig::Settings r = ig::loadSettings(path);
    CHECK(r.motion == "off"); CHECK(r.theme == "light"); CHECK(r.zoom == "115"); CHECK(r.author == "Kai Sato");
    ig::Settings d;
    d.read("motion=30\r\ntheme=\nzoom=1000\ngarbage\n=x\nauthor=A=B\n");
    CHECK(d.motion == "30"); CHECK(d.theme == "dark"); CHECK(d.zoom == "100"); CHECK(d.author == "A=B");
    CHECK(d.json() == "{\"motion\":\"30\",\"theme\":\"dark\",\"zoom\":\"100\",\"author\":\"A=B\"}");
    CHECK(ig::loadSettings((dir / "none.txt").u8string()).motion == "60");   // no file: the defaults
    std::error_code ec;
    fs::remove_all(dir, ec);
    CHECK(ig::settingsPath("SWINGBY").find("SWINGBY") != std::string::npos);
}
