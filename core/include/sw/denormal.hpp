// SW AUDIO core — scoped flush-to-zero / denormals-are-zero for the audio thread
#pragma once
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
namespace sw {
class ScopedNoDenormals {
public:
    ScopedNoDenormals() : old_(_mm_getcsr()) { _mm_setcsr(old_ | 0x8040); }  // FTZ | DAZ
    ~ScopedNoDenormals() { _mm_setcsr(old_); }
private:
    unsigned int old_;
};
}
#elif defined(__aarch64__)  // GCC/Clang incl. Apple Silicon (MSVC ARM64 falls back to no-op)
#include <cstdint>
namespace sw {
class ScopedNoDenormals {
public:
    ScopedNoDenormals() { asm volatile("mrs %0, fpcr" : "=r"(old_)); asm volatile("msr fpcr, %0" ::"r"(old_ | (1ull << 24))); }
    ~ScopedNoDenormals() { asm volatile("msr fpcr, %0" ::"r"(old_)); }
private:
    uint64_t old_ = 0;
};
}
#else
namespace sw { struct ScopedNoDenormals {}; }
#endif
