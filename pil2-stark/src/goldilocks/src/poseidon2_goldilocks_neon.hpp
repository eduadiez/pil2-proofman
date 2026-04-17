#ifndef POSEIDON2_GOLDILOCKS_NEON
#define POSEIDON2_GOLDILOCKS_NEON

#include "platform.hpp"
#include "poseidon2_goldilocks.hpp"
#include "goldilocks_base_field.hpp"
#include "goldilocks_neon.hpp"

#if PIL2_HAS_NEON

#include <arm_neon.h>
#include <cstdint>

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

    // matmul_external punts to the scalar implementation: NEON-store,
    // scalar-call, NEON-load. Tried a full NEON M4 + cross-chunk-sum
    // vectorisation; the lane-shuffle overhead (vextq / vzip) made W=12 / W=16
    // ~14% slower vs the punt. Apple Silicon has 8 integer ALUs vs 4 NEON
    // ALUs, so scalar add chains have more parallel headroom than NEON gl_add
    // chains for this kind of cross-element add-heavy code. Revisit if a
    // chunked layout (lane 0 = chunk0 elem k, lane 1 = chunk1 elem k) is
    // adopted across the whole permute — that would let M4 run truly in
    // parallel across chunks without shuffles.
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

// ============================================================================
// NEON 2-sponge BATCH primitives (Part 5 Task 36).
//
// Layout: state[2 * SPONGE_WIDTH] = {sp0.x0..sp0.x{W-1}, sp1.x0..sp1.x{W-1}}
//   — two sponges back-to-back, mirroring AVX BATCH's 4-sponges contract
//   but with 2 sponges (NEON has 2 lanes per uint64x2_t vs AVX2's 4).
//
// Per-element regs: st[k] = {sp0.x_k, sp1.x_k}. With this layout, every
// Poseidon2 op (M4, pow7add, partial-round D-mul) becomes element-wise
// NEON across regs — NO lane shuffles within a reg, ever. This is the
// pattern that breaks the W=12/W=16 single-sponge regression: the matmul
// algebra (which mixes elements within a sponge) becomes adds across
// element-indexed regs, which NEON does in parallel for both sponges.
// ============================================================================

namespace Poseidon2Neon_batch {

// Strided load: gather lane 0 = base[0], lane 1 = base[stride]. Used to
// pick element_k from each of the 2 back-to-back sponges in `state[]`.
static inline uint64x2_t load_strided_2(const Goldilocks::Element* base, uint64_t stride) {
    uint64x2_t r = vsetq_lane_u64(base[0].fe, vdupq_n_u64(0), 0);
    return vsetq_lane_u64(base[stride].fe, r, 1);
}

static inline void store_strided_2(Goldilocks::Element* base, uint64_t stride, uint64x2_t v) {
    base[0].fe       = vgetq_lane_u64(v, 0);
    base[stride].fe  = vgetq_lane_u64(v, 1);
}

}  // namespace Poseidon2Neon_batch

