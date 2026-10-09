// SW IN07 SWINGBY: the plug-in window through the plug-in the hosts load (plugin/clap/in07_traits.hpp + instrument_adapter.hpp), without a
// platform view: what the page posts (values, gestures, notes, presets, the licence, settings) and what comes back to the page and the host.
// The user's folders are redirected to a temporary one (XDG_DATA_HOME). Needs the CLAP headers: built with the plug-ins (SW_TEST_CLAP).
#if defined(SW_TEST_CLAP) && !defined(_WIN32) && !defined(__APPLE__)
#include "doctest.h"
#include "in07_traits.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ig = sw::instgui;
using Plug = sw::clapinst::Plugin<sw::in07clap::In07>;
using T = sw::in07clap::In07;

namespace {
const void* noExt(const clap_host_t*, const char*) { return nullptr; }
void noop(const clap_host_t*) {}
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "test", "SW", "", "1", noExt, noop, noop, noop};

struct Out {
    std::vector<std::pair<uint16_t, clap_id>> ev;   // type, param id
    std::vector<double> values;
    int noteEnds = 0;
    clap_output_events_t list{this, push};
    Out() = default;
    Out(const Out&) = delete;              // list points at this object
    Out& operator=(const Out&) = delete;
    static bool push(const clap_output_events_t* l, const clap_event_header_t* h) {
        Out* o = static_cast<Out*>(l->ctx);
        if (h->type == CLAP_EVENT_PARAM_VALUE) { const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h); o->ev.push_back({h->type, e->param_id}); o->values.push_back(e->value); }
        else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) o->ev.push_back({h->type, reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id});
        else if (h->type == CLAP_EVENT_NOTE_END) ++o->noteEnds;
        return true;
    }
};
struct In {
    std::vector<std::vector<uint8_t>> ev;
    clap_input_events_t list{this, size, get};
    template <class E> void push(const E& e) { std::vector<uint8_t> b(sizeof(E)); std::memcpy(b.data(), &e, sizeof(E)); ev.push_back(b); }
    void midi(uint8_t a, uint8_t b, uint8_t c) {
        clap_event_midi_t e{}; e.header.size = sizeof(e); e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_MIDI; e.port_index = 0;
        e.data[0] = a; e.data[1] = b; e.data[2] = c; push(e);
    }
    static uint32_t size(const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<const In*>(l->ctx)->ev.size()); }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) { return reinterpret_cast<const clap_event_header_t*>(static_cast<const In*>(l->ctx)->ev[i].data()); }
};
double hostValue(Plug& p, int id) { double v = 0; static_cast<const clap_plugin_params_t*>(p.clap()->get_extension(p.clap(), CLAP_EXT_PARAMS))->get_value(p.clap(), static_cast<clap_id>(id), &v); return v; }
// one block of 256; returns the peak
double block(Plug& p, Out& out, In* in = nullptr) {
    static In none;
    std::vector<float> l(256), r(256);
    float* ch[2] = {l.data(), r.data()};
    clap_audio_buffer_t ob{}; ob.data32 = ch; ob.channel_count = 2;
    clap_process_t pr{}; pr.frames_count = 256; pr.audio_outputs = &ob; pr.audio_outputs_count = 1; pr.in_events = in ? &in->list : &none.list; pr.out_events = &out.list; pr.steady_time = -1;
    p.clap()->process(p.clap(), &pr);
    double pk = 0; for (int i = 0; i < 256; ++i) pk = std::max({pk, std::abs(static_cast<double>(l[i])), std::abs(static_cast<double>(r[i]))});
    return pk;
}
// the values of an update script (SW.update([...], ...))
std::vector<double> updateValues(const std::string& s) {
    std::vector<double> v;
    const size_t a = s.find('['), b = s.find(']');
    if (a == std::string::npos || b == std::string::npos) return v;
    size_t i = a + 1;
    while (i < b) { size_t j = s.find(',', i); if (j == std::string::npos || j > b) j = b; double x = 0; sw::presetfile::parseNumber(s.substr(i, j - i), x); v.push_back(x); i = j + 1; }
    return v;
}
std::string b64(const std::string& s) { return ig::base64(reinterpret_cast<const unsigned char*>(s.data()), s.size()); }
struct Env {   // the user's folders in a temporary place
    fs::path dir = fs::temp_directory_path() / ("sw_in07_window_" + std::to_string(std::random_device{}()));
    std::string old; bool had = false;
    Env() { const char* o = std::getenv("XDG_DATA_HOME"); had = o != nullptr; if (o) old = o; fs::create_directories(dir); setenv("XDG_DATA_HOME", dir.c_str(), 1); }
    ~Env() { if (had) setenv("XDG_DATA_HOME", old.c_str(), 1); else unsetenv("XDG_DATA_HOME"); std::error_code ec; fs::remove_all(dir, ec); }
};
struct Stream {
    std::vector<uint8_t> b; size_t pos = 0;
    clap_ostream_t os{this, wr};
    clap_istream_t is{this, rd};
    static int64_t wr(const clap_ostream_t* s, const void* d, uint64_t n) { auto* t = static_cast<Stream*>(s->ctx); t->b.insert(t->b.end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n); return static_cast<int64_t>(n); }
    static int64_t rd(const clap_istream_t* s, void* d, uint64_t n) { auto* t = static_cast<Stream*>(s->ctx); const uint64_t k = std::min<uint64_t>(n, t->b.size() - t->pos); std::memcpy(d, t->b.data() + t->pos, k); t->pos += k; return static_cast<int64_t>(k); }
};
const clap_plugin_state_t* stateExt(Plug& p) { return static_cast<const clap_plugin_state_t*>(p.clap()->get_extension(p.clap(), CLAP_EXT_STATE)); }
}  // namespace

