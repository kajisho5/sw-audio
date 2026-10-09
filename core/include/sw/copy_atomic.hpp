// SW AUDIO core — a std::atomic that can be copied (a Processor is returned by value in tests and moved around before it runs; the copy is not atomic, which is fine
// because nothing else touches the object yet). Used for the few values a core shares between the audio thread and the screen's thread (Measure / Auto align state and results).
#pragma once
#include <atomic>

namespace sw {

template <class T>
struct CopyAtomic {
    std::atomic<T> v;
    CopyAtomic(T x = T()) : v(x) {}
    CopyAtomic(const CopyAtomic& o) : v(o.v.load()) {}
    CopyAtomic& operator=(const CopyAtomic& o) { v.store(o.v.load()); return *this; }
    CopyAtomic& operator=(T x) { v.store(x); return *this; }
    operator T() const { return v.load(); }
    T load() const { return v.load(); }
    void store(T x) { v.store(x); }
};

}  // namespace sw