template<uint32_t SPONGE_WIDTH_T>
inline void Poseidon2Goldilocks<SPONGE_WIDTH_T>::permute_batch_neon(
    Goldilocks::Element *state, const Goldilocks::Element *input)
{
    namespace N = Goldilocks_neon;
    namespace B = Poseidon2Neon_batch;

    // Per-width round constants (same arrays as scalar / single-sponge).
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

    constexpr uint32_t W = SPONGE_WIDTH;

    // Lambdas factor the batch primitives without polluting the class scope.
    auto matmul_m4_batch = [](uint64x2_t& s0, uint64x2_t& s1, uint64x2_t& s2, uint64x2_t& s3) {
        // Same M4 algebra as scalar matmul_m4_, but each "scalar add" is a
        // NEON gl_add — both sponges processed in parallel with no shuffle.
        uint64x2_t t0 = N::gl_add(s0, s1);
        uint64x2_t t1 = N::gl_add(s2, s3);
        uint64x2_t two_s1 = N::gl_add(s1, s1);
        uint64x2_t t2 = N::gl_add(two_s1, t1);
        uint64x2_t two_s3 = N::gl_add(s3, s3);
        uint64x2_t t3 = N::gl_add(two_s3, t0);
        uint64x2_t t1_2 = N::gl_add(t1, t1);
        uint64x2_t t0_2 = N::gl_add(t0, t0);
        uint64x2_t t4 = N::gl_add(N::gl_add(t1_2, t1_2), t3);
        uint64x2_t t5 = N::gl_add(N::gl_add(t0_2, t0_2), t2);
        uint64x2_t t6 = N::gl_add(t3, t5);
        uint64x2_t t7 = N::gl_add(t2, t4);
        s0 = t6;  s1 = t5;  s2 = t7;  s3 = t4;
    };

    auto matmul_external_batch = [&matmul_m4_batch](uint64x2_t* x) {
        for (uint32_t i = 0; i < W; i += 4)
            matmul_m4_batch(x[i], x[i + 1], x[i + 2], x[i + 3]);
        if constexpr (W > 4) {
            uint64x2_t stored[4];
            stored[0] = N::gl_add(x[0], x[4]);
            stored[1] = N::gl_add(x[1], x[5]);
            stored[2] = N::gl_add(x[2], x[6]);
            stored[3] = N::gl_add(x[3], x[7]);
            for (uint32_t i = 8; i < W; i += 4) {
                stored[0] = N::gl_add(stored[0], x[i]);
                stored[1] = N::gl_add(stored[1], x[i + 1]);
                stored[2] = N::gl_add(stored[2], x[i + 2]);
                stored[3] = N::gl_add(stored[3], x[i + 3]);
            }
            for (uint32_t i = 0; i < W; ++i)
                x[i] = N::gl_add(x[i], stored[i % 4]);
        }
    };

    // Fused (state + C)^7 element-wise across both sponges.
    auto pow7add_batch = [](uint64x2_t* x, const Goldilocks::Element C_[W]) {
        for (uint32_t i = 0; i < W; ++i) {
            uint64x2_t c  = N::splat(C_[i].fe);
            uint64x2_t s  = N::gl_add(x[i], c);
            uint64x2_t s2 = N::gl_square(s);
            uint64x2_t s4 = N::gl_square(s2);
            uint64x2_t s3 = N::gl_mul(s, s2);
            x[i] = N::gl_mul(s3, s4);
        }
    };

    auto element_pow7_batch = [](uint64x2_t& x) {
        uint64x2_t pw2 = N::gl_square(x);
        uint64x2_t pw4 = N::gl_square(pw2);
        uint64x2_t pw3 = N::gl_mul(x, pw2);
        x = N::gl_mul(pw3, pw4);
    };

    // ---- Load 2 sponges into W NEON regs (one reg per element index) ----
    std::memcpy(state, input, 2 * W * sizeof(Goldilocks::Element));
    uint64x2_t st[W];
    for (uint32_t i = 0; i < W; ++i)
        st[i] = B::load_strided_2(&state[i], W);

    // Initial M_E.
    matmul_external_batch(st);

    // First half full rounds.
    for (uint32_t r = 0; r < HALF_N_FULL_ROUNDS; ++r) {
        pow7add_batch(st, &C[r * W]);
        matmul_external_batch(st);
    }

    // Partial rounds — both sponges' state[0] live in lane 0 / 1 of st[0],
    // so element_pow7 across both lanes does both partial S-boxes at once.
    uint64x2_t d[W];
    for (uint32_t i = 0; i < W; ++i)
        d[i] = N::splat(D[i].fe);

    for (uint32_t r = 0; r < N_PARTIAL_ROUNDS; ++r) {
        uint64x2_t c = N::splat(C[HALF_N_FULL_ROUNDS * W + r].fe);
        st[0] = N::gl_add(st[0], c);
        element_pow7_batch(st[0]);
        uint64x2_t sum = N::splat(0);
        for (uint32_t i = 0; i < W; ++i)
            sum = N::gl_add(sum, st[i]);
        for (uint32_t i = 0; i < W; ++i) {
            st[i] = N::gl_mul(st[i], d[i]);
            st[i] = N::gl_add(st[i], sum);
        }
    }

    // Second half full rounds.
    for (uint32_t r = 0; r < HALF_N_FULL_ROUNDS; ++r) {
        pow7add_batch(st, &C[HALF_N_FULL_ROUNDS * W + N_PARTIAL_ROUNDS + r * W]);
        matmul_external_batch(st);
    }

    // Store result back to {sp0, sp1} consecutive layout.
    for (uint32_t i = 0; i < W; ++i)
        B::store_strided_2(&state[i], W, st[i]);
}

template<uint32_t SPONGE_WIDTH_T>
inline void Poseidon2Goldilocks<SPONGE_WIDTH_T>::compress_batch_neon(
    Goldilocks::Element (&state)[2 * CAPACITY],
    Goldilocks::Element const (&input)[2 * SPONGE_WIDTH])
{
    Goldilocks::Element aux[2 * SPONGE_WIDTH];
    permute_batch_neon(aux, input);
    // First CAPACITY elements of each permuted sponge.
    std::memcpy(&state[0],          &aux[0],            CAPACITY * sizeof(Goldilocks::Element));
    std::memcpy(&state[CAPACITY],   &aux[SPONGE_WIDTH], CAPACITY * sizeof(Goldilocks::Element));
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