TEST_CASE("IN07 WINDOW: the page, values and gestures to the host, notes from the screen") {
    Env env;
    Plug p(&kHost);
    REQUIRE(p.clap()->activate(p.clap(), 48000, 32, 256));
    const std::string page = p.guiPage();
    size_t closes = 0; for (size_t i = 0; (i = page.find("</script>", i)) != std::string::npos; ++i) ++closes;
    CHECK(closes == 2);
    CHECK(page.find("\"presets\":[{\"name\":") != std::string::npos);
    CHECK(page.find("\"assets\":[\"Barlow_Condensed-400\"") != std::string::npos);
    CHECK(page.size() < 1500000);                       // far under a WebView2 page's 2 MB: the pictures come later
    CHECK(p.guiMessage("c asset 0").rfind("SW.asset(0,\"Barlow_Condensed-400\",\"font/woff2\",\"d09GMg", 0) == 0);   // "wOF2"
    CHECK(p.guiMessage("c asset 999").empty());
    // a value from the page: a gesture to the host, the synth follows
    const int cutoff = sw::in07::lp(0, sw::in07::Cutoff);
    Out o;
    p.guiMessage("b " + std::to_string(cutoff));
    p.guiMessage("s " + std::to_string(cutoff) + " 1234");
    p.guiMessage("e " + std::to_string(cutoff));
    block(p, o);
    REQUIRE(o.ev.size() == 3);
    CHECK(o.ev[0] == std::pair<uint16_t, clap_id>{CLAP_EVENT_PARAM_GESTURE_BEGIN, static_cast<clap_id>(cutoff)});
    CHECK(o.ev[1].first == CLAP_EVENT_PARAM_VALUE);
    CHECK(o.ev[2].first == CLAP_EVENT_PARAM_GESTURE_END);
    CHECK(Plug::hostToPlain(cutoff, o.values[0]) == doctest::Approx(1234).epsilon(1e-9));
    const std::string upd = p.guiMessage("p");
    CHECK(upd.rfind("SW.update([", 0) == 0);
    REQUIRE(updateValues(upd).size() == static_cast<size_t>(sw::in07::kNumParams));
    CHECK(updateValues(upd)[static_cast<size_t>(cutoff)] == doctest::Approx(1234).epsilon(1e-9));
    CHECK(upd.find("\"held\":0") != std::string::npos);
    // a note from the screen sounds, stops, and is not reported to the host (it never sent it)
    p.guiMessage("c factory 0");
    for (int i = 0; i < 8; ++i) block(p, o);
    p.guiMessage("n 60 0.8");
    double pk = 0; for (int i = 0; i < 20; ++i) pk = std::max(pk, block(p, o));
    CHECK(pk > 0.01);
    CHECK(p.guiMessage("p").find("\"note\":1") != std::string::npos);
    p.guiMessage("o 60");
    for (int i = 0; i < 2000; ++i) block(p, o);
    CHECK(o.noteEnds == 0);
    CHECK(p.guiMessage("p").find("\"held\":0") != std::string::npos);
    p.clap()->deactivate(p.clap());
}

