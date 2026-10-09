// A guard for "the audio thread does not allocate": the global operator new counts the allocations made on a thread while a Scope is alive on it (tests/test_alloc_hook.cpp)
#pragma once
#include <atomic>
namespace allocguard {
inline thread_local bool on = false;
inline std::atomic<long> count{0};
struct Scope {
    Scope() { count = 0; on = true; }
    ~Scope() { on = false; }
    long n() const { return count.load(); }
};
}  // namespace allocguard
