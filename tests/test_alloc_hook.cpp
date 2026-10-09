// the global operator new / delete of the unit-test binary, counting while an allocguard::Scope is alive (tests/alloc_guard.hpp)
#include "alloc_guard.hpp"
#include <cstdlib>
#include <new>
#include <execinfo.h>
#include <unistd.h>

namespace {
void note() {   // SW_ALLOC_TRACE=1: where the counted allocations come from (addresses; addr2line -f -C -e build-cmake/sw-tests <offset> turns them into functions)
    static const bool trace = std::getenv("SW_ALLOC_TRACE") != nullptr;
    if (allocguard::count.fetch_add(1) == 0 && trace) {   // the first one of a Scope
        allocguard::on = false; void* bt[24]; const int k = backtrace(bt, 24); backtrace_symbols_fd(bt, k, 2); (void)!write(2, "----\n", 5); allocguard::on = true;
    }
}
void* take(std::size_t n) {
    if (allocguard::on) note();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* takeAligned(std::size_t n, std::size_t a) {
    if (allocguard::on) note();
    void* p = nullptr;
    if (a < sizeof(void*)) a = sizeof(void*);
    if (posix_memalign(&p, a, n ? n : 1) != 0) throw std::bad_alloc();
    return p;
}
}  // namespace
void* operator new(std::size_t n) { return take(n); }
void* operator new[](std::size_t n) { return take(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return take(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return take(n); } catch (...) { return nullptr; } }
void* operator new(std::size_t n, std::align_val_t a) { return takeAligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return takeAligned(n, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
