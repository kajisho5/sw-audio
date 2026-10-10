// SW AUDIO core — two doubles in one SIMD register (left and right of a stereo chain that share their coefficients).
// SSE2 on x86-64 (every x86-64 CPU has it; MSVC, GCC, Clang), NEON on 64-bit ARM (Apple silicon), WebAssembly SIMD (the browser trial,
// built with -msimd128), plain pairs elsewhere.
// Only what the filters and the oversamplers need: + - * /, vmin, vmax (not min / max: no clash with the Windows macros), a broadcast, the lanes.
#pragma once

#if defined(SW_SIMD2_SCALAR)   // forced plain pairs (the tests build this way too)
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define SW_SIMD2_SSE2 1
#elif defined(__ARM_NEON) && (defined(__aarch64__) || defined(_M_ARM64))
#include <arm_neon.h>
#define SW_SIMD2_NEON 1
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define SW_SIMD2_WASM 1
#endif

namespace sw {

struct D2 {
#if defined(SW_SIMD2_SSE2)
    __m128d v;
    D2() : v(_mm_setzero_pd()) {}
    explicit D2(__m128d x) : v(x) {}
    D2(double a, double b) : v(_mm_set_pd(b, a)) {}
    static D2 all(double a) { return D2(_mm_set1_pd(a)); }
    double lo() const { return _mm_cvtsd_f64(v); }
    double hi() const { return _mm_cvtsd_f64(_mm_unpackhi_pd(v, v)); }
    friend D2 operator+(D2 a, D2 b) { return D2(_mm_add_pd(a.v, b.v)); }
    friend D2 operator-(D2 a, D2 b) { return D2(_mm_sub_pd(a.v, b.v)); }
    friend D2 operator*(D2 a, D2 b) { return D2(_mm_mul_pd(a.v, b.v)); }
    friend D2 operator/(D2 a, D2 b) { return D2(_mm_div_pd(a.v, b.v)); }
    friend D2 vmin(D2 a, D2 b) { return D2(_mm_min_pd(a.v, b.v)); }
    friend D2 vmax(D2 a, D2 b) { return D2(_mm_max_pd(a.v, b.v)); }
#elif defined(SW_SIMD2_NEON)
    float64x2_t v;
    D2() : v(vdupq_n_f64(0.0)) {}
    explicit D2(float64x2_t x) : v(x) {}
    D2(double a, double b) { const double t[2] = {a, b}; v = vld1q_f64(t); }
    static D2 all(double a) { return D2(vdupq_n_f64(a)); }
    double lo() const { return vgetq_lane_f64(v, 0); }
    double hi() const { return vgetq_lane_f64(v, 1); }
    friend D2 operator+(D2 a, D2 b) { return D2(vaddq_f64(a.v, b.v)); }
    friend D2 operator-(D2 a, D2 b) { return D2(vsubq_f64(a.v, b.v)); }
    friend D2 operator*(D2 a, D2 b) { return D2(vmulq_f64(a.v, b.v)); }
    friend D2 operator/(D2 a, D2 b) { return D2(vdivq_f64(a.v, b.v)); }
    friend D2 vmin(D2 a, D2 b) { return D2(vminq_f64(a.v, b.v)); }
    friend D2 vmax(D2 a, D2 b) { return D2(vmaxq_f64(a.v, b.v)); }
#elif defined(SW_SIMD2_WASM)
    v128_t v;
    D2() : v(wasm_f64x2_splat(0.0)) {}
    explicit D2(v128_t x) : v(x) {}
    D2(double a, double b) : v(wasm_f64x2_make(a, b)) {}
    static D2 all(double a) { return D2(wasm_f64x2_splat(a)); }
    double lo() const { return wasm_f64x2_extract_lane(v, 0); }
    double hi() const { return wasm_f64x2_extract_lane(v, 1); }
    friend D2 operator+(D2 a, D2 b) { return D2(wasm_f64x2_add(a.v, b.v)); }
    friend D2 operator-(D2 a, D2 b) { return D2(wasm_f64x2_sub(a.v, b.v)); }
    friend D2 operator*(D2 a, D2 b) { return D2(wasm_f64x2_mul(a.v, b.v)); }
    friend D2 operator/(D2 a, D2 b) { return D2(wasm_f64x2_div(a.v, b.v)); }
    friend D2 vmin(D2 a, D2 b) { return D2(wasm_f64x2_pmin(b.v, a.v)); }   // a < b ? a : b, as the plain pairs
    friend D2 vmax(D2 a, D2 b) { return D2(wasm_f64x2_pmax(b.v, a.v)); }   // a > b ? a : b
#else
    double a_ = 0.0, b_ = 0.0;
    D2() = default;
    D2(double a, double b) : a_(a), b_(b) {}
    static D2 all(double a) { return D2(a, a); }
    double lo() const { return a_; }
    double hi() const { return b_; }
    friend D2 operator+(D2 a, D2 b) { return D2(a.a_ + b.a_, a.b_ + b.b_); }
    friend D2 operator-(D2 a, D2 b) { return D2(a.a_ - b.a_, a.b_ - b.b_); }
    friend D2 operator*(D2 a, D2 b) { return D2(a.a_ * b.a_, a.b_ * b.b_); }
    friend D2 operator/(D2 a, D2 b) { return D2(a.a_ / b.a_, a.b_ / b.b_); }
    friend D2 vmin(D2 a, D2 b) { return D2(a.a_ < b.a_ ? a.a_ : b.a_, a.b_ < b.b_ ? a.b_ : b.b_); }
    friend D2 vmax(D2 a, D2 b) { return D2(a.a_ > b.a_ ? a.a_ : b.a_, a.b_ > b.b_ ? a.b_ : b.b_); }
#endif
    D2& operator+=(D2 b) { *this = *this + b; return *this; }
    D2& operator-=(D2 b) { *this = *this - b; return *this; }
    D2& operator*=(D2 b) { *this = *this * b; return *this; }
};

}  // namespace sw
