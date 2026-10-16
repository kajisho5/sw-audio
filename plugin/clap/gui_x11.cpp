// Linux: the plug-in window is a web view in a process of its own (sw-audio-gui, plugin/clap/gui_x11_helper.cpp: GTK + WebKitGTK, embedded into the X11 window the host gives).
// This side starts it, sends it the page and the scripts, and hands the page's messages to the plug-in. No toolkit is loaded into the host. The protocol: gui_x11_proto.hpp.
// Without the helper next to the plug-in (or in one of the fixed places), platformApi() is null and the host shows its own parameter list, as before.
#include "gui_view.hpp"
#include "gui_x11_proto.hpp"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace sw::gui {
namespace {
using namespace x11;

std::string selfPath() { Dl_info i{}; return dladdr(reinterpret_cast<void*>(&selfPath), &i) && i.dli_fname ? std::string(i.dli_fname) : std::string(); }

// the helper: the environment variable SW_AUDIO_GUI_HELPER (tests, a person who put it elsewhere), then next to this plug-in, then the fixed places. "" when there is none.
const std::string& helperPath() {
    static const std::string path = [] {
        const char* env = std::getenv("SW_AUDIO_GUI_HELPER");
        if (env && *env && access(env, X_OK) == 0) return std::string(env);
        for (const auto& c : helperCandidates(selfPath())) if (access(c.c_str(), X_OK) == 0) return c;
        return std::string();
    }();
    return path;
}

// the helper starts (its libraries - GTK 3, WebKitGTK - are installed on this machine)? Asked once per process, when the first window opens; a few milliseconds.
bool helperUsable() {
    static const bool usable = [] {
        const std::string& path = helperPath(); if (path.empty()) return false;
        char* argv[] = {const_cast<char*>(path.c_str()), const_cast<char*>("--check"), nullptr};
        pid_t pid = -1;
        if (posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv, environ) != 0) return false;
        for (int i = 0; i < 200; ++i) {   // 2 s at the most
            int st = 0; const pid_t r = waitpid(pid, &st, WNOHANG);
            if (r == pid) return WIFEXITED(st) && WEXITSTATUS(st) == 0;
            if (r < 0) return false;
            usleep(10000);
        }
        kill(pid, SIGKILL); int st; waitpid(pid, &st, 0); return false;
    }();
    return usable;
}

class X11View : public View {
public:
    X11View(std::string html, std::function<std::string(const std::string&)> onMessage) : html_(std::move(html)), onMessage_(std::move(onMessage)) {}
    ~X11View() override { stop(); }

    // the host's window (an X11 window id): the helper is started now, it embeds itself into that window
    bool setParent(void* h) override {
        const unsigned long xid = reinterpret_cast<uintptr_t>(h);
        if (!xid || started_) return false;
        // one socket pair for both directions (a write to a closed socket with MSG_NOSIGNAL is an error, not the SIGPIPE that would end the host). The helper gets its end as fd 3; the number must not be one
        // this process still needs, so the end is first moved out of the way (>= 10)
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) return false;
        const int hfd = fcntl(sv[1], F_DUPFD_CLOEXEC, 10);
        posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
        if (hfd >= 0) posix_spawn_file_actions_adddup2(&fa, hfd, 3);
        char parentArg[32], sizeArg[32]; std::snprintf(parentArg, sizeof parentArg, "%lu", xid); std::snprintf(sizeArg, sizeof sizeArg, "%ux%u", w_.load(), h_.load());
        const std::string path = helperPath();
        char* argv[] = {const_cast<char*>(path.c_str()), const_cast<char*>("--parent"), parentArg, const_cast<char*>("--size"), sizeArg, nullptr};
        pid_t pid = -1;
        const int rc = (hfd >= 0 && !path.empty()) ? posix_spawn(&pid, path.c_str(), &fa, nullptr, argv, environ) : -1;
        posix_spawn_file_actions_destroy(&fa);
        if (hfd >= 0) close(hfd);
        close(sv[1]);
        if (rc != 0) { close(sv[0]); return false; }
        pid_ = pid; fd_ = sv[0];
        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
        started_ = true;
        reader_ = std::thread([this] { run(); });
        return true;
    }
    void setSize(uint32_t w, uint32_t h) override { w_ = w; h_ = h; if (ready_) send('Z', std::to_string(w) + " " + std::to_string(h)); }
    void setVisible(bool v) override { visible_ = v; if (ready_) send('V', v ? "1" : "0"); }
    void eval(const std::string& script) override { if (ready_) send('S', script); }

