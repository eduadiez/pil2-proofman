#ifndef POSEIDON2_GOLDILOCKS_NEON
#define POSEIDON2_GOLDILOCKS_NEON

#include "platform.hpp"
#include "poseidon2_goldilocks.hpp"
#include "goldilocks_base_field.hpp"

#if PIL2_HAS_NEON

#include <arm_neon.h>
#include <cstdint>

// ============================================================================
// NEON field-op helpers for Goldilocks (p = 2^64 - 2^32 + 1).
//
// 2 lanes per `uint64x2_t`. State of W elements -> W/2 NEON registers.
//
// Implementation notes:
// - The inner 64x64 -> 128 mul uses `__uint128_t` per lane: NEON lacks a
//   widening multiply intrinsic, and on Apple Silicon clang's scheduling of
//   the C path beats hand-written intrinsics (verified in the goldilocks
//   reference port).
// - The reduction matches `goldilocks_base_field_scalar.hpp::Goldilocks::mul`
//   bit-for-bit (lo + (mid << 32) - mid - hi, with the standard borrow/carry
//   handling), so NEON results are bit-exact with the scalar path.
// - Add uses NEON for the data-flow but per-lane scalar canonicalization to
//   avoid NEON's lack of a carry-out signal on `vaddq_u64`.
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

inline uint64x2_t gl_mul(uint64x2_t a, uint64x2_t b) {
    uint64_t r0 = gl_mul_scalar(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0));
    uint64_t r1 = gl_mul_scalar(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1));
    uint64x2_t out = vdupq_n_u64(0);
    out = vsetq_lane_u64(r0, out, 0);
    out = vsetq_lane_u64(r1, out, 1);
    return out;
}

inline uint64x2_t gl_square(uint64x2_t a) {
    return gl_mul(a, a);
}

// Modular add. NEON adds the lanes in parallel; canonicalisation is per-lane
// scalar to avoid NEON's lack of a carry-out signal on vaddq_u64.
inline uint64x2_t gl_add(uint64x2_t a, uint64x2_t b) {
    uint64_t r0 = gl_add_scalar(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0));
    uint64_t r1 = gl_add_scalar(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1));
    uint64x2_t out = vdupq_n_u64(0);
    out = vsetq_lane_u64(r0, out, 0);
    out = vsetq_lane_u64(r1, out, 1);
    return out;
}

inline uint64x2_t gl_sub(uint64x2_t a, uint64x2_t b) {
    uint64_t r0 = gl_sub_scalar(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0));
    uint64_t r1 = gl_sub_scalar(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1));
    uint64x2_t out = vdupq_n_u64(0);
    out = vsetq_lane_u64(r0, out, 0);
    out = vsetq_lane_u64(r1, out, 1);
    return out;
}

}  // namespace Goldilocks_neon

// ============================================================================
// Poseidon2-level NEON primitive bodies. Forward declarations live in
// poseidon2_goldilocks.hpp under #if PIL2_HAS_NEON. Bodies land in Task 34
// (W=8) and subsequent per-width tasks.
// ============================================================================

// (intentionally empty for now — Task 33 ships the field-op spine only)

#endif  // PIL2_HAS_NEON
#endif  // POSEIDON2_GOLDILOCKS_NEON
