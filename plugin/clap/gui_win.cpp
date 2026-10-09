// Windows: the plug-in window is a child window of the host's parent HWND that carries a WebView2 (Edge) control.
#include "gui_view.hpp"
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <atomic>
#include <memory>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace sw::gui {
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n); return w;
}
std::string narrow(const wchar_t* w) {
    if (!w) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(static_cast<size_t>(n - 1), '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr); return s;
}
const wchar_t* kClass = L"SWAudioGuiHost";
void registerClass() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DefWindowProcW; wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase); wc.lpszClassName = kClass; wc.hbrBackground = CreateSolidBrush(RGB(12, 12, 13));
    RegisterClassExW(&wc);
}

class WinView : public View {
public:
    WinView(const std::string& html, std::function<std::string(const std::string&)> onMessage) : html_(widen(html)), onMessage_(std::move(onMessage)) {}
    ~WinView() override {
        alive_->store(false);
        if (controller_) controller_->Close();
        webview_.Reset(); controller_.Reset();
        if (hwnd_) DestroyWindow(hwnd_);
    }
    bool setParent(void* h) override {
        parent_ = static_cast<HWND>(h); if (!parent_) return false;
        registerClass();
        hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, static_cast<int>(w_), static_cast<int>(h_), parent_, nullptr, reinterpret_cast<HINSTANCE>(&__ImageBase), nullptr);
        if (!hwnd_) return false;
        start();
        return true;
    }
    void setSize(uint32_t w, uint32_t h) override { w_ = w; h_ = h; if (hwnd_) SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h), SWP_NOZORDER | SWP_NOACTIVATE); fit(); }
    void setVisible(bool v) override { visible_ = v; if (hwnd_) ShowWindow(hwnd_, v ? SW_SHOW : SW_HIDE); if (controller_) controller_->put_IsVisible(v ? TRUE : FALSE); }
    void eval(const std::string& s) override { if (webview_) webview_->ExecuteScript(widen(s).c_str(), nullptr); }
private:
    void fit() { if (controller_) { RECT r{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)}; controller_->put_Bounds(r); } }
    void start() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // S_FALSE / RPC_E_CHANGED_MODE: the host already did it
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); std::wstring data = std::wstring(tmp) + L"SWAudioWebView2";
        auto alive = alive_;
        const HRESULT started = CreateCoreWebView2EnvironmentWithOptions(nullptr, data.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, alive](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (!alive->load()) return S_OK;
                if (FAILED(hr) || !env) { showMissing(); return S_OK; }
                env->CreateCoreWebView2Controller(hwnd_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this, alive](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
                    if (!alive->load()) return S_OK;
                    if (FAILED(hr2) || !c) { showMissing(); return S_OK; }
                    controller_ = c; c->get_CoreWebView2(&webview_);
                    ComPtr<ICoreWebView2Settings> st; if (SUCCEEDED(webview_->get_Settings(&st)) && st) { st->put_AreDefaultContextMenusEnabled(FALSE); st->put_AreDevToolsEnabled(FALSE); st->put_IsStatusBarEnabled(FALSE); st->put_IsZoomControlEnabled(FALSE); }
                    EventRegistrationToken tok;
                    webview_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this, alive](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                        if (!alive->load()) return S_OK;
                        LPWSTR w = nullptr; if (SUCCEEDED(a->TryGetWebMessageAsString(&w)) && w) { const std::string reply = onMessage_(narrow(w)); CoTaskMemFree(w); if (!reply.empty()) eval(reply); }
                        return S_OK;
                    }).Get(), &tok);
                    fit(); c->put_IsVisible(visible_ ? TRUE : FALSE);
                    webview_->NavigateToString(html_.c_str());
                    return S_OK;
                }).Get());
                return S_OK;
            }).Get());
        if (FAILED(started)) showMissing();   // no WebView2 Runtime on this computer: the handler is never called
    }
    // without the WebView2 Runtime (Windows 10 without it, or a damaged one) the window says what is missing instead of staying empty
    void showMissing() {
        if (!hwnd_ || note_) return;
        note_ = CreateWindowExW(0, L"STATIC", L"This window needs the Microsoft Edge WebView2 Runtime.\r\nInstall it from Microsoft (search: WebView2 Runtime download), then open the window again.\r\nThe plug-in plays and the host's own controls work without it.",
                                WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 40, static_cast<int>(w_) - 40, 120, hwnd_, nullptr, reinterpret_cast<HINSTANCE>(&__ImageBase), nullptr);
    }
    std::wstring html_;
    std::function<std::string(const std::string&)> onMessage_;
    HWND parent_ = nullptr, hwnd_ = nullptr, note_ = nullptr; uint32_t w_ = 960, h_ = 550; bool visible_ = true;
    ComPtr<ICoreWebView2Controller> controller_; ComPtr<ICoreWebView2> webview_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
}  // namespace

const char* platformApi() { return "win32"; }
std::unique_ptr<View> createView(const std::string& html, std::function<std::string(const std::string&)> onMessage, double) { return std::make_unique<WinView>(html, std::move(onMessage)); }
}  // namespace sw::gui
