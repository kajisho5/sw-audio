// SW Link: the instances of all SW AUDIO plug-ins inside one host process know about each other (spec: common functions, "SW Link"; here the first part: who is there and what they play).
//
//   * One registry per process, shared by every plug-in binary (each product is its own .clap / .vst3 / .component, so a static would be one copy per product). The first plug-in to join allocates
//     it straight from the operating system (VirtualAlloc / mmap: never freed, so it outlives the binary that made it) and writes its address into the environment variable SW_AUDIO_LINK as
//     "<process id>:<hex address>". The others read it back; the process id keeps a copy of the variable that a child process inherited from being trusted (the address means nothing there).
//     A host that runs each plug-in in a process of its own has one registry per plug-in, so the plug-ins do not see each other: peers() is 0 there, which the screen shows as "SW Link off".
//   * An instance joins in init() (main thread) with a view of its output ring (gui_spectrum.hpp: RingView): the samples, the write position, the size. It claims one slot by compare-and-swap and
//     publishes the pointers last. Nothing is copied on the audio thread: the instance only writes its ring, as it does for its own screen.
//   * Others read the ring from their GUI thread (peers(), others()). To make that safe against the owner going away, a reader first counts itself in the slot (readers), then looks at the pointers; the owner
//     withdraws the pointers (leave()) and waits until no reader is inside before the ring is destroyed. A slot that changed owner in between is noticed by its id, and the read is dropped.
//   * A product that takes another instance's signal as a key (LV05 Auto ducker: "Key = the SW Link instance LV01 Voice") reads the latest samples of that instance's output ring on its own audio thread
//     (readKey: atomics only, the reader count guards against the owner going away). The ring holds what the other instance has played so far: the key is that instance's last block when it has
//     already been processed in this cycle, or the one before (at most a block late: a ducker's attack and hold are far longer). An instance whose ring has stopped moving is no key.
//   * A setting shared by the instances of one product (MT05: the 0 VU reference of the session): the instance whose setting the person changed publishes the value with a stamp from the registry's
//     counter (publishShared); the others of that product take the newest value they have not seen (adoptShared) and write it to their own host parameter. An instance that adopts does not publish,
//     so nothing bounces; the last change wins. An instance that joins late takes what is already there.
//   * "Alive": a slot whose write position has not moved for `timeout` (1.5 s) is an instance that is not processing (stopped, bypassed by the host), and does not count.
//   * A product that has a reference spectrum to share (UT03: the long-term spectrum of its reference, 60 bands of 1/6 octave, sw/band_spectrum.hpp) publishes it in its slot (publishReference:
//     only atomic stores, so the audio thread may do it after a block): the values first, then a serial that is not 0; a reader (findReference: EQ05 Match) reads the serial, the values and the serial again and drops the read when it changed.
//   * Layout: magic, version and sizes are checked; a build of another layout cannot join and sees no peers (the others do not see it either).
//   Thread rules: join()/leave() on the main thread, peers()/others() on one GUI thread per Member (they keep state), never on the audio thread.
#pragma once
#include "gui_spectrum.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
extern "C" {   // declared here instead of including windows.h (this header is included everywhere): the same prototypes as <windows.h>, so both can be seen in one file
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char*, char*, unsigned long);
__declspec(dllimport) int __stdcall SetEnvironmentVariableA(const char*, const char*);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned long long, unsigned long, unsigned long);
}
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sw::link {

constexpr uint32_t kMagic = 0x4b4c5753u, kVersion = 3;   // "SWLK"
constexpr int kSlots = 128;
constexpr int kRefBands = 60;                           // the reference spectrum a product may publish (= sw::BandSpectrum::kBands)
constexpr const char* kEnv = "SW_AUDIO_LINK";

struct Slot {
    std::atomic<uint64_t> id{0};                                   // 0 = free; the owner's random number otherwise
    std::atomic<uint32_t> readers{0};                              // how many other instances are reading this slot's ring right now
    std::atomic<const std::atomic<float>*> data{nullptr};          // the owner's output ring (the last thing published, the first thing withdrawn)
    std::atomic<const std::atomic<size_t>*> head{nullptr};
    std::atomic<size_t> mask{0};
    std::atomic<double> sampleRate{48000.0};
    std::atomic<uint32_t> product{0};                              // the product code, four characters packed (EQ02 -> 'E','Q','0','2')
    std::atomic<uint32_t> refSerial{0};                            // the shared reference spectrum: 0 = none, otherwise which one it is (changes with every new reference); written last, withdrawn first
    std::atomic<float> refDb[kRefBands];                           // (dB, 1/6 octave from 20 Hz)
    std::atomic<uint32_t> sharedStamp{0};                          // the setting this instance's person changed last (0: none yet): when (a stamp of the registry's counter) ...
    std::atomic<double> sharedValue{0.0};                          // ... and its value; written value first, stamp last
};
struct Registry { uint32_t magic = kMagic, version = kVersion, slots = kSlots, slotSize = sizeof(Slot); std::atomic<uint32_t> stamp{0}; Slot s[kSlots]; };

inline bool valid(const Registry* r) { return r && r->magic == kMagic && r->version == kVersion && r->slots == static_cast<uint32_t>(kSlots) && r->slotSize == sizeof(Slot); }

namespace detail {
inline unsigned long processId() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}
inline bool getEnv(const char* name, char* out, size_t n) {
#if defined(_WIN32)
    const unsigned long r = GetEnvironmentVariableA(name, out, static_cast<unsigned long>(n)); return r > 0 && r < n;
#else
    const char* v = std::getenv(name); if (!v || std::strlen(v) >= n) return false; std::strcpy(out, v); return true;
#endif
}
inline void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    SetEnvironmentVariableA(name, value);
#else
    ::setenv(name, value, 1);
#endif
}
// memory that belongs to the process, not to the plug-in that asked for it (zeroed)
inline void* processMemory(size_t n) {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, n, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 0x04 /* PAGE_READWRITE */);
#else
    void* p = ::mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); return p == MAP_FAILED ? nullptr : p;
