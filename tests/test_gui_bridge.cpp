#include "doctest.h"
#include "gui_bridge.hpp"
#include <chrono>
#include <filesystem>
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
    double latencyMs() { return 1.5; } double cpu() { return -1; } double meter(int k) { return -20.0 - k; } void spectrum(double* o) { for (int i = 0; i < gui::kSpecBands; ++i) o[i] = -80.0 + i; } int nro = 0; int numReadouts() { return nro; } double readout(int i) { return -23.5 + i; } void stereo(double* o) { o[0] = 0.5; for (int i = 1; i < 1 + 2 * gui::kGonioPts; ++i) o[i] = 0.25; }
    int nlk = 1; int link(double* o) { o[0] = 2; for (int i = 0; i < gui::kSpecBands; ++i) o[1 + i] = -70.0 + i; return nlk; }   // SW Link: 2 peers (+ their spectrum when nlk is 65)
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
    // SW Link is the last argument: the number of other instances, and the spectrum of them when the page asked
    CHECK(a.size() > 9); CHECK(a.substr(a.size() - 8) == ",[2.0]);");
    f.nlk = 1 + gui::kSpecBands; const std::string b = s.onMessage("p"); CHECK(b.find(",[2.0,-70.0,-69.0,") != std::string::npos); CHECK(b.substr(b.size() - 8) == ",-7.0]);"); CHECK(b.find("nan") == std::string::npos);
}

TEST_CASE("GUI page: the parameter table of a product is in it, valid and safe") {
    const std::string code = gui::codeOf("com.seventh-well.sw-audio.dy08"); CHECK(code == "DY08");
    std::vector<double> init; for (const auto& p : dy08::specs()) init.push_back(p.def);
    const std::string h = gui::page(code, dy08::specs(), true, true, true, init, 0.0);
    CHECK(h.find("<!doctype html>") == 0); CHECK(h.find("var SWBOOT=") != std::string::npos); CHECK(h.find("\"dy08.thresh\"") != std::string::npos); CHECK(h.find("SWUI") != std::string::npos);
    CHECK(h.find("\"name\":\"Clean\"") != std::string::npos); CHECK(h.find("traits:{autoGain:true,delta:true,bypass:true}") != std::string::npos);
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
    size_t commas = 0, start = u.find(",[-80.0"), stop = u.find(']', start); for (size_t i = start; i < stop; ++i) commas += u[i] == ',';
    CHECK(commas == gui::kSpecBands);                                // the last array: 64 values -> 63 commas + the one that opens it
}

TEST_CASE("GUI readouts: the values a core measures follow the spectrum in the update script") {
    Fake f; f.v = {1.0}; f.nro = 3; gui::Session<Fake> s(f); const std::string u = s.onMessage("p");
    CHECK(u.size() > 30); CHECK(u.find(",[-23.5,-22.5,-21.5],[0.5,0.25,0.25,") != std::string::npos);
    Fake g; g.v = {1.0}; gui::Session<Fake> t(g); const std::string w = t.onMessage("p"); CHECK(w.substr(w.size() - 3) == "]);"); CHECK(w.find("-23.5") == std::string::npos); CHECK(w.find("],[],[0.5,") != std::string::npos);   // no readouts: an empty array keeps the place of the stereo values
}

TEST_CASE("GUI readouts: up to kMaxReadouts values go through (RS05 sends a 2 x 64 point waveform); a core cannot send more than that") {
    Fake f; f.v = {1.0}; f.nro = 131; gui::Session<Fake> s(f); const std::string u = s.onMessage("p");
    const size_t a = u.find("],[-23.5,"); REQUIRE(a != std::string::npos);
    const size_t e = u.find("],[", a + 3); REQUIRE(e != std::string::npos);
    size_t commas = 0; for (size_t i = a; i < e; ++i) if (u[i] == ',') ++commas;
    CHECK(commas == 131);                                              // the "],[" before the first value counts one comma, the 130 between 131 values the rest
    Fake g; g.v = {1.0}; g.nro = 5000; gui::Session<Fake> t(g); const std::string w = t.onMessage("p");
    const size_t b = w.find("],[-23.5,"); REQUIRE(b != std::string::npos); const size_t d = w.find("],[", b + 3); size_t c2 = 0; for (size_t i = b; i < d; ++i) if (w[i] == ',') ++c2;
    CHECK(c2 == static_cast<size_t>(gui::kMaxReadouts));               // clipped to the limit
}

TEST_CASE("GUI messages: a button call may be large (a piece of a reference file), nothing else may") {
    gui::Message m;
    CHECK(gui::parseMessage("c refdata " + std::string(300000, 'A'), m)); CHECK(m.name == "refdata"); CHECK(m.args.size() == 300000);
    CHECK_FALSE(gui::parseMessage("c refdata " + std::string(2000000, 'A'), m));    // over the limit
    CHECK_FALSE(gui::parseMessage("s 1 " + std::string(5000, '1'), m));            // other messages stay short
    CHECK(gui::parseMessage("c tap", m)); CHECK(gui::parseMessage("s 3 0.5", m));
}