private:
    // the reader thread: first the page, then the helper's messages (every message of the page is answered here, on this thread: the plug-in's session was built for a window thread of its own)
    void run() {
        const bool sent = [&] { const auto page = encode('H', html_); return writeAll(page.data(), page.size()); }();
        std::string().swap(html_);   // the page is several MB: the helper has it now
        if (sent) {
            ready_ = true;
            send('Z', std::to_string(w_.load()) + " " + std::to_string(h_.load()));
            if (!visible_.load()) send('V', "0");
        }
        FrameReader r; uint8_t buf[65536];
        while (!stop_.load()) {
            pollfd p{fd_, POLLIN, 0};
            const int n = poll(&p, 1, 100);
            if (n < 0 && errno != EINTR) break;
            if (n <= 0) continue;
            const ssize_t got = read(fd_, buf, sizeof buf);
            if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (got <= 0) break;   // the helper is gone (closed its window, crashed, was killed): the window stays empty, the plug-in carries on
            r.feed(buf, static_cast<size_t>(got));
            Frame f;
            while (r.next(f)) {
                if (f.type == 'M' && onMessage_) { const std::string reply = onMessage_(f.payload); if (!reply.empty() && !stop_.load()) send('S', reply); }
            }
            if (r.bad()) break;
        }
        ready_ = false;
    }
    void send(char type, const std::string& payload) { const auto f = encode(type, payload); writeAll(f.data(), f.size()); }
    // everything or nothing within a few seconds: a helper that does not read must not freeze the host
    bool writeAll(const uint8_t* data, size_t n) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (fd_ < 0) return false;
        size_t off = 0;
        while (off < n) {
            const ssize_t w = ::send(fd_, data + off, n - off, MSG_NOSIGNAL);
            if (w >= 0) { off += static_cast<size_t>(w); continue; }
            if (errno == EINTR) continue;
            if (errno != EAGAIN) return false;
            pollfd p{fd_, POLLOUT, 0};
            if (poll(&p, 1, 5000) <= 0) return false;
        }
        return true;
    }
    void stop() {
        if (!started_) return;
        stop_ = true;
        if (ready_) send('Q', "");
        { std::lock_guard<std::mutex> lock(writeMutex_); if (fd_ >= 0) shutdown(fd_, SHUT_RDWR); }   // the helper also quits on the end of the connection; the reader thread wakes with it
        // it leaves within a moment; if it does not, it is killed
        for (int i = 0; i < 50; ++i) { int st; if (waitpid(pid_, &st, WNOHANG) != 0) { pid_ = -1; break; } usleep(20000); }
        if (pid_ > 0) { kill(pid_, SIGKILL); int st; waitpid(pid_, &st, 0); pid_ = -1; }
        if (reader_.joinable()) reader_.join();
        { std::lock_guard<std::mutex> lock(writeMutex_); if (fd_ >= 0) { close(fd_); fd_ = -1; } }
        started_ = false;
    }

    std::string html_;
    std::function<std::string(const std::string&)> onMessage_;
    std::atomic<unsigned> w_{960}, h_{550};
    std::atomic<bool> visible_{true}, ready_{false}, stop_{false};
    bool started_ = false;
    pid_t pid_ = -1; int fd_ = -1;
    std::mutex writeMutex_;
    std::thread reader_;
};
}  // namespace

const char* platformApi() { return helperPath().empty() ? nullptr : "x11"; }
std::unique_ptr<View> createView(const std::string& html, std::function<std::string(const std::string&)> onMessage, double) {
    if (helperPath().empty() || !helperUsable()) return nullptr;   // (the host then has no window to show: it falls back on its own parameter list)
    return std::make_unique<X11View>(html, std::move(onMessage));
}
}  // namespace sw::gui
