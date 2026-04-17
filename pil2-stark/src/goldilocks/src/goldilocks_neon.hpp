#ifndef GOLDILOCKS_NEON_HPP
#define GOLDILOCKS_NEON_HPP

#include "platform.hpp"
#include "goldilocks_base_field.hpp"

#if PIL2_HAS_NEON

#include <arm_neon.h>
#include <cstdint>

// ============================================================================
// NEON field-op helpers for Goldilocks (p = 2^64 - 2^32 + 1).
//
// 2 lanes per `uint64x2_t`. Designed to be shared across NEON callers
// (Poseidon2, NTT, future kernels) — owns the canonical forms of
// load / store / splat / gl_add / gl_sub / gl_mul / gl_square.
//
// Implementation notes:
// - The inner 64x64 -> 128 mul uses paired aarch64 inline asm: two scalar
//   `mul`+`umulh` chains interleaved so both Apple Silicon integer-mul
//   pipes are issued each cycle. NEON has no widening multiply intrinsic,
//   and per-lane `__uint128_t` underutilises the second pipe.
// - The reduction matches `goldilocks_base_field_scalar.hpp::Goldilocks::mul`
//   bit-for-bit (lo + (mid << 32) - mid - hi, with the standard borrow/carry
//   handling), so NEON results are bit-exact with the scalar path.
// - gl_add / gl_sub use real NEON intrinsics (vaddq/vsubq + vcgeq/vcltq +
//   vandq) — branchless lane-wise canonicalisation.
// ============================================================================

