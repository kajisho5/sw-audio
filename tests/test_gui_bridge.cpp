#include "doctest.h"
#include "gui_bridge.hpp"
#include "dy08/dy08.hpp"
#include "lv12/lv12.hpp"
#include <cmath>
using namespace sw;

namespace {
struct Fake {
    std::vector<double> v; std::vector<std::string> log;
    int numParams() { return static_cast<int>(v.size()); }
    double plain(int i) { return v[static_cast<size_t>(i)]; }
    void begin(int i) { log.push_back("b" + std::to_string(i)); }
    void end(int i) { log.push_back("e" + std::to_string(i)); }
    void set(int i, double x) { v[static_cast<size_t>(i)] = x; log.push_back("s" + std::to_string(i)); }
    double latencyMs() { return 1.5; } double cpu() { return -1; } double meter(int k) { return -20.0 - k; } void spectrum(double* o) { for (int i = 0; i < gui::kSpecBands; ++i) o[i] = -80.0 + i; }
    void call(const std::string& n, const std::string& a) { log.push_back("c:" + n + ":" + a); }
};
}

TEST_CASE("GUI messages: the five kinds, and what is refused") {
    gui::Message m;
    CHECK(gui::parseMessage("s 3 1.25", m)); CHECK(m.type == 's'); CHECK(m.index == 3); CHECK(m.value == 1.25);
    CHECK(gui::parseMessage("s 12 -40", m)); CHECK(m.index == 12); CHECK(m.value == -40);
    CHECK(gui::parseMessage("s 0 1e-3", m)); CHECK(std::abs(m.value - 0.001) < 1e-12);
    CHECK(gui::parseMessage("b 7", m)); CHECK(m.type == 'b'); CHECK(m.index == 7);
    CHECK(gui::parseMessage("e 7", m)); CHECK(m.type == 'e');
    CHECK(gui::parseMessage("p", m)); CHECK(gui::parseMessage("r", m));
    CHECK(gui::parseMessage("c tap", m)); CHECK(m.name == "tap"); CHECK(m.args.empty());
    CHECK(gui::parseMessage("c mark Chorus one", m)); CHECK(m.name == "mark"); CHECK(m.args == "Chorus one");
    for (const char* bad : {"", "x", "s", "s ", "s 3", "s 3 ", "s 3 abc", "s -1 0", "s 3 nan", "b", "b x", "b 3 4", "p 1", "sx 3 4", "c", "c ", "s 999999 0"}) CHECK_FALSE(gui::parseMessage(bad, m));
    CHECK_FALSE(gui::parseMessage(std::string(5000, 's'), m));
}

TEST_CASE("GUI session: values and gestures reach the plug-in; a poll answers with a script") {
    Fake f; f.v = {1.0, 2.0, 3.0}; gui::Session<Fake> s(f);
    CHECK(s.onMessage("b 1").empty()); CHECK(s.onMessage("s 1 5.5").empty()); CHECK(s.onMessage("e 1").empty());
    CHECK(f.v[1] == 5.5); CHECK(f.log == std::vector<std::string>{"b1", "s1", "e1"});
    s.onMessage("s 9 1"); s.onMessage("b 9"); s.onMessage("e 9"); s.onMessage("garbage"); CHECK(f.log.size() == 3);   // out of range or malformed: ignored
    s.onMessage("c tap 1 2"); CHECK(f.log.back() == "c:tap:1 2");
    const std::string a = s.onMessage("p"); CHECK(a.rfind("SWHOST.update([1,5.5,3],1.5,-1,[-20,-21,-22,-23],[-80.0,-79.0,", 0) == 0); CHECK(a.substr(a.size() - 2) == ");"); CHECK(s.onMessage("r") == a);
}

TEST_CASE("GUI page: the parameter table of a product is in it, valid and safe") {
    const std::string code = gui::codeOf("com.seventh-well.sw-audio.dy08"); CHECK(code == "DY08");
    std::vector<double> init; for (const auto& p : dy08::specs()) init.push_back(p.def);
    const std::string h = gui::page(code, dy08::specs(), true, true, init, 0.0);
    CHECK(h.find("<!doctype html>") == 0); CHECK(h.find("var SWBOOT=") != std::string::npos); CHECK(h.find("\"dy08.thresh\"") != std::string::npos); CHECK(h.find("SWUI") != std::string::npos);
    CHECK(h.find("\"name\":\"Clean\"") != std::string::npos); CHECK(h.find("traits:{autoGain:true,delta:true}") != std::string::npos);
    // exactly the four script / style blocks and no stray closing tags from the data
    size_t n = 0, pos = 0; while ((pos = h.find("</script>", pos)) != std::string::npos) { ++n; pos += 9; } CHECK(n == 3);
}

