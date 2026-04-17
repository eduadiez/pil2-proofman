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
    uint64_t tmp[2] = {r0, r1};
    return vld1q_u64(tmp);
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
// Poseidon2-level NEON primitive bodies. Forward declarations of permute_neon
// / compress_neon live in poseidon2_goldilocks.hpp under #if PIL2_HAS_NEON.
//
// Implementation notes:
// - State of W elements lives in W/2 NEON registers (uint64x2_t).
// - matmul_external_neon is a NEON-store -> scalar-call -> NEON-load wrapper
//   around the existing scalar matmul. The math is purely cheap adds; the
//   round bottleneck is pow7 (which IS in NEON), so the punt is fine for
//   now and keeps the matmul bit-exact-by-construction.
// - The partial-round loop mirrors the AVX path's "track aux + state0 in
//   scalar; let st[0] lane 0 hold the wrong value; fix it once at the end"
//   trick to avoid per-iteration NEON<->scalar roundtrips.
// ============================================================================

// ---- Element-wise primitives ---------------------------------------------

template<uint32_t SPONGE_WIDTH_T>
inline void Poseidon2Goldilocks<SPONGE_WIDTH_T>::permute_neon(
    Goldilocks::Element *output, const Goldilocks::Element *input)
{
    // Pick the per-width round constants.
    const Goldilocks::Element* C =
        SPONGE_WIDTH ==  4 ? Poseidon2GoldilocksConstants::C4  :
        SPONGE_WIDTH ==  8 ? Poseidon2GoldilocksConstants::C8  :
        SPONGE_WIDTH == 12 ? Poseidon2GoldilocksConstants::C12 :
                             Poseidon2GoldilocksConstants::C16;
    const Goldilocks::Element* D =
        SPONGE_WIDTH ==  4 ? Poseidon2GoldilocksConstants::D4  :
        SPONGE_WIDTH ==  8 ? Poseidon2GoldilocksConstants::D8  :
        SPONGE_WIDTH == 12 ? Poseidon2GoldilocksConstants::D12 :
                             Poseidon2GoldilocksConstants::D16;

    // Lambdas factor out the inline NEON helpers without dragging the W
    // template parameter into a free-function dispatch.
    constexpr uint32_t HALF_W = SPONGE_WIDTH >> 1;

    auto matmul_external_neon = [](uint64x2_t st[HALF_W]) {
        Goldilocks::Element scratch[SPONGE_WIDTH];
        for (uint32_t i = 0; i < HALF_W; ++i)
            Goldilocks_neon::store(&scratch[i << 1], st[i]);
        Poseidon2Goldilocks<SPONGE_WIDTH_T>::matmul_external_(scratch);
        for (uint32_t i = 0; i < HALF_W; ++i)
            st[i] = Goldilocks_neon::load(&scratch[i << 1]);
    };

    // Fused (state + C)^7 element-wise, paired across two regs for ILP.
    auto pow7add_neon = [](uint64x2_t st[HALF_W],
                            const Goldilocks::Element C_[SPONGE_WIDTH]) {
        for (uint32_t i = 0; i < HALF_W; i += 2) {
            uint64x2_t c0 = Goldilocks_neon::load(&C_[(i + 0) << 1]);
            uint64x2_t c1 = Goldilocks_neon::load(&C_[(i + 1) << 1]);
            uint64x2_t a0 = Goldilocks_neon::gl_add(st[i + 0], c0);
            uint64x2_t a1 = Goldilocks_neon::gl_add(st[i + 1], c1);
            // Inline pow7 with two independent chains so the two integer-mul
            // pipes on Apple Silicon stay busy each cycle.
            uint64x2_t pw2_0 = Goldilocks_neon::gl_square(a0);
            uint64x2_t pw2_1 = Goldilocks_neon::gl_square(a1);
            uint64x2_t pw4_0 = Goldilocks_neon::gl_square(pw2_0);
            uint64x2_t pw4_1 = Goldilocks_neon::gl_square(pw2_1);
            uint64x2_t pw3_0 = Goldilocks_neon::gl_mul(a0, pw2_0);
            uint64x2_t pw3_1 = Goldilocks_neon::gl_mul(a1, pw2_1);
            st[i + 0] = Goldilocks_neon::gl_mul(pw3_0, pw4_0);
            st[i + 1] = Goldilocks_neon::gl_mul(pw3_1, pw4_1);
        }
    };

    // Load state into W/2 NEON regs.
    std::memcpy(output, input, SPONGE_WIDTH * sizeof(Goldilocks::Element));
    uint64x2_t st[HALF_W];
    for (uint32_t i = 0; i < HALF_W; ++i)
        st[i] = Goldilocks_neon::load(&output[i << 1]);

    // Initial M_E.
    matmul_external_neon(st);

    // First half full rounds.
    for (uint32_t r = 0; r < HALF_N_FULL_ROUNDS; ++r) {
        pow7add_neon(st, &C[r * SPONGE_WIDTH]);
        matmul_external_neon(st);
    }

    // Partial rounds. Track state[0] and aux in scalar; let st[0] lane 0 drift
    // to the wrong value and fix it once at the end (matches AVX trick).
    Goldilocks::Element aux_lanes[2];
    Goldilocks_neon::store(aux_lanes, st[0]);
    Goldilocks::Element state0 = aux_lanes[0];
    Goldilocks::Element aux    = state0;

    for (uint32_t r = 0; r < N_PARTIAL_ROUNDS; ++r) {
        // Sum across all lanes of all NEON regs.
        uint64x2_t partial = st[0];
        for (uint32_t i = 1; i < HALF_W; ++i)
            partial = Goldilocks_neon::gl_add(partial, st[i]);
        Goldilocks::Element partial_lanes[2];
        Goldilocks_neon::store(partial_lanes, partial);
        Goldilocks::Element sum = partial_lanes[0] + partial_lanes[1];
        sum = sum - aux;                                            // exclude old state[0]
        state0 = state0 + C[HALF_N_FULL_ROUNDS * SPONGE_WIDTH + r];
        Poseidon2Goldilocks<SPONGE_WIDTH_T>::pow7(state0);          // partial-round S-box
        sum = sum + state0;                                         // include new state[0]

        uint64x2_t scalar = Goldilocks_neon::splat(sum.fe);
        for (uint32_t i = 0; i < HALF_W; ++i) {
            uint64x2_t d = Goldilocks_neon::load(&D[i << 1]);
            st[i] = Goldilocks_neon::gl_mul(st[i], d);
            st[i] = Goldilocks_neon::gl_add(st[i], scalar);
        }

        // st[0] lane 0 now holds aux*D[0] + sum (wrong; the right value is
        // state0*D[0] + sum). Update both trackers in lockstep.
        state0 = state0 * D[0] + sum;
        aux    = aux    * D[0] + sum;
    }

    // Patch the wrong lane-0 of st[0] to the correct state[0].
    Goldilocks_neon::store(aux_lanes, st[0]);
    aux_lanes[0] = state0;
    st[0] = Goldilocks_neon::load(aux_lanes);

    // Second half full rounds.
    for (uint32_t r = 0; r < HALF_N_FULL_ROUNDS; ++r) {
        pow7add_neon(st, &C[HALF_N_FULL_ROUNDS * SPONGE_WIDTH + N_PARTIAL_ROUNDS + r * SPONGE_WIDTH]);
        matmul_external_neon(st);
    }

    // Store result.
    for (uint32_t i = 0; i < HALF_W; ++i)
        Goldilocks_neon::store(&output[i << 1], st[i]);
}

template<uint32_t SPONGE_WIDTH_T>
inline void Poseidon2Goldilocks<SPONGE_WIDTH_T>::compress_neon(
    Goldilocks::Element (&state)[CAPACITY],
    Goldilocks::Element const (&input)[SPONGE_WIDTH])
{
    Goldilocks::Element aux[SPONGE_WIDTH];
    permute_neon(aux, input);
    std::memcpy(state, aux, CAPACITY * sizeof(Goldilocks::Element));
}

#endif  // PIL2_HAS_NEON
#endif  // POSEIDON2_GOLDILOCKS_NEON