TEST_CASE("IN07 WINDOW: factory and user presets, saving, the folder only, the name in the state") {
    Env env;
    Plug p(&kHost);
    REQUIRE(p.clap()->activate(p.clap(), 48000, 32, 256));
    Out o;
    CHECK(p.guiMessage("c factory 3") == "SW.reply(\"loaded\",{\"kind\":\"factory\",\"index\":3,\"path\":\"\",\"name\":\"\",\"category\":\"\"});");
    block(p, o);
    std::vector<double> want;
    T::factoryPresetValues(3, want);
    const int sel = sw::in07::PresetSelect, cut = sw::in07::lp(0, sw::in07::Cutoff);
    CHECK(Plug::hostToPlain(sel, [&] { double v = 0; static_cast<const clap_plugin_params_t*>(p.clap()->get_extension(p.clap(), CLAP_EXT_PARAMS))->get_value(p.clap(), sel, &v); return v; }()) == 4);
    std::string upd = p.guiMessage("p");
    CHECK(updateValues(upd)[static_cast<size_t>(cut)] == doctest::Approx(want[static_cast<size_t>(cut)]).epsilon(1e-9));
    CHECK(p.guiMessage("c factory 99999").find("\"error\"") != std::string::npos);
    CHECK(p.guiMessage("c factory 1.5").find("\"error\"") != std::string::npos);
    // save: a new name, the same name again (asks), overwrite
    const std::string req = "{\"name\":\"Night/Drive \\u00e9\",\"category\":\"BASS\",\"author\":\"Kai\",\"comment\":\"\",\"overwrite\":false}";
    std::string r = p.guiMessage("c save " + b64(req));
    CHECK(r.find("\"ok\":true") != std::string::npos);
    CHECK(r.find("\"category\":\"BASS\"") != std::string::npos);
    const std::string folder = sw::userpresets::folder("SEVENTHWELL", "SWINGBY");
    CHECK(folder.rfind(env.dir.u8string(), 0) == 0);
    CHECK(fs::exists(fs::u8path(folder) / fs::u8path("Night_Drive \xc3\xa9.swpreset")));
    CHECK(p.current().kind == "user");
    CHECK(p.guiMessage("c save " + b64(req)) == "SW.reply(\"saved\",{\"exists\":true});");
    std::string req2 = req; req2.replace(req2.find("false"), 5, "true");
    CHECK(p.guiMessage("c save " + b64(req2)).find("\"ok\":true") != std::string::npos);
    CHECK(p.guiMessage("c save " + b64("{\"name\":\"  \"}")).find("a name is needed") != std::string::npos);
    CHECK(p.guiMessage("c save " + b64("[1]")).find("bad request") != std::string::npos);
    CHECK(p.guiMessage("c save !!").find("bad request") != std::string::npos);
    // the list, then loading it back (another sound in between)
    r = p.guiMessage("c users");
    CHECK(r.find("\"name\":\"Night/Drive \xc3\xa9\"") != std::string::npos);
    CHECK(r.find("\"author\":\"Kai\"") != std::string::npos);
    const std::string path = (fs::u8path(folder) / fs::u8path("Night_Drive \xc3\xa9.swpreset")).u8string();
    p.guiMessage("c factory 10");
    block(p, o);
    r = p.guiMessage("c user " + b64(path));
    CHECK(r.find("\"kind\":\"user\"") != std::string::npos);
    block(p, o);
    CHECK(updateValues(p.guiMessage("p"))[static_cast<size_t>(cut)] == doctest::Approx(want[static_cast<size_t>(cut)]).epsilon(1e-9));
    // nothing outside the folder, nothing but presets
    fs::create_directories(env.dir / "other");
    { std::ofstream f(env.dir / "other" / "x.swpreset"); f << "SW PRESET"; }
    for (const std::string& bad : {std::string("/etc/passwd"), (env.dir / "other" / "x.swpreset").u8string(), folder + "/../../other/x.swpreset", folder + "/x.txt", folder})
        CHECK_MESSAGE(p.guiMessage("c user " + b64(bad)).find("not a preset of the user folder") != std::string::npos, bad);
    // the state keeps the preset's name; an older state (no name) still loads its values
    Stream st;
    REQUIRE(stateExt(p)->save(p.clap(), &st.os));
    const std::string bytes(st.b.begin(), st.b.end());
    CHECK(bytes.find("SWNM") != std::string::npos);
    Plug q(&kHost);
    REQUIRE(stateExt(q)->load(q.clap(), &st.is));
    CHECK(q.current().kind == "user");
    CHECK(q.current().name == "Night/Drive \xc3\xa9");
    Stream old;
    old.b.assign(st.b.begin(), st.b.begin() + static_cast<long>(st.b.size() - (st.b.size() - bytes.find("SWNM"))));
    Plug w(&kHost);
    REQUIRE(stateExt(w)->load(w.clap(), &old.is));
    CHECK(w.current().kind == "init");
    // the host changes the program: the window follows at its next poll
    In in;
    clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.param_id = static_cast<clap_id>(sel); e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = 6;   // step 6 = factory preset 5
    in.push(e);
    block(p, o, &in);
    r = p.guiMessage("p");
    CHECK(r.find("SW.reply(\"loaded\",{\"kind\":\"factory\",\"index\":5") != std::string::npos);
    CHECK(p.guiMessage("p").find("SW.reply") == std::string::npos);
    p.clap()->deactivate(p.clap());
}

