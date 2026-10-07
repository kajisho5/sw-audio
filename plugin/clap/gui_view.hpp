// The platform side of the plug-in window: a web view inside a native parent window. Implemented in gui_mac.mm (WKWebView), gui_win.cpp (WebView2) and gui_none.cpp (Linux: no window yet).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sw::gui {

class View {
public:
    virtual ~View() = default;
    virtual bool setParent(void* nativeHandle) = 0;     // NSView* or HWND
    virtual void setSize(uint32_t width, uint32_t height) = 0;   // in the unit of the platform's window API
    virtual void setVisible(bool visible) = 0;
    virtual void eval(const std::string& script) = 0;   // run a script in the page (UI thread)
};

// "cocoa", "win32" or nullptr when the platform has no view yet
const char* platformApi();
// onMessage(text) is called on the UI thread for every message the page posts; its result, when not empty, is evaluated in the page
std::unique_ptr<View> createView(const std::string& html, std::function<std::string(const std::string&)> onMessage, double scale);

}  // namespace sw::gui
