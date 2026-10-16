// Linux: the plug-in window's web view, in a process of its own (sw-audio-gui). One per open window; started by the plug-in (gui_x11.cpp) with a socket on fd 3 (frames both ways, the protocol is in
// gui_x11_proto.hpp). Why a process and not a library in the host: a DAW already has a toolkit loaded (GTK 2 or 3, Qt, its own), and two toolkits in one process crash each other; a web engine that dies
// must not take the session with it. The window is a GtkPlug (X11 embedding) into the window the host gives (CLAP gui "x11").
//   usage: sw-audio-gui --parent <X window id> [--size <w>x<h>]      embedded (what the plug-in does)
//          sw-audio-gui --standalone [--size <w>x<h>]                a window of its own (tests; the screenshot of a page)
//          sw-audio-gui --check                                      exits 0 when it started (GTK and WebKitGTK are installed)
#include "gui_x11_proto.hpp"
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>
#include <glib-unix.h>
#include <webkit2/webkit2.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace sw::gui::x11;

namespace {
constexpr int kIn = 3, kOut = 3;   // one socket, both directions
std::mutex gWriteMutex;
GtkWidget* gWindow = nullptr; WebKitWebView* gView = nullptr;
bool gPlug = false; unsigned gW = 960, gH = 550;

void sendFrame(char type, const std::string& payload) {
    const auto f = encode(type, payload);
    std::lock_guard<std::mutex> lock(gWriteMutex);
    size_t off = 0;
    while (off < f.size()) {
        const ssize_t n = write(kOut, f.data() + off, f.size() - off);
        if (n < 0) { if (errno == EINTR) continue; return; }   // the plug-in is gone: the reader thread notices on its side and quits
        off += static_cast<size_t>(n);
    }
}

void runScript(const std::string& script) {
#if defined(WEBKIT_CHECK_VERSION)
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(gView, script.c_str(), static_cast<gssize>(script.size()), nullptr, nullptr, nullptr, nullptr, nullptr);
    return;
#endif
#endif
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    webkit_web_view_run_javascript(gView, script.c_str(), nullptr, nullptr, nullptr);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

void applySize() {
    gtk_widget_set_size_request(GTK_WIDGET(gView), static_cast<int>(gW), static_cast<int>(gH));
    gtk_window_resize(GTK_WINDOW(gWindow), static_cast<int>(gW), static_cast<int>(gH));
}

// a frame from the plug-in, on the GTK thread
gboolean handleFrame(gpointer data) {
    std::unique_ptr<Frame> f(static_cast<Frame*>(data));
    switch (f->type) {
    case 'H': webkit_web_view_load_html(gView, f->payload.c_str(), nullptr); break;
    case 'S': runScript(f->payload); break;
    case 'Z': { unsigned w, h; if (parseSize(f->payload, w, h)) { gW = w; gH = h; applySize(); } break; }
    case 'V':
        if (f->payload == "1") { gtk_widget_show_all(gWindow); if (gPlug) gdk_window_show(gtk_widget_get_window(gWindow)); }   // (a plug is mapped by hand, see main())
        else gtk_widget_hide(gWindow);
        break;
    case 'Q': gtk_main_quit(); break;
    default: break;
    }
    return G_SOURCE_REMOVE;
}

void readerThread() {
    FrameReader r; uint8_t buf[65536];
    for (;;) {
        const ssize_t n = read(kIn, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;                       // the plug-in closed its end (it went away, the host may have died): this window has no purpose any more
        r.feed(buf, static_cast<size_t>(n));
        Frame f;
        while (r.next(f)) g_idle_add(handleFrame, new Frame(f));
        if (r.bad()) break;
    }
    g_idle_add([](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);
}

void onMessage(WebKitUserContentManager*, WebKitJavascriptResult* res, gpointer) {
    JSCValue* v = webkit_javascript_result_get_js_value(res);
    if (!jsc_value_is_string(v)) return;
    char* s = jsc_value_to_string(v);
    if (s) { sendFrame('M', s); g_free(s); }
}

void onLoadChanged(WebKitWebView*, WebKitLoadEvent ev, gpointer) { if (ev == WEBKIT_LOAD_FINISHED) sendFrame('R', ""); }
gboolean onContextMenu(WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) { return TRUE; }   // no browser menu in a plug-in window
gboolean onLoadFailed(WebKitWebView*, WebKitLoadEvent, const char* uri, GError* e, gpointer) { sendFrame('E', std::string("load failed: ") + (e ? e->message : "?") + " " + (uri ? uri : "")); return FALSE; }
void onRealize(GtkWidget* w, gpointer) {
    GdkWindow* gw = gtk_widget_get_window(w); if (!gw) return;
    sendFrame('P', std::to_string(static_cast<unsigned long>(gPlug ? gtk_plug_get_id(GTK_PLUG(w)) : GDK_WINDOW_XID(gw))));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "--check")) return 0;   // the plug-in asks once whether the helper can start at all (its libraries are found): the loader has done its part by now
    unsigned long parent = 0; bool standalone = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--parent") && i + 1 < argc) parent = std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--standalone")) standalone = true;
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) { unsigned w, h; if (std::sscanf(argv[++i], "%ux%u", &w, &h) == 2 && w >= 16 && h >= 16 && w <= 16384 && h <= 16384) { gW = w; gH = h; } }
    }
    if (!parent && !standalone) { std::fprintf(stderr, "sw-audio-gui: --parent <X window id> or --standalone\n"); return 2; }
    std::signal(SIGPIPE, SIG_IGN);
    setenv("GDK_BACKEND", "x11", 1);   // the host gives an X11 window: not Wayland
    setenv("GDK_SCALE", "1", 1);       // the size of the window is in pixels, the page fits itself to it (ui/sw-ui.js "fit"): no second scaling by GTK
    if (!gtk_init_check(&argc, &argv)) { std::fprintf(stderr, "sw-audio-gui: no display\n"); return 3; }
    g_unix_signal_add(SIGTERM, [](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);

    gPlug = parent != 0;
    gWindow = gPlug ? gtk_plug_new(static_cast<::Window>(parent)) : gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(gWindow), static_cast<int>(gW), static_cast<int>(gH));
    g_signal_connect(gWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(gWindow, "realize", G_CALLBACK(onRealize), nullptr);

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    g_signal_connect(ucm, "script-message-received::sw", G_CALLBACK(onMessage), nullptr);
    webkit_user_content_manager_register_script_message_handler(ucm, "sw");   // the page posts with window.webkit.messageHandlers.sw.postMessage(text), the same as on macOS
    gView = WEBKIT_WEB_VIEW(webkit_web_view_new_with_user_content_manager(ucm));
    WebKitSettings* st = webkit_web_view_get_settings(gView);
    webkit_settings_set_enable_developer_extras(st, FALSE);
    webkit_settings_set_enable_write_console_messages_to_stdout(st, FALSE);
    webkit_settings_set_enable_back_forward_navigation_gestures(st, FALSE);
    GdkRGBA bg = {12.0 / 255, 12.0 / 255, 13.0 / 255, 1.0}; webkit_web_view_set_background_color(gView, &bg);
    g_signal_connect(gView, "load-changed", G_CALLBACK(onLoadChanged), nullptr);
    g_signal_connect(gView, "load-failed", G_CALLBACK(onLoadFailed), nullptr);
    g_signal_connect(gView, "context-menu", G_CALLBACK(onContextMenu), nullptr);
    gtk_container_add(GTK_CONTAINER(gWindow), GTK_WIDGET(gView));
    applySize();
    gtk_widget_show_all(gWindow);
    // XEmbed: a plug waits for the embedder to map it, but the window the host gives is a plain window that does not speak XEmbed: map it ourselves
    if (gPlug) { gtk_widget_realize(gWindow); gdk_window_show(gtk_widget_get_window(gWindow)); }

    std::thread(readerThread).detach();
    gtk_main();
    return 0;
}