namespace Goldilocks_neon {

// Goldilocks prime p = 2^64 - 2^32 + 1
static constexpr uint64_t P              = 0xFFFFFFFF00000001ULL;
// -p mod 2^64 = 2^64 - p = 2^32 - 1 (used as the carry-correction offset).
static constexpr uint64_t MINUS_P_MOD_64 = 0x00000000FFFFFFFFULL;

// ---- Loads / stores / broadcasts ------------------------------------------

inline uint64x2_t splat(uint64_t x) {
    return vdupq_n_u64(x);
}

inline uint64x2_t load(const Goldilocks::Element* p) {
    return vld1q_u64(reinterpret_cast<const uint64_t*>(p));
}

inline void store(Goldilocks::Element* p, uint64x2_t v) {
    vst1q_u64(reinterpret_cast<uint64_t*>(p), v);
}

// ---- Per-lane scalar Goldilocks reduction (matches scalar reference) ------

// Modular Goldilocks multiplication for one lane. Bit-exact with
// goldilocks_base_field_scalar.hpp::Goldilocks::mul.
//   res = (uint128) a * b
//   rl = res[63:0],  rh = res[127:64]
//   rhh = rh[63:32], rhl = rh[31:0]
//   aux1 = rl - rhh  (with -=0xFFFFFFFF on borrow)
//   aux  = 0xFFFFFFFF * rhl
//   r = aux1 + aux mod p
static inline uint64_t gl_mul_scalar(uint64_t a, uint64_t b) {
    __uint128_t res = (__uint128_t)a * (__uint128_t)b;
    uint64_t rl  = (uint64_t)res;
    uint64_t rh  = (uint64_t)(res >> 64);
    uint64_t rhh = rh >> 32;
    uint64_t rhl = rh & 0xFFFFFFFFULL;
    uint64_t aux1 = rl - rhh;
    if (rhh > rl) aux1 -= 0xFFFFFFFFULL;
    uint64_t aux2 = 0xFFFFFFFFULL * rhl;
    // Modular add of aux1 and aux2 (matches scalar add path).
    uint64_t r = aux1 + aux2;
    if (r < aux1) r += 0xFFFFFFFFULL;  // carry correction
    if (r >= P)   r -= P;              // canonical reduction
    return r;
}

static inline uint64_t gl_add_scalar(uint64_t a, uint64_t b) {
    // Pre-canonicalize a (matches scalar reference's `if (in_1 >= P) in_1 -= P`).
    if (a >= P) a -= P;
    uint64_t r = a + b;
    if (a > r) r -= P;  // overflow detection: r < a iff carry-out
    return r;
}

static inline uint64_t gl_sub_scalar(uint64_t a, uint64_t b) {
    // Compute (a - b) mod p; if a < b, add p back (handled via overflow on
    // the wraparound).
    if (b >= P) b -= P;  // canonicalize b
    uint64_t r = a - b;
    if (a < b) r += P;   // borrow -> add p back (mod 2^64 the +P cancels the synthetic 2^64)
    return r;
}

// ---- Vectorised wrappers --------------------------------------------------

// Paired Goldilocks mul on aarch64. Apple Silicon has two integer-mul
// pipes (each can issue mul+umulh per cycle). The naive per-lane scalar
// path uses only one pipe at a time, giving NO real speedup over scalar.
// Manually interleaving the two lanes' mul/umulh and reduction steps
// lets clang feed both pipes — the actual win.
//
// Reduction (matches goldilocks_base_field_scalar.hpp::Goldilocks::mul
// bit-for-bit, returning a value in [0, 2^64) which may be in [p, 2p)):
//   prod = a * b              (128 bits via mul + umulh)
//   hh   = prod[127:96]
//   hl   = prod[95:64]
//   t    = prod[63:0] - hh    (subtract hh from lo, +EPSILON on borrow)
//   he   = (hl << 32) - hl    = hl * (2^32 - 1) = hl * EPSILON
//   r    = t + he             (mod 2^64; +EPSILON on overflow)
// where EPSILON = 2^32 - 1 = -p mod 2^64.
inline uint64x2_t gl_mul(uint64x2_t a, uint64x2_t b) {
#if defined(__aarch64__)
    uint64_t a0 = vgetq_lane_u64(a, 0);
    uint64_t a1 = vgetq_lane_u64(a, 1);
    uint64_t b0 = vgetq_lane_u64(b, 0);
    uint64_t b1 = vgetq_lane_u64(b, 1);
    constexpr uint64_t EPS = 0xFFFFFFFFULL;  // -p mod 2^64
    uint64_t r0, r1;
    uint64_t lo0, lo1, hi0, hi1, hh0, hh1, hl0, hl1, s0, s1, he0, he1;
    uint64_t adj0, adj1, t0, t1;
    asm(
        "mul   %[lo0], %[a0], %[b0]\n\t"
        "mul   %[lo1], %[a1], %[b1]\n\t"
        "umulh %[hi0], %[a0], %[b0]\n\t"
        "umulh %[hi1], %[a1], %[b1]\n\t"
        "lsr   %[hh0], %[hi0], #32\n\t"
        "lsr   %[hh1], %[hi1], #32\n\t"
        "subs  %[t0],  %[lo0], %[hh0]\n\t"
        "csetm %w[adj0], cc\n\t"
        "subs  %[t1],  %[lo1], %[hh1]\n\t"
        "csetm %w[adj1], cc\n\t"
        "sub   %[t0],  %[t0], %[adj0]\n\t"
        "sub   %[t1],  %[t1], %[adj1]\n\t"
        "and   %[hl0], %[hi0], %[eps]\n\t"
        "and   %[hl1], %[hi1], %[eps]\n\t"
        "lsl   %[s0],  %[hl0], #32\n\t"
        "lsl   %[s1],  %[hl1], #32\n\t"
        "sub   %[he0], %[s0], %[hl0]\n\t"
        "sub   %[he1], %[s1], %[hl1]\n\t"
        "adds  %[r0],  %[t0], %[he0]\n\t"
        "csetm %w[adj0], cs\n\t"
        "adds  %[r1],  %[t1], %[he1]\n\t"
        "csetm %w[adj1], cs\n\t"
        "add   %[r0],  %[r0], %[adj0]\n\t"
        "add   %[r1],  %[r1], %[adj1]\n\t"
        : [r0]"=&r"(r0),   [r1]"=&r"(r1),
          [lo0]"=&r"(lo0), [lo1]"=&r"(lo1),
          [hi0]"=&r"(hi0), [hi1]"=&r"(hi1),
          [hh0]"=&r"(hh0), [hh1]"=&r"(hh1),
          [hl0]"=&r"(hl0), [hl1]"=&r"(hl1),
          [s0]"=&r"(s0),   [s1]"=&r"(s1),
          [he0]"=&r"(he0), [he1]"=&r"(he1),
          [adj0]"=&r"(adj0), [adj1]"=&r"(adj1),
          [t0]"=&r"(t0),   [t1]"=&r"(t1)
        : [a0]"r"(a0), [b0]"r"(b0),
          [a1]"r"(a1), [b1]"r"(b1),
          [eps]"r"(EPS)
        : "cc"
    );
    // Construct the result NEON reg via direct GP->NEON lane moves
    // (compiles to two fmov instructions on aarch64) instead of a
    // stack write + vld1q_u64 round-trip.
    uint64x2_t out = vsetq_lane_u64(r0, vdupq_n_u64(0), 0);
    return vsetq_lane_u64(r1, out, 1);
#else
    uint64_t r0 = gl_mul_scalar(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0));
    uint64_t r1 = gl_mul_scalar(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1));
    uint64x2_t out = vdupq_n_u64(0);
    out = vsetq_lane_u64(r0, out, 0);
    out = vsetq_lane_u64(r1, out, 1);
    return out;
