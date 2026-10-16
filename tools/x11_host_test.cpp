// A small X11 host for the Linux plug-in window (run it under Xvfb): it loads a built .clap like a DAW does, opens the plug-in's window inside a window of its own and checks, with the real helper
// process (sw-audio-gui, WebKitGTK) and the real page, what no other test sees:
//   1. the window appears in the host's window (it is painted: many different colours, not the host's grey), at the size the plug-in asked for
//   2. a click on the design's "100%" chip makes the plug-in ask this host for a bigger window (CLAP request_resize); this host says yes and resizes its window; the page then fills the new size
//   3. a drag down on a knob changes a parameter of the plug-in (the pointer reached the page, the page's message reached the plug-in, the value reached the host's parameter)
//   4. hide empties the window, show brings the page back
//   5. closing the window ends the helper process (no process is left behind); with --kill the helper is killed first (a crash of the web engine): the plug-in carries on
// usage: sw-x11-host <plug-in .clap> --chip <x> <y> --knob <x> <y> [--preset <button x y> <name field x y> <Save x y>] [--ppm <file prefix>]      (coordinates: in the design's own pixels, from its top left; EQ01: --chip 797 21 --knob 195 242)
//   (--preset: the preset menu: a click on its button, a click on the name field, the keyboard types a name, a click on Save: the preset file must appear - the keyboard reaches the page; EQ01: --preset 488 20 426 116 537 116)
// exit code 0 = all passed; the reasons are printed. Needs xdotool for the pointer.
#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
Display* gDpy = nullptr; Window gWin = 0;
int gAsks = 0; uint32_t gAskW = 0, gAskH = 0;
int gFails = 0;
void fail(const std::string& m) { std::printf("FAIL %s\n", m.c_str()); ++gFails; }
void pass(const std::string& m) { std::printf("ok   %s\n", m.c_str()); }

// the host's gui extension: the plug-in asks for a size, this host agrees and resizes its window
bool hostResize(const clap_host_t*, uint32_t w, uint32_t h) { ++gAsks; gAskW = w; gAskH = h; XResizeWindow(gDpy, gWin, w, h); XFlush(gDpy); return true; }
void hostNop(const clap_host_t*) {}
std::atomic<bool> gCallback{false};   // the plug-in asked for on_main_thread (from its window thread): the main loop below runs it, as a host would
void hostCallback(const clap_host_t*) { gCallback = true; }
bool hostBool(const clap_host_t*) { return true; }
void hostClosed(const clap_host_t*, bool) {}
const clap_host_gui_t gHostGui = {hostNop, hostResize, hostBool, hostBool, hostClosed};
const void* hostExt(const clap_host_t*, const char* id) { return id && !std::strcmp(id, CLAP_EXT_GUI) ? &gHostGui : nullptr; }
clap_host_t gHost = {CLAP_VERSION_INIT, nullptr, "sw-x11-host", "SEVENTHWELL", "", "1", hostExt, hostNop, hostNop, hostCallback};

const clap_plugin_t* gPlugin = nullptr;
void pump(double seconds) {
    const int steps = static_cast<int>(seconds * 20);
    for (int i = 0; i < steps; ++i) {
        while (XPending(gDpy)) { XEvent e; XNextEvent(gDpy, &e); }
        if (gCallback.exchange(false) && gPlugin) gPlugin->on_main_thread(gPlugin);
        usleep(50000);
    }
}