#endif
}
inline Registry* fromEnv() {
    char buf[64]; if (!getEnv(kEnv, buf, sizeof buf)) return nullptr;
    unsigned long pid = 0; unsigned long long addr = 0;
    if (std::sscanf(buf, "%lu:%llx", &pid, &addr) != 2 || pid != processId() || !addr) return nullptr;
    return reinterpret_cast<Registry*>(static_cast<uintptr_t>(addr));
}
inline Registry* open() {
    if (Registry* r = fromEnv()) return valid(r) ? r : nullptr;
    void* mem = processMemory(sizeof(Registry)); if (!mem) return nullptr;
    Registry* mine = new (mem) Registry();
    char buf[64]; std::snprintf(buf, sizeof buf, "%lu:%llx", processId(), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mine)));
    setEnv(kEnv, buf);
    // two plug-ins created at the same instant may both have written: the variable holds one of them, and everybody (this one too) goes with that
    if (Registry* r = fromEnv()) return valid(r) ? r : nullptr;
    return mine;
}
inline uint64_t randomId() {
    static std::atomic<uint64_t> counter{0};
    std::random_device rd; uint64_t v = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ (static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) * 0x9E3779B97F4A7C15ull) ^ (counter.fetch_add(1) << 17);
    return v ? v : 1;
}
}  // namespace detail

// the registry of this process (null when the one in the environment has another layout).
// `static`, not `inline`: GCC makes the statics of inline functions "unique" symbols that the dynamic linker merges across every plug-in loaded into one process on Linux (macOS and Windows keep one copy per
// binary). With internal linkage every binary (every translation unit, in fact) asks the environment itself, which is what the other platforms do, so a Linux test means something.
static Registry* registry() { static Registry* r = detail::open(); return r; }

inline uint32_t packCode(const char* c) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<uint8_t>(c && c[i] ? c[i] : ' '); return v; }

class Member {
public:
    Member() = default;
    Member(const Member&) = delete; Member& operator=(const Member&) = delete;
    ~Member() { leave(); }