TEST_CASE("IN07 WINDOW: the licence and the window settings") {
    Env env;
    Plug p(&kHost);
    REQUIRE(p.clap()->activate(p.clap(), 48000, 32, 256));
    std::string r = p.guiMessage("c lic");
    CHECK(r.find("\"machine\":\"" + sw::license::thisMachine() + "\"") != std::string::npos);
    if (!sw::license::enforced()) CHECK(r.find("\"state\":\"licensed\"") != std::string::npos);   // a development build has no demo
    r = p.guiMessage("c licfile " + b64("hello"));
    CHECK(r.find("\"ok\":false") != std::string::npos);
    CHECK(p.guiMessage("c licfile ###").find("not a licence file") != std::string::npos);
    CHECK(p.guiMessage("c licfile " + b64(std::string(9000, 'x'))).find("not a licence file") != std::string::npos);
    // settings: kept in the user's folder, refused values change nothing; the size follows the zoom
    uint32_t w = 0, h = 0;
    p.guiSize(w, h);
    CHECK(w == 1280); CHECK(h == 860);
    p.guiMessage("c set zoom " + b64("115"));
    p.guiMessage("c set theme " + b64("light"));
    p.guiMessage("c set theme " + b64("pink"));
    p.guiMessage("c set author " + b64("Kai Sato"));
    p.guiMessage("c set motion " + b64("60"));
    p.guiSize(w, h);
    CHECK(w == 1472); CHECK(h == 989);
    const ig::Settings s = ig::loadSettings(ig::settingsPath("SWINGBY"));
    CHECK(s.zoom == "115"); CHECK(s.theme == "light"); CHECK(s.author == "Kai Sato");
    {   // an author cleared to nothing arrives as "-" (a message has no empty words) and is kept empty
        Plug e(&kHost);
        e.guiMessage("c set author -");
        CHECK(ig::loadSettings(ig::settingsPath("SWINGBY")).author.empty());
        e.guiMessage("c set author " + b64("Kai Sato"));
    }
    CHECK(ig::settingsPath("SWINGBY").rfind(env.dir.u8string(), 0) == 0);
    Plug q(&kHost);
    CHECK(q.guiPage().find("\"settings\":{\"motion\":\"60\",\"theme\":\"light\",\"zoom\":\"115\",\"author\":\"Kai Sato\"}") != std::string::npos);
    p.clap()->deactivate(p.clap());
}