struct Shot { unsigned w = 0, h = 0; std::vector<unsigned char> rgb; };
Shot grab() {
    Shot s; XWindowAttributes a; if (!XGetWindowAttributes(gDpy, gWin, &a)) return s;
    XImage* im = XGetImage(gDpy, gWin, 0, 0, static_cast<unsigned>(a.width), static_cast<unsigned>(a.height), AllPlanes, ZPixmap); if (!im) return s;
    s.w = static_cast<unsigned>(a.width); s.h = static_cast<unsigned>(a.height); s.rgb.resize(static_cast<size_t>(s.w) * s.h * 3);
    for (unsigned y = 0; y < s.h; ++y) for (unsigned x = 0; x < s.w; ++x) {
        const unsigned long p = XGetPixel(im, static_cast<int>(x), static_cast<int>(y)); unsigned char* d = &s.rgb[(static_cast<size_t>(y) * s.w + x) * 3];
        d[0] = static_cast<unsigned char>((p >> 16) & 255); d[1] = static_cast<unsigned char>((p >> 8) & 255); d[2] = static_cast<unsigned char>(p & 255);
    }
    XDestroyImage(im); return s;
}
// how much of the window is painted by the page: the share of pixels that are not the host's own grey (0x202020), and how many different colours (coarse) there are
void paint(const Shot& s, double& share, size_t& colours) {
    size_t other = 0; std::set<unsigned> c;
    for (size_t i = 0; i + 2 < s.rgb.size(); i += 3) { const unsigned r = s.rgb[i], g = s.rgb[i + 1], b = s.rgb[i + 2]; if (!(r == 0x20 && g == 0x20 && b == 0x20)) ++other; c.insert((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)); }
    share = s.rgb.empty() ? 0.0 : static_cast<double>(other) / static_cast<double>(s.rgb.size() / 3); colours = c.size();
}
void writePpm(const Shot& s, const std::string& file) { FILE* f = std::fopen(file.c_str(), "wb"); if (!f) return; std::fprintf(f, "P6\n%u %u\n255\n", s.w, s.h); std::fwrite(s.rgb.data(), 1, s.rgb.size(), f); std::fclose(f); }
// the design is dark (its bottom strip is nearly black); an area nobody painted is the host's grey or, as X leaves it after a resize, white. How much of a row of the window is dark:
double darkShare(const Shot& s, unsigned y) {
    if (y >= s.h || s.w == 0) return 0.0;
    size_t dark = 0;
    for (unsigned x = 0; x < s.w; ++x) { const unsigned char* p = &s.rgb[(static_cast<size_t>(y) * s.w + x) * 3]; if (p[0] < 0x1c && p[1] < 0x1c && p[2] < 0x1c) ++dark; }
    return static_cast<double>(dark) / s.w;
}
bool bottomPainted(const Shot& s) { return s.h >= 8 && darkShare(s, s.h - 3) > 0.8; }
// a point of the host window in screen coordinates (with a window manager the window has a title bar and a border: its place on the screen is not what XCreateWindow was told)
void screenPoint(int x, int y, int& sx, int& sy) { Window child; XTranslateCoordinates(gDpy, gWin, DefaultRootWindow(gDpy), x, y, &sx, &sy, &child); }
void xdo(const std::string& args) { const std::string c = "xdotool " + args + " >/dev/null 2>&1"; if (std::system(c.c_str()) != 0) std::printf("(xdotool failed: %s)\n", args.c_str()); }
int helperProcesses() { FILE* p = popen("pgrep -c -x sw-audio-gui 2>/dev/null", "r"); int n = 0; if (p) { if (std::fscanf(p, "%d", &n) != 1) n = 0; pclose(p); } return n; }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: sw-x11-host <plug-in .clap> --chip x y --knob x y [--ppm prefix]\n"); return 2; }
    const std::string path = argv[1]; int chipX = -1, chipY = -1, knobX = -1, knobY = -1; int pre[6] = {-1, -1, -1, -1, -1, -1}; bool killHelper = false, parentFirst = false; std::string ppm;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--chip") && i + 2 < argc) { chipX = std::atoi(argv[i + 1]); chipY = std::atoi(argv[i + 2]); i += 2; }
        else if (!std::strcmp(argv[i], "--knob") && i + 2 < argc) { knobX = std::atoi(argv[i + 1]); knobY = std::atoi(argv[i + 2]); i += 2; }
        else if (!std::strcmp(argv[i], "--preset") && i + 6 < argc) { for (int k = 0; k < 6; ++k) pre[k] = std::atoi(argv[i + 1 + k]); i += 6; }
        else if (!std::strcmp(argv[i], "--kill")) killHelper = true;
        else if (!std::strcmp(argv[i], "--parent-first")) parentFirst = true;
        else if (!std::strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
    }
    char home[] = "/tmp/sw_x11_host_XXXXXX"; if (!mkdtemp(home)) { std::perror("mkdtemp"); return 2; }
    setenv("HOME", home, 1);   // a folder of its own: the remembered zoom (Documents/SW AUDIO/zoom.txt) and the web engine's files do not carry over from a run to the next
    gDpy = XOpenDisplay(nullptr); if (!gDpy) { std::fprintf(stderr, "no display (run under xvfb-run)\n"); return 2; }
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry")); if (!entry || !entry->init(path.c_str())) { std::fprintf(stderr, "no clap_entry\n"); return 2; }
    const auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* d = fac->get_plugin_descriptor(fac, 0);
    const clap_plugin_t* p = fac->create_plugin(fac, &gHost, d->id);
    if (!p || !p->init(p)) { std::fprintf(stderr, "init failed\n"); return 2; }
    gPlugin = p;
    const auto* gui = static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
    const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    if (!gui) { fail("the plug-in has no gui extension (the helper sw-audio-gui must be next to the .clap)"); return 1; }
    if (!gui->is_api_supported(p, CLAP_WINDOW_API_X11, false)) fail("the x11 api is not supported");
    if (!gui->create(p, CLAP_WINDOW_API_X11, false)) { fail("gui create"); return 1; }
    gui->set_scale(p, 1.0);
    uint32_t w0 = 0, h0 = 0; gui->get_size(p, &w0, &h0);
    gWin = XCreateSimpleWindow(gDpy, DefaultRootWindow(gDpy), 20, 20, w0, h0, 0, 0, 0x202020);
    XSelectInput(gDpy, gWin, StructureNotifyMask | SubstructureNotifyMask); XMapWindow(gDpy, gWin); XFlush(gDpy);
    clap_window_t cw{}; cw.api = CLAP_WINDOW_API_X11; cw.x11 = gWin;
    if (!gui->set_parent(p, &cw)) { fail("gui set_parent"); return 1; }
    gui->show(p);
    std::printf("window %u x %u\n", w0, h0);

    // 1. painted
    double share = 0; size_t colours = 0; Shot s;
    for (int i = 0; i < 80; ++i) { pump(0.25); s = grab(); paint(s, share, colours); if (share > 0.5 && colours > 40) break; }
    if (share > 0.5 && colours > 40) pass("the window is painted (" + std::to_string(static_cast<int>(share * 100)) + " % of it, " + std::to_string(colours) + " colours)"); else fail("the window is not painted by the page (" + std::to_string(static_cast<int>(share * 100)) + " %, " + std::to_string(colours) + " colours)");
    if (!ppm.empty()) writePpm(s, ppm + "_1.ppm");
    if (helperProcesses() != 1) fail("expected one helper process, found " + std::to_string(helperProcesses()));
    else pass("one helper process is running");

    // hide and show again: the window empties (the host's own grey) and the page comes back
    gui->hide(p);
    for (int i = 0; i < 20; ++i) { pump(0.25); s = grab(); paint(s, share, colours); if (share < 0.2) break; }
    if (share < 0.2) pass("hide: the window is empty"); else fail("hide: the page is still on screen (" + std::to_string(static_cast<int>(share * 100)) + " %)");
    gui->show(p);
    for (int i = 0; i < 40; ++i) { pump(0.25); s = grab(); paint(s, share, colours); if (share > 0.5 && colours > 40) break; }
    if (share > 0.5 && colours > 40) pass("show: the page is back"); else fail("show: the page did not come back (" + std::to_string(static_cast<int>(share * 100)) + " %)");

    // 3 (first: the page is at its natural size): a drag on a knob changes a parameter
    if (knobX >= 0 && params) {
        const uint32_t n = params->count(p); std::vector<double> before(n), after(n);
        for (uint32_t i = 0; i < n; ++i) { clap_param_info_t inf{}; params->get_info(p, i, &inf); double v = 0; params->get_value(p, inf.id, &v); before[i] = v; }
        int sx, sy; screenPoint(knobX, knobY, sx, sy); char cmd[200]; std::snprintf(cmd, sizeof cmd, "mousemove %d %d mousedown 1 mousemove_relative --sync 0 20 mousemove_relative --sync 0 20 mousemove_relative --sync 0 20 mouseup 1", sx, sy);
        xdo(cmd); pump(1.0);
        int changed = 0; for (uint32_t i = 0; i < n; ++i) { clap_param_info_t inf{}; params->get_info(p, i, &inf); double v = 0; params->get_value(p, inf.id, &v); after[i] = v; if (std::fabs(after[i] - before[i]) > 1e-9) ++changed; }
        if (changed >= 1) pass("a drag on the knob moved " + std::to_string(changed) + " parameter(s)"); else fail("a drag on the knob changed nothing (the pointer, the page or the message path)");
    }

    // the keyboard: a preset is named by typing (the page has a text field in the preset menu); the file appears in the (temporary) Documents folder
    if (pre[0] >= 0) {
        char cmd[320]; int sx, sy;
        screenPoint(pre[0], pre[1], sx, sy); std::snprintf(cmd, sizeof cmd, "mousemove %d %d click 1", sx, sy); xdo(cmd); pump(0.8);
        screenPoint(pre[2], pre[3], sx, sy); std::snprintf(cmd, sizeof cmd, "mousemove %d %d click 1", sx, sy); xdo(cmd); pump(0.5);
        xdo("type --delay 60 xkeytest"); pump(0.5);
        screenPoint(pre[4], pre[5], sx, sy); std::snprintf(cmd, sizeof cmd, "mousemove %d %d click 1", sx, sy); xdo(cmd);
        const std::string find = std::string("find '") + home + "/Documents/SW AUDIO/Presets' -name 'xkeytest.swpreset' 2>/dev/null | grep -q .";
        bool found = false; for (int i = 0; i < 30 && !found; ++i) { pump(0.2); found = std::system(find.c_str()) == 0; }
        if (found) pass("typing a preset name and pressing Save wrote the preset file (the keyboard reaches the page)"); else fail("the preset file did not appear (the keyboard does not reach the page, or the click missed)");
    }

    // 2. the zoom chip: the plug-in asks this host for a bigger window, the page fills it
    if (chipX >= 0) {
        gAsks = 0; int sx, sy; screenPoint(chipX, chipY, sx, sy); char cmd[120]; std::snprintf(cmd, sizeof cmd, "mousemove %d %d click 1", sx, sy); xdo(cmd);
        for (int i = 0; i < 40 && gAsks == 0; ++i) pump(0.25);
        if (gAsks >= 1 && gAskW > w0 && gAskH > h0) pass("the chip asked the host for " + std::to_string(gAskW) + " x " + std::to_string(gAskH) + " (was " + std::to_string(w0) + " x " + std::to_string(h0) + ")"); else fail("the chip did not ask for a bigger window (" + std::to_string(gAsks) + " requests)");
        for (int i = 0; i < 40; ++i) { pump(0.25); s = grab(); if (bottomPainted(s) && s.w == gAskW) break; }   // (WebKit repaints a moment after the window grew)
        paint(s, share, colours);
        if (s.w == gAskW && s.h == gAskH) pass("the host window is " + std::to_string(s.w) + " x " + std::to_string(s.h)); else fail("the host window is " + std::to_string(s.w) + " x " + std::to_string(s.h));
        if (bottomPainted(s) && share > 0.5) pass("the page fills the bigger window (the bottom edge is painted)"); else fail("the page does not fill the bigger window");
        if (!ppm.empty()) writePpm(s, ppm + "_2.ppm");
        uint32_t w1 = 0, h1 = 0; gui->get_size(p, &w1, &h1);
        if (w1 == gAskW && h1 == gAskH) pass("get_size agrees with what was asked"); else fail("get_size is " + std::to_string(w1) + " x " + std::to_string(h1));
    }

    // the helper is killed under the window (a crash of the web engine): the plug-in and the host carry on, and the window can still be closed
    if (killHelper) {
        if (std::system("pkill -9 -x sw-audio-gui") != 0) fail("could not kill the helper");
        pump(1.0);
        uint32_t kw = 0, kh = 0; gui->get_size(p, &kw, &kh); gui->set_size(p, kw, kh); gui->hide(p); gui->show(p);   // (calls on a window whose helper is gone must not crash or hang)
        pass("the plug-in carries on after the helper was killed");
    }

    // some hosts destroy their window before they tell the plug-in: the helper's window goes with it and the helper ends; destroy afterwards is still fine
    if (parentFirst) { XDestroyWindow(gDpy, gWin); XSync(gDpy, False); pump(1.0); pass("the host window was destroyed first"); gWin = 0; }

    // 5. closing the window ends the helper
    gui->hide(p); gui->destroy(p);
    for (int i = 0; i < 40 && helperProcesses() != 0; ++i) usleep(100000);
    if (helperProcesses() == 0) pass("the helper process ended with the window"); else fail("the helper process is still running after destroy");
    p->destroy(p); entry->deinit(); if (gWin) XDestroyWindow(gDpy, gWin); XCloseDisplay(gDpy);
    { const std::string rm = std::string("rm -rf ") + home; if (std::system(rm.c_str()) != 0) {} }
    std::printf("%s\n", gFails ? "FAILURES" : "all passed");
    return gFails ? 1 : 0;
}
