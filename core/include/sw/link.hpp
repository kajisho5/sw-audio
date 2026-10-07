// SW AUDIO core — "Link": a small registry that the instances of one product inside one process share (instance 1..8 publish a few numbers and read the others'). A stand-in for SW Link
// (which does not exist yet) that works wherever the instances of a plug-in live in the same process: the SW AUDIO engine (all instances in one process) and most hosts.
// It does not reach across processes or across different products. Lock-free (atomics only); claim() / release() are done in prepare() / the destructor, never in the audio callback.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace sw {

template <class Tag, int N = 8>
class LinkGroup {
public:
    struct Slot { std::atomic<bool> used{false}; std::atomic<float> a{0.0f}, b{0.0f}; std::atomic<bool> flag{false}; std::atomic<unsigned> beat{0}; };
    static std::array<Slot, N>& slots() { static std::array<Slot, N> s; return s; }
    // the lowest free slot, or -1 when all N are taken
    static int claim() {
        for (int i = 0; i < N; ++i) { bool e = false; if (slots()[static_cast<std::size_t>(i)].used.compare_exchange_strong(e, true)) { auto& s = slots()[static_cast<std::size_t>(i)]; s.a = 0; s.b = 0; s.flag = false; s.beat = 0; return i; } }
        return -1;
    }
    static void release(int i) { if (i >= 0 && i < N) { auto& s = slots()[static_cast<std::size_t>(i)]; s.flag = false; s.a = 0; s.b = 0; s.used = false; } }
};

// A reader's view of who is alive: an instance that stops processing (bypassed, stopped) leaves its numbers behind, so every member bumps `beat` once per block (LinkMember::tick())
// and a reader treats a slot whose beat has not moved for `timeout` seconds of its own time as gone.
template <class Tag, int N = 8>
class LinkWatch {
public:
    using Group = LinkGroup<Tag, N>;
    void update(double blockSeconds, double timeout = 0.5) {
        for (int i = 0; i < N; ++i) {
            auto& s = Group::slots()[static_cast<std::size_t>(i)]; const unsigned b = s.beat.load();
            if (b != beat_[static_cast<std::size_t>(i)]) { beat_[static_cast<std::size_t>(i)] = b; idle_[static_cast<std::size_t>(i)] = 0.0; } else idle_[static_cast<std::size_t>(i)] += blockSeconds;
            alive_[static_cast<std::size_t>(i)] = s.used.load() && idle_[static_cast<std::size_t>(i)] < timeout;
        }
    }
    bool alive(int i) const { return alive_[static_cast<std::size_t>(i)]; }
private:
    std::array<unsigned, N> beat_{}; std::array<double, N> idle_{}; std::array<bool, N> alive_{};
};

// owns one slot for its lifetime
template <class Tag, int N = 8>
class LinkMember {
public:
    using Group = LinkGroup<Tag, N>;
    LinkMember() = default;
    LinkMember(const LinkMember&) = delete; LinkMember& operator=(const LinkMember&) = delete;
    LinkMember(LinkMember&& o) noexcept : idx_(o.idx_) { o.idx_ = -1; }
    LinkMember& operator=(LinkMember&& o) noexcept { if (this != &o) { Group::release(idx_); idx_ = o.idx_; o.idx_ = -1; } return *this; }
    ~LinkMember() { Group::release(idx_); }
    void join() { if (idx_ < 0) idx_ = Group::claim(); }
    void leave() { Group::release(idx_); idx_ = -1; }
    int index() const { return idx_; }
    bool joined() const { return idx_ >= 0; }
    void tick() const { if (idx_ >= 0) Group::slots()[static_cast<std::size_t>(idx_)].beat.fetch_add(1); }
    typename Group::Slot* mine() const { return idx_ >= 0 ? &Group::slots()[static_cast<std::size_t>(idx_)] : nullptr; }
private:
    int idx_ = -1;
};

}  // namespace sw
