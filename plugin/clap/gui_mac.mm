// macOS: the plug-in window is a WKWebView (WebKit) added to the parent NSView the host gives.
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "gui_view.hpp"

@interface SWMessageHandler : NSObject <WKScriptMessageHandler>
@property(nonatomic, assign) std::function<void(const std::string&)>* callback;
@end
@implementation SWMessageHandler
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
    if (!self.callback || ![m.body isKindOfClass:[NSString class]]) return;
    (*self.callback)(std::string([(NSString*)m.body UTF8String]));
}
@end

// <input type="file"> in a WKWebView does nothing until the app answers the open panel (the instruments' licence file, for example):
// a sheet on the host's window when there is one, else a panel of its own
@interface SWUIDelegate : NSObject <WKUIDelegate>
@end
@implementation SWUIDelegate
- (void)webView:(WKWebView*)webView runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters initiatedByFrame:(WKFrameInfo*)frame
    completionHandler:(void (^)(NSArray<NSURL*>* _Nullable URLs))completionHandler {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    void (^done)(NSModalResponse) = ^(NSModalResponse r) { completionHandler(r == NSModalResponseOK ? panel.URLs : nil); };
    if (webView.window) [panel beginSheetModalForWindow:webView.window completionHandler:done];
    else [panel beginWithCompletionHandler:done];
}
@end

namespace sw::gui {
namespace {
class MacView : public View {
public:
    MacView(const std::string& html, std::function<std::string(const std::string&)> onMessage) : onMessage_(std::move(onMessage)) {
        callback_ = [this](const std::string& m) {
            const std::string script = onMessage_(m);
            if (!script.empty()) eval(script);
        };
        handler_ = [[SWMessageHandler alloc] init]; handler_.callback = &callback_;
        WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
        [cfg.userContentController addScriptMessageHandler:handler_ name:@"sw"];
        container_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 960, 550)];
        web_ = [[WKWebView alloc] initWithFrame:container_.bounds configuration:cfg];
        ui_ = [[SWUIDelegate alloc] init];
        web_.UIDelegate = ui_;   // a weak reference in WebKit: kept alive by ui_
        web_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [web_ setValue:@NO forKey:@"drawsBackground"];
        [container_ addSubview:web_];
        [web_ loadHTMLString:[NSString stringWithUTF8String:html.c_str()] baseURL:nil];
    }
    ~MacView() override {
        [web_.configuration.userContentController removeScriptMessageHandlerForName:@"sw"];
        handler_.callback = nullptr;
        web_.UIDelegate = nil;
        [container_ removeFromSuperview];
    }
    bool setParent(void* h) override { NSView* parent = (__bridge NSView*)h; if (!parent) return false; [parent addSubview:container_]; container_.frame = NSMakeRect(0, 0, container_.frame.size.width, container_.frame.size.height); return true; }
    void setSize(uint32_t w, uint32_t h) override { container_.frame = NSMakeRect(0, 0, w, h); }
    void setVisible(bool v) override { container_.hidden = !v; }
    void eval(const std::string& s) override { [web_ evaluateJavaScript:[NSString stringWithUTF8String:s.c_str()] completionHandler:nil]; }
private:
    std::function<std::string(const std::string&)> onMessage_;
    std::function<void(const std::string&)> callback_;
    SWMessageHandler* handler_ = nil;
    SWUIDelegate* ui_ = nil;
    NSView* container_ = nil;
    WKWebView* web_ = nil;
};
}  // namespace

const char* platformApi() { return "cocoa"; }
std::unique_ptr<View> createView(const std::string& html, std::function<std::string(const std::string&)> onMessage, double) { return std::make_unique<MacView>(html, std::move(onMessage)); }
}  // namespace sw::gui