    // main thread. false: no registry, or all kSlots are taken (the instance then works like one without SW Link)
    bool join(const gui::RingView& ring, const char* product, double sampleRate = 48000.0) {
        if (slot_) return true;
        Registry* r = registry(); if (!r || !ring.data || !ring.head) return false;
        const uint64_t id = detail::randomId();
        for (int i = 0; i < kSlots; ++i) {
            uint64_t e = 0; Slot& s = r->s[i];
            if (!s.id.compare_exchange_strong(e, id)) continue;
            s.mask.store(ring.mask); s.sampleRate.store(sampleRate); s.product.store(packCode(product));
            s.head.store(ring.head); s.data.store(ring.data);   // last: a reader that sees the ring sees the rest
            slot_ = &s; id_ = id; reg_ = r; return true;
        }
        return false;
    }
    void setSampleRate(double sr) { if (slot_) slot_->sampleRate.store(sr); }
    // main thread: withdraw the ring, wait for the readers that are inside, free the slot. Call before the ring is destroyed.
    void leave() {
        if (!slot_) return;
        slot_->refSerial.store(0); slot_->sharedStamp.store(0);
        slot_->data.store(nullptr); slot_->head.store(nullptr);
        while (slot_->readers.load() != 0) std::this_thread::yield();
        slot_->id.store(0); slot_ = nullptr; reg_ = nullptr;
    }
    bool joined() const { return slot_ != nullptr; }
    // share a reference spectrum (kRefBands dB values; serial != 0) or withdraw it (serial 0): atomic stores only (not while leave() runs: the owner leaves when it is destroyed)
    void publishReference(uint32_t serial, const float* db) {
        if (!slot_) return;
        slot_->refSerial.store(0);
        if (!serial || !db) return;
        for (int i = 0; i < kRefBands; ++i) slot_->refDb[i].store(db[i], std::memory_order_relaxed);
        slot_->refSerial.store(serial, std::memory_order_release);
    }
    // GUI thread: the reference spectrum of another instance of `product` ("UT03"), if one has published one: db[kRefBands] in dB; returns the serial (0: none). With db null it only looks.
    uint32_t findReference(const char* product, double* db = nullptr) const {
        Registry* r = reg_ ? reg_ : registry(); if (!r) return 0;
        const uint32_t code = packCode(product);
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = r->s[i];
            const uint64_t id = s.id.load();
            if (id == 0 || id == id_ || s.product.load() != code) continue;
            const uint32_t a = s.refSerial.load(std::memory_order_acquire);
            if (!a) continue;
            if (!db) return a;
            float tmp[kRefBands]; for (int k = 0; k < kRefBands; ++k) tmp[k] = s.refDb[k].load(std::memory_order_relaxed);
            if (s.refSerial.load(std::memory_order_acquire) != a || s.id.load() != id) continue;   // changed while it was read
            for (int k = 0; k < kRefBands; ++k) db[k] = tmp[k];
            return a;
        }
        return 0;
    }
    void setTimeout(double seconds) { timeout_ = seconds; }
    void setSpectrumInterval(double seconds) { specInterval_ = seconds; }   // how often others() looks at the rings again (default 90 ms)

    // A shared setting (see above): the person changed it on this instance; atomics only (any thread, not while leave() runs)
    void publishShared(double value) {
        if (!slot_ || !reg_) return;
        const uint32_t st = reg_->stamp.fetch_add(1) + 1;
        slot_->sharedValue.store(value, std::memory_order_relaxed); slot_->sharedStamp.store(st, std::memory_order_release);
        seenStamp_ = st;
    }
    // ... and the other instances of `product` take it: the newest value that was published after the last one this instance published or took. False when there is none. Atomics only.
    bool adoptShared(const char* product, double& value) {
        Registry* r = reg_ ? reg_ : registry(); if (!r) return false;
        const uint32_t code = packCode(product);
        uint32_t best = 0; double v = 0.0;
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = r->s[i]; const uint64_t id = s.id.load();
            if (id == 0 || id == id_ || s.product.load() != code) continue;
            const uint32_t a = s.sharedStamp.load(std::memory_order_acquire);
            if (a == 0 || a <= seenStamp_ || a <= best) continue;
            const double x = s.sharedValue.load(std::memory_order_relaxed);
            if (s.sharedStamp.load(std::memory_order_acquire) != a || s.id.load() != id) continue;   // changed while it was read: the next call
            best = a; v = x;
        }
        if (!best) return false;
        seenStamp_ = best; value = v; return true;
    }

    // The audio thread: the latest `frames` samples of the output ring of another instance of `product` ("LV01") into out (the ring is the mean of its left and right). What it remembers between calls: which slot,
    // and whether the ring still moves. False when there is no such instance, or its ring has not moved for 2 calls (stopped, bypassed by the host): out is not touched then. No lock, no allocation.
    struct KeyState { int slot = -1; uint64_t id = 0; size_t head = 0; int still = 0; };
    bool readKey(const char* product, float* out, int frames, KeyState& st) const {
        Registry* r = reg_ ? reg_ : registry(); if (!r || frames <= 0) return false;
        const uint32_t code = packCode(product);
        auto tryRead = [&](int i) -> bool {
            Slot& s = r->s[i]; const uint64_t id = s.id.load();
            if (id == 0 || id == id_ || s.product.load() != code) return false;
            bool ok = false;
            s.readers.fetch_add(1);                                      // from here the owner cannot take the ring away
            const std::atomic<float>* d = s.data.load(); const std::atomic<size_t>* hp = s.head.load();
            if (d && hp && s.id.load() == id) {
                const size_t mask = s.mask.load(), h = hp->load(std::memory_order_acquire), n = static_cast<size_t>(frames);
                if (id != st.id || i != st.slot) { st.slot = i; st.id = id; st.head = h; st.still = 0; }
                else if (h != st.head) { st.head = h; st.still = 0; } else ++st.still;
                if (st.still < 2 && h >= n && mask + 1 >= n) {
                    for (size_t k = 0; k < n; ++k) out[k] = d[(h - n + k) & mask].load(std::memory_order_relaxed);
                    ok = s.id.load() == id;
                }
            }
            s.readers.fetch_sub(1);
            return ok;
        };
        if (st.slot >= 0 && tryRead(st.slot)) return true;
        for (int i = 0; i < kSlots; ++i) if (i != st.slot && tryRead(i)) return true;   // (another instance of the product, or the first time)
        return false;
    }

    // GUI thread: how many other instances are alive
    int peers() { return refresh(); }
    // GUI thread: the sum of the power of the other live instances' output spectra: out[gui::kSpecBands] in dB (-120 where nothing is), the number of instances that are in it.
    // Recomputed at most every 90 ms (setSpectrumInterval); the result in between is the last one.
    int others(double* out) {
        const auto now = Clock::now();
        if (cached_ && std::chrono::duration<double>(now - lastSpec_).count() < specInterval_) { std::copy(spec_, spec_ + gui::kSpecBands, out); return cachedN_; }
        const int n = refresh(true); std::copy(spec_, spec_ + gui::kSpecBands, out); cached_ = true; cachedN_ = n; lastSpec_ = now; return n;
    }
    // GUI thread, after peers() / others(): the product codes of the live instances (e.g. "EQ02 DY08"), at most `max` of them
    std::string peerCodes(int max = 12) const {
        std::string s; int k = 0;
        for (int i = 0; i < kSlots && k < max; ++i) if (seen_[i].alive && seen_[i].product) { if (k) s += ' '; for (int b = 3; b >= 0; --b) { const char c = static_cast<char>((seen_[i].product >> (8 * b)) & 0xff); if (c != ' ') s += c; } ++k; }
        return s;
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Seen { uint64_t id = 0; size_t head = 0; Clock::time_point moved; bool alive = false; uint32_t product = 0; };

    // walks the slots: updates who is alive and, with `spectra`, adds up their power
    int refresh(bool spectra = false) {
        Registry* r = reg_ ? reg_ : registry(); if (!r) return 0;
        const auto now = Clock::now(); int n = 0;
        double pw[gui::kSpecBands] = {}; std::vector<float> x;
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = r->s[i]; Seen& v = seen_[i]; v.alive = false;
            const uint64_t id = s.id.load();
            if (id == 0 || id == id_) { v.id = 0; continue; }
            s.readers.fetch_add(1);                                    // from here the owner cannot take the ring away
            const std::atomic<float>* d = s.data.load(); const std::atomic<size_t>* hp = s.head.load();
            if (d && hp && s.id.load() == id) {
                const size_t mask = s.mask.load(), h = hp->load(std::memory_order_acquire);
                if (v.id != id) { v = Seen{}; v.id = id; v.head = h; v.moved = now; }
                else if (h != v.head) { v.head = h; v.moved = now; }
                v.product = s.product.load();
                v.alive = std::chrono::duration<double>(now - v.moved).count() < timeout_;
                if (v.alive) {
                    ++n;
                    if (spectra && mask + 1 >= static_cast<size_t>(gui::kSpecFft) && h >= static_cast<size_t>(gui::kSpecFft)) {
                        x.resize(gui::kSpecFft);
                        for (int k = 0; k < gui::kSpecFft; ++k) x[static_cast<size_t>(k)] = d[(h - gui::kSpecFft + static_cast<size_t>(k)) & mask].load(std::memory_order_relaxed);
                        const double sr = s.sampleRate.load();
                        if (s.id.load() == id) { double db[gui::kSpecBands]; gui::spectrumOf(x.data(), sr > 0 ? sr : 48000.0, db); for (int b = 0; b < gui::kSpecBands; ++b) pw[b] += std::pow(10.0, db[b] / 10.0); }
                    }
                }
            } else v.id = 0;
            s.readers.fetch_sub(1);
        }
        if (spectra) for (int b = 0; b < gui::kSpecBands; ++b) spec_[b] = pw[b] > 1e-12 ? 10.0 * std::log10(pw[b]) : -120.0;
        return n;
    }

    Slot* slot_ = nullptr; Registry* reg_ = nullptr; uint64_t id_ = 0; uint32_t seenStamp_ = 0;
    double timeout_ = 1.5, specInterval_ = 0.09;
    Seen seen_[kSlots];
    double spec_[gui::kSpecBands] = {}; bool cached_ = false; int cachedN_ = 0; Clock::time_point lastSpec_;
};

}  // namespace sw::link
