// SW AUDIO core — one background thread that runs a job when it is kicked (EQ08 / EQ02 Linear: the kernel design runs here, not in the audio thread; spec: 「カーネル再計算は別スレッド」).
// kick() is for the audio thread: it sets a flag and wakes the thread without taking a lock or allocating (a wake-up that falls between the thread's check and its wait is picked up by the
// 50 ms timeout, so a kick is never lost, at worst late). The job itself is a function the owner gives to start(); it runs on the worker's thread, one run per kick (kicks during a run make one more run).
// A copy of a BackgroundWork has no thread: the owner starts its own (the copy's job must point at the copy).
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// ThreadSanitizer of GCC 11 (the Ubuntu 22.04 runner of the CI) does not see pthread_cond_clockwait, which condition_variable::wait_for (steady clock) calls: the mutex that the wait gives up and takes again goes
// missing, and it reports races between accesses that are both under that one mutex (CI run 182: "Read ... (mutexes: write M288)" against "Previous write ... (mutexes: write M288)") and between a design
// and the write after waitIdle(). A build with the sanitizer therefore waits against the system clock (pthread_cond_timedwait, which every version sees); the product waits as before.
#if defined(__SANITIZE_THREAD__)
#define SW_WORKER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SW_WORKER_TSAN 1
#endif
#endif

namespace sw {

class BackgroundWork {
public:
    BackgroundWork() = default;
    BackgroundWork(const BackgroundWork&) {}
    BackgroundWork& operator=(const BackgroundWork&) { return *this; }
    ~BackgroundWork() { stop(); }

    // not on the audio thread: (re)starts the thread with `job`. False when the system gives no thread: the owner then does the work itself (running() is false: EQ08 / EQ02 design in process())
    bool start(std::function<void()> job) {
        stop();
        job_ = std::move(job); quit_.store(false); kicked_.store(false); busy_ = false;
        try { th_ = std::thread([this] { loop(); }); } catch (...) { return false; }
        return true;
    }
    // joins the thread (what was kicked and not yet run is dropped)
    void stop() {
        if (!th_.joinable()) return;
        quit_.store(true); cv_.notify_all(); th_.join();
        kicked_.store(false); busy_ = false;
    }
    bool running() const { return th_.joinable(); }
    // the audio thread: no lock, no allocation
    void kick() { kicked_.store(true); cv_.notify_one(); }
    // not on the audio thread: returns when nothing is kicked and no run is in progress (false: no thread, or it did not come to rest within `ms`)
    bool waitIdle(int ms = 5000) {
        if (!th_.joinable()) return true;
        std::unique_lock<std::mutex> lk(m_);
        return waitFor(idle_, lk, ms, [this] { return !kicked_.load() && !busy_; });
    }

private:
    template <class Pred> static bool waitFor(std::condition_variable& cv, std::unique_lock<std::mutex>& lk, int ms, Pred pred) {
#ifdef SW_WORKER_TSAN
        return cv.wait_until(lk, std::chrono::system_clock::now() + std::chrono::milliseconds(ms), pred);
#else
        return cv.wait_for(lk, std::chrono::milliseconds(ms), pred);
#endif
    }
    void loop() {
        std::unique_lock<std::mutex> lk(m_);
        for (;;) {
            waitFor(cv_, lk, 50, [this] { return kicked_.load() || quit_.load(); });
            if (quit_.load()) return;
            if (!kicked_.exchange(false)) continue;
            busy_ = true;
            lk.unlock(); job_(); lk.lock();
            busy_ = false; idle_.notify_all();
        }
    }
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_, idle_;
    std::atomic<bool> kicked_{false}, quit_{false};
    bool busy_ = false;
    std::function<void()> job_;
};

}  // namespace sw