TEST_CASE("GUI specs JSON: every field of every product's parameters, and escaping") {
    const std::string j = gui::specsJson(lv12::specs()); CHECK(j.front() == '['); CHECK(j.back() == ']');
    CHECK(j.find("\"name\":\"Band 20 Hz\"") != std::string::npos); CHECK(j.find("\"labels\":[\"Left\",\"Right\",\"Both\"]") != std::string::npos); CHECK(j.find("\"minLabel\":\"Off\"") != std::string::npos);
    CHECK(gui::jsonString("a\"b\\c<d>&\n") == "\"a\\\"b\\\\c\\u003cd\\u003e\\u0026\\u000a\"");
    CHECK(gui::num(std::nan("")) == "0"); CHECK(gui::num(0.1) == "0.1"); CHECK(gui::num(-12) == "-12");
    // balanced brackets (a cheap well-formedness check)
    int depth = 0; bool inStr = false; for (size_t i = 0; i < j.size(); ++i) { const char c = j[i]; if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; } else if (c == '"') inStr = true; else if (c == '[' || c == '{') ++depth; else if (c == ']' || c == '}') --depth; }
    CHECK(depth == 0); CHECK_FALSE(inStr);
}

TEST_CASE("GUI meta: known products carry their colours, unknown ones fall back") {
    const std::string m = gui::metaJson("EQ05"); CHECK(m.find("\"code\":\"EQ05\"") != std::string::npos); CHECK(m.find("#3b7fe6") != std::string::npos);
    const std::string u = gui::metaJson("ZZ99"); CHECK(u.find("\"code\":\"ZZ99\"") != std::string::npos); CHECK(u.find("#f0ad3d") != std::string::npos);
}

TEST_CASE("GUI spectrum: a sine reads its own level in the right band, silence is the floor, the update script carries 64 values") {
    const double fs = 48000.0;
    auto sine = [&](double f, double amp) { std::vector<float> x(gui::kSpecFft); for (int i = 0; i < gui::kSpecFft; ++i) x[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979323846 * f * i / fs)); return x; };
    for (double f : {100.0, 1000.0, 5000.0, 12000.0}) {
        const auto x = sine(f, 0.5); double out[gui::kSpecBands];
        gui::spectrumOf(x.data(), fs, out);
        int pk = 0; for (int b = 1; b < gui::kSpecBands; ++b) if (out[b] > out[pk]) pk = b;
        CHECK(std::abs(gui::specBandFreq(pk) / f - 1.0) < 0.12);     // the loudest band is the sine's (band width is 11 %)
        CHECK(std::abs(out[pk] - (-6.02)) < 1.6);                    // 0.5 amplitude = -6 dB re full scale
    }
    { std::vector<float> z(gui::kSpecFft, 0.f); double out[gui::kSpecBands]; gui::spectrumOf(z.data(), fs, out); for (double v : out) CHECK(v <= -119.0); }
    { gui::SpectrumTap tap; std::vector<float> a(10000), b(10000); for (size_t i = 0; i < a.size(); ++i) { a[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 * 2000.0 * static_cast<double>(i) / fs)); b[i] = a[i]; }
      float* ch[2] = {a.data(), b.data()}; tap.push(ch, 2, 5000); tap.push(ch, 2, 5000);        // more than the ring holds in two pushes, wrapping
      double out[gui::kSpecBands]; tap.compute(fs, out); int pk = 0; for (int i = 1; i < gui::kSpecBands; ++i) if (out[i] > out[pk]) pk = i; CHECK(std::abs(gui::specBandFreq(pk) / 2000.0 - 1.0) < 0.12); }
    Fake f; f.v = {1.0, 2.0}; gui::Session<Fake> s(f); const std::string u = s.onMessage("p");
    CHECK(u.rfind("SWHOST.update([", 0) == 0);
    size_t commas = 0, start = u.rfind(",["); for (size_t i = start; i < u.size(); ++i) commas += u[i] == ',';
    CHECK(commas == gui::kSpecBands);                                // the last array: 64 values -> 63 commas + the one that opens it
}