#endif
}

inline uint64x2_t gl_square(uint64x2_t a) {
    return gl_mul(a, a);
}

// Specialised add for when ONE operand is known canonical (< p). The other
// operand may be non-canonical in [0, 2^64). Skips the pre-canonicalise of
// `a` that the general gl_add needs to keep itself to a single overflow
// correction. 4 NEON ops vs 7 — saves 3 ops per call.
//
// Correctness: a < 2^64, b_canon < p ≈ 2^64. a + b_canon < 2^64 + p < 2^65,
// so at most one wrap. On wrap, r = a + b_canon - 2^64, and r < a < 2^64,
// with r strictly less than p (since the wrapped excess is bounded by b_canon
// < p). r -= p (equivalent to r += EPS) stays in [0, 2^64) — no second wrap.
// Output is non-canonical [0, 2^64), matching gl_mul's contract.
//
// Use at call sites where one operand is a compile-time constant or otherwise
// known canonical (e.g. Poseidon2 round constants C[], scalar broadcasts of
// a pre-reduced value).
inline uint64x2_t gl_add_c(uint64x2_t a, uint64x2_t b_canon) {
    const uint64x2_t p_vec = vdupq_n_u64(P);
    uint64x2_t r = vaddq_u64(a, b_canon);
    uint64x2_t overflow = vcgtq_u64(a, r);   // r < a iff carry-out
    return vsubq_u64(r, vandq_u64(overflow, p_vec));
}

// Modular add, fully vectorised. Bit-exact with scalar Goldilocks::add:
//   1. Canonicalise a: if (a >= p) a -= p
//   2. r = a + b
//   3. Overflow correction: if (a > r) r -= p   (carry detection via vcgtq)
inline uint64x2_t gl_add(uint64x2_t a, uint64x2_t b) {
    const uint64x2_t p_vec = vdupq_n_u64(P);
    // Step 1: canonicalise a
    uint64x2_t a_ge_p = vcgeq_u64(a, p_vec);
    a = vsubq_u64(a, vandq_u64(a_ge_p, p_vec));
    // Step 2: add
    uint64x2_t r = vaddq_u64(a, b);
    // Step 3: overflow correction (vcgtq_u64(a, r) iff carry-out happened)
    uint64x2_t overflow = vcgtq_u64(a, r);
    return vsubq_u64(r, vandq_u64(overflow, p_vec));
}

// Modular sub, fully vectorised. Bit-exact with scalar Goldilocks::sub:
//   1. Canonicalise b: if (b >= p) b -= p
//   2. r = a - b
//   3. Borrow correction: if (a < b) r += p
inline uint64x2_t gl_sub(uint64x2_t a, uint64x2_t b) {
    const uint64x2_t p_vec = vdupq_n_u64(P);
    // Step 1: canonicalise b
    uint64x2_t b_ge_p = vcgeq_u64(b, p_vec);
    b = vsubq_u64(b, vandq_u64(b_ge_p, p_vec));
    // Step 2: subtract
    uint64x2_t r = vsubq_u64(a, b);
    // Step 3: borrow correction (vcltq_u64(a, b) iff borrow-out)
    uint64x2_t borrow = vcltq_u64(a, b);
    return vaddq_u64(r, vandq_u64(borrow, p_vec));
}

}  // namespace Goldilocks_neon

#endif  // PIL2_HAS_NEON
#endif  // GOLDILOCKS_NEON_HPP