TEST_CASE("IN07 MIDI: Program Change picks a factory preset, a learned controller moves a macro, the host's list has groups (2026-10-09)") {
    Env env;
    Plug p(&kHost);
    REQUIRE(p.clap()->activate(p.clap(), 48000, 32, 256));
    const auto* params = static_cast<const clap_plugin_params_t*>(p.clap()->get_extension(p.clap(), CLAP_EXT_PARAMS));
    auto module = [&](int id) { clap_param_info_t i{}; params->get_info(p.clap(), static_cast<uint32_t>(id), &i); return std::string(i.module); };
    CHECK(module(sw::in07::lp(0, sw::in07::Cutoff)) == "Layer 1/Filter");
    CHECK(module(sw::in07::lp(3, sw::in07::Wave)) == "Layer 4/Oscillator");
    CHECK(module(sw::in07::lp(1, sw::in07::AmpA)) == "Layer 2/Amp envelope");
    CHECK(module(sw::in07::FxDelayTime) == "Effects/Delay");
    CHECK(module(sw::in07::FxEqLow) == "Effects/EQ");
    CHECK(module(sw::in07::FxSlot1) == "Effects/Order");
    CHECK(module(sw::in07::Macro1 + 2) == "Macros");
    CHECK(module(sw::in07::modId(2, sw::in07::ModSrc)) == "Mod matrix/Slot 3");
    CHECK(module(sw::in07::arpVel(4)) == "Arpeggiator");
    CHECK(module(sw::in07::gateStep(0)) == "Trance gate");
    CHECK(module(sw::in07::PresetSelect) == "Preset");
    CHECK(module(sw::in07::Level) == "Voice");
    for (int i = 0; i < sw::in07::kNumParams; ++i) CHECK_MESSAGE(!module(i).empty(), i);
    // Program Change 5 = factory preset 6 (selector step 6): loaded, the selector and the changed values reported to the host
    Out o;
    In in; in.midi(0xC0, 5, 0);
    block(p, o, &in);
    CHECK(hostValue(p, sw::in07::PresetSelect) == 6);
    bool selReported = false;
    for (size_t i = 0; i < o.ev.size(); ++i) if (o.ev[i].first == CLAP_EVENT_PARAM_VALUE && o.ev[i].second == static_cast<clap_id>(sw::in07::PresetSelect)) selReported = o.values[i] == 6;
    CHECK(selReported);
    std::vector<double> want; T::factoryPresetValues(5, want);
    const int cut = sw::in07::lp(0, sw::in07::Cutoff);
    CHECK(Plug::hostToPlain(cut, hostValue(p, cut)) == doctest::Approx(want[static_cast<size_t>(cut)]).epsilon(1e-9));
    CHECK(p.guiMessage("p").find("SW.reply(\"loaded\",{\"kind\":\"factory\",\"index\":5") != std::string::npos);
    In pc127; pc127.midi(0xC0, 127, 0);
    block(p, o, &pc127);
    CHECK(hostValue(p, sw::in07::PresetSelect) == 128);
    // MIDI learn: the window asks, the next controller is the macro's; it moves it (reported to the host); forget
    const int m3 = sw::in07::Macro1 + 2;
    p.guiMessage("c learn 2");
    CHECK(p.guiMessage("p").find("\"learn\":2") != std::string::npos);
    In cc; cc.midi(0xB0, 64, 127); cc.midi(0xB0, 1, 127); cc.midi(0xB0, 21, 127);   // the pedal and the mod wheel are not learned
    o.ev.clear(); o.values.clear();
    block(p, o, &cc);
    CHECK(hostValue(p, m3) == doctest::Approx(1.0));
    CHECK(p.guiMessage("p").find("\"learn\":-1,\"cc\":[-1,-1,21,-1,-1,-1,-1,-1]") != std::string::npos);
    bool reported = false;
    for (size_t i = 0; i < o.ev.size(); ++i) if (o.ev[i].second == static_cast<clap_id>(m3) && o.ev[i].first == CLAP_EVENT_PARAM_VALUE) reported = true;
    CHECK(reported);
    In cc0; cc0.midi(0xB1, 21, 0);   // any channel
    block(p, o, &cc0);
    CHECK(hostValue(p, m3) == doctest::Approx(0.0));
    // learning another macro with the same controller takes it over
    p.guiMessage("c learn 4");
    In cc2; cc2.midi(0xB0, 21, 64);
    block(p, o, &cc2);
    CHECK(p.guiMessage("p").find("\"cc\":[-1,-1,-1,-1,21,-1,-1,-1]") != std::string::npos);
    CHECK(hostValue(p, sw::in07::Macro1 + 4) == doctest::Approx(64.0 / 127.0));
    // the state keeps it
    Stream st;
    REQUIRE(stateExt(p)->save(p.clap(), &st.os));
    Plug q(&kHost);
    REQUIRE(stateExt(q)->load(q.clap(), &st.is));
    CHECK(q.guiMessage("p").find("\"cc\":[-1,-1,-1,-1,21,-1,-1,-1]") != std::string::npos);
    p.guiMessage("c forget 4");
    CHECK(p.guiMessage("p").find("\"cc\":[-1,-1,-1,-1,-1,-1,-1,-1]") != std::string::npos);
    In cc3; cc3.midi(0xB0, 21, 0);
    block(p, o, &cc3);
    CHECK(hostValue(p, sw::in07::Macro1 + 4) == doctest::Approx(64.0 / 127.0));   // no longer moved
    CHECK(p.guiMessage("c learn 9").empty());
    CHECK(p.guiMessage("p").find("\"learn\":-1") != std::string::npos);
    p.clap()->deactivate(p.clap());
}
// what a bug check found (2026-10-09): a state from an older version (fewer values) leaves the rest at their defaults; the selector sent
// again at the same step keeps the edits; a program loaded while the window was closed is the preset in use (page, state); a note held in
// the window ends when the window closes; nothing thrown by a file name reaches the host.
TEST_CASE("IN07 WINDOW: older states, the selector sent twice, programs while closed, notes when the window closes (2026-10-09)") {
    Env env;
    const int n = sw::in07::kNumParams, last = n - 1;
    auto param = [](In& in, int id, double v) {
        clap_event_param_value_t e{}; e.header.size = sizeof(e); e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = static_cast<clap_id>(id); e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v; in.push(e);
    };
    {   // a state with one value fewer: the value it lacks goes back to its default, whatever it was before
        Plug a(&kHost);
        Stream st;
        REQUIRE(stateExt(a)->save(a.clap(), &st.os));
        uint32_t count = 0; std::memcpy(&count, st.b.data() + 4, 4);
        REQUIRE(count == static_cast<uint32_t>(n));
        Stream older;
        older.b.assign(st.b.begin(), st.b.begin() + 8 + 8 * (n - 1));
        const uint32_t fewer = count - 1; std::memcpy(older.b.data() + 4, &fewer, 4);
        Plug b(&kHost);
        REQUIRE(b.clap()->activate(b.clap(), 48000, 32, 256));
        const auto& sp = sw::in07::specs()[static_cast<size_t>(last)];
        const double other = Plug::plainToHost(last, sp.def == sp.max ? sp.min : sp.max);
        In in; param(in, last, other);
        Out o; block(b, o, &in);
        REQUIRE(hostValue(b, last) == doctest::Approx(other));
        REQUIRE(stateExt(b)->load(b.clap(), &older.is));
        CHECK(hostValue(b, last) == doctest::Approx(Plug::plainToHost(last, sp.def)));
        b.clap()->deactivate(b.clap());
    }
    Plug p(&kHost);
    REQUIRE(p.clap()->activate(p.clap(), 48000, 32, 256));
    Out o;
    {   // the selector moved to step 6 loads preset 6; the same step again keeps an edit made since
        In a; param(a, sw::in07::PresetSelect, 6); block(p, o, &a);
        const int cut = sw::in07::lp(0, sw::in07::Cutoff);
        In b; param(b, cut, 0.123); block(p, o, &b);
        In c; param(c, sw::in07::PresetSelect, 6); block(p, o, &c);
        CHECK(hostValue(p, cut) == doctest::Approx(0.123));
        In d; param(d, sw::in07::PresetSelect, 7); block(p, o, &d);   // another step loads
        CHECK(hostValue(p, cut) != doctest::Approx(0.123));
    }
    {   // Program Change while no window is open: the page and the state know the preset in use
        In pc; pc.midi(0xC0, 9, 0); block(p, o, &pc);
        CHECK(p.guiPage().find("\"current\":{\"kind\":\"factory\",\"index\":9") != std::string::npos);
        In pc2; pc2.midi(0xC0, 11, 0); block(p, o, &pc2);
        Stream st;
        REQUIRE(stateExt(p)->save(p.clap(), &st.os));
        const std::string all(st.b.begin(), st.b.end());
        CHECK(all.find("\"kind\":\"factory\",\"index\":11") != std::string::npos);
    }
    {   // a note held in the window ends when the window closes
        p.guiMessage("n 60 0.8");
        double pk = 0;
        for (int k = 0; k < 40; ++k) pk = block(p, o);
        CHECK(pk > 1e-3);
        p.closeWindow();
        for (int k = 0; k < 2000; ++k) pk = block(p, o);   // ~10 s: the longest release of the preset in use
        CHECK(pk < 1e-5);
    }
    // an empty argument is "-" (the author cleared); garbage paths are refused without an exception
    CHECK(p.guiMessage("c user " + b64(std::string("\xff\xfe/../x.swpreset"))).find("not a preset of the user folder") != std::string::npos);
    p.clap()->deactivate(p.clap());
}
#endif