TEST_CASE("GUI presets: the page's preset menu (list, save, load, delete) through the session; other calls still go to the plug-in") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sw_session_presets_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); fs::remove_all(dir);
    Fake f; f.v = {1.0}; gui::Session<Fake> s(f, dir.string(), "DY08");
    CHECK(s.onMessage("c presetlist") == "SWHOST.presets([],\"\");");
    CHECK(s.onMessage("c presetsave Vocal%20bus dy08.ratio=4;dy08.thresh=-18.5") == "SWHOST.presets([\"Vocal bus\"],\"Vocal bus\");");
    CHECK(s.onMessage("c presetsave %E3%83%9C%E3%83%BC%E3%82%AB%E3%83%AB dy08.ratio=2") == "SWHOST.presets([\"Vocal bus\",\"\xE3\x83\x9C\xE3\x83\xBC\xE3\x82\xAB\xE3\x83\xAB\"],\"\xE3\x83\x9C\xE3\x83\xBC\xE3\x82\xAB\xE3\x83\xAB\");");
    CHECK(s.onMessage("c presetload Vocal%20bus") == "SWHOST.presetLoaded(\"Vocal bus\",\"dy08.ratio=4;dy08.thresh=-18.5\");");
    CHECK(s.onMessage("c presetload nothing").rfind("SWHOST.presetError(", 0) == 0);
    CHECK(s.onMessage("c presetsave bad dy08.ratio=<b>").rfind("SWHOST.presetError(", 0) == 0);    // a body that is not ids and numbers
    CHECK(s.onMessage("c presetsave only-a-name").rfind("SWHOST.presetError(", 0) == 0);            // no body
    CHECK(s.onMessage("c presetdelete Vocal%20bus") == "SWHOST.presets([\"\xE3\x83\x9C\xE3\x83\xBC\xE3\x82\xAB\xE3\x83\xAB\"],\"\");");
    CHECK(f.log.empty());                                            // none of this reached the plug-in
    CHECK(s.onMessage("c tap").empty()); CHECK(f.log.size() == 1); CHECK(f.log[0] == "c:tap:");   // a button call does
    gui::Session<Fake> n(f, "", "DY08"); CHECK(n.onMessage("c presetlist").rfind("SWHOST.presetError(", 0) == 0);   // no folder: the page is told
    fs::remove_all(dir);
}

TEST_CASE("GUI copy / paste of a product's settings: one text per product code, shared by its instances") {
    Fake f; f.v = {1.0};
    gui::Session<Fake> a(f, "", "LV03"), b(f, "", "LV03"), c(f, "", "LV04");
    CHECK(a.onMessage("c presetpaste").rfind("SWHOST.presetError(", 0) == 0);              // nothing copied yet
    CHECK(a.onMessage("c presetcopy lv03.in=-3;lv03.out=2.5").empty());
    CHECK(b.onMessage("c presetpaste") == "SWHOST.presetPasted(\"lv03.in=-3;lv03.out=2.5\");");   // another window of the same product
    CHECK(c.onMessage("c presetpaste").rfind("SWHOST.presetError(", 0) == 0);              // another product has its own
    CHECK(a.onMessage("c presetcopy <b>").rfind("SWHOST.presetError(", 0) == 0);           // not ids and numbers
    CHECK(b.onMessage("c presetpaste") == "SWHOST.presetPasted(\"lv03.in=-3;lv03.out=2.5\");");   // the earlier copy stays
    CHECK(f.log.empty());
}

TEST_CASE("GUI stereo scope: correlation of in-phase, out-of-phase and independent signals; the points are the recent samples") {
    gui::SpectrumTap tap; const int n = 6000; std::vector<float> l(n), r(n), r2(n);
    for (int i = 0; i < n; ++i) { l[i] = static_cast<float>(0.5 * std::sin(0.05 * i)); r[i] = l[i]; r2[i] = -l[i]; }
    double o[1 + 2 * gui::kGonioPts];
    { float* ch[2] = {l.data(), r.data()}; tap.pushStereo(ch, 2, n); tap.stereo(o); CHECK(o[0] > 0.999); CHECK(o[1 + 2 * (gui::kGonioPts - 1)] == doctest::Approx(l[n - 8]).epsilon(1e-6)); }
    { gui::SpectrumTap t2; float* ch[2] = {l.data(), r2.data()}; t2.pushStereo(ch, 2, n); t2.stereo(o); CHECK(o[0] < -0.999); }
    { gui::SpectrumTap t3; std::vector<float> q(n); for (int i = 0; i < n; ++i) q[i] = static_cast<float>(0.5 * std::sin(0.05 * i + 1.5708)); float* ch[2] = {l.data(), q.data()}; t3.pushStereo(ch, 2, n); t3.stereo(o); CHECK(std::abs(o[0]) < 0.1); }
    { gui::SpectrumTap t4; double z[1 + 2 * gui::kGonioPts]; t4.stereo(z); CHECK(z[0] == 0.0); }
}

namespace {
struct FakeWithList : Fake { std::string linkListScript() { return "SWHOST.linkList([[\"LV01\",\"MC mic\",0,-18.2]]);"; } };
}
TEST_CASE("GUI session: 'linklist' is answered with the list of the other SW AUDIO instances when the plug-in has one, and is not passed on as a button") {
    FakeWithList f; f.v = {1.0}; gui::Session<FakeWithList> s(f);
    CHECK(s.onMessage("c linklist") == "SWHOST.linkList([[\"LV01\",\"MC mic\",0,-18.2]]);");
    for (const auto& l : f.log) CHECK(l.rfind("c:linklist", 0) != 0);
    Fake g; g.v = {1.0}; gui::Session<Fake> t(g);
    CHECK(t.onMessage("c linklist").empty());                                       // a facade without the list: nothing (and no button call either)
    for (const auto& l : g.log) CHECK(l.rfind("c:linklist", 0) != 0);
}
