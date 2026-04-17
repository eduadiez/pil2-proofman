// Bit-exact tests for the pure-C++ CIOS Montgomery fallback used on Darwin
// when __USE_ASSEMBLY__ is not defined. Oracle is GMP (mpz_class), which is
// available everywhere this codebase already builds.
//
// On Linux (__USE_ASSEMBLY__ defined) the asm is the real implementation
// and this file's tests are skipped — the asm is the oracle Linux trusts,
// and re-checking it against itself adds no signal.

#include <gtest/gtest.h>

#ifndef __USE_ASSEMBLY__

#include <cstdint>
#include <gmp.h>
#include <gmpxx.h>

#include "fr.hpp"
#include "ffiasm_cios_montgomery.hpp"

namespace {

// limbs (LSB first, 4 × u64) -> mpz_class. Avoids ambiguity between
// gmpxx's unsigned-long and unsigned-long-long ctors on Apple Silicon.
mpz_class to_mpz(const uint64_t* limbs) {
    mpz_class v;
    mpz_import(v.get_mpz_t(), Fr_N64, /*order=*/-1,
               sizeof(uint64_t), /*endian=*/0, /*nails=*/0, limbs);
    return v;
}

mpz_class fr_modulus() { return to_mpz(Fr_rawq); }

// Pack a non-negative mpz_class < 2^256 into 4 little-endian u64 limbs.
// Zero-fills high limbs if v has fewer than 4 limbs of magnitude.
void from_mpz(FrRawElement r, const mpz_class& v) {
    for (int i = 0; i < Fr_N64; i++) r[i] = 0;
    size_t count = 0;
    mpz_export(r, &count, /*order=*/-1,
               sizeof(uint64_t), /*endian=*/0, /*nails=*/0, v.get_mpz_t());
    // count <= Fr_N64 by precondition; remaining limbs already zero.
}

// Canonical sample values exercising boundaries: 0, 1, small, p/2, p-1,
// and a large pseudo-random looking constant.
mpz_class sample(int i) {
    mpz_class p = fr_modulus();
    switch (i) {
        case 0: return mpz_class(0);
        case 1: return mpz_class(1);
        case 2: return mpz_class(2);
        case 3: return mpz_class("123456789");
        case 4: return p / 2;
        case 5: return p - 1;
        case 6: return mpz_class("31415926535897932384626433832795028841971693993");
        default: return mpz_class(i);
    }
}

constexpr int N_SAMPLES = 7;

}  // namespace

TEST(Fr_constants, prime_limbs_match_known_BN128_scalar_modulus) {
    // BN128 scalar prime: 21888242871839275222246405745257275088548364400416034343698204186575808495617
    mpz_class expected("21888242871839275222246405745257275088548364400416034343698204186575808495617");
    EXPECT_EQ(fr_modulus(), expected);
}

TEST(Fr_rawCopy, copies_all_limbs) {
    for (int i = 0; i < N_SAMPLES; i++) {
        FrRawElement a, r;
        from_mpz(a, sample(i));
        Fr_rawCopy(r, a);
        EXPECT_EQ(to_mpz(r), to_mpz(a)) << "sample " << i;
    }
}

TEST(Fr_rawSwap, exchanges_both_args) {
    for (int i = 0; i < N_SAMPLES; i++) {
        FrRawElement a, b, a_orig, b_orig;
        from_mpz(a, sample(i));
        from_mpz(b, sample((i + 3) % N_SAMPLES));
        Fr_rawCopy(a_orig, a);
        Fr_rawCopy(b_orig, b);
        Fr_rawSwap(a, b);
        EXPECT_EQ(to_mpz(a), to_mpz(b_orig)) << "sample " << i;
        EXPECT_EQ(to_mpz(b), to_mpz(a_orig)) << "sample " << i;
    }
}

TEST(Fr_rawIsEq, returns_1_for_equal_0_for_unequal) {
    for (int i = 0; i < N_SAMPLES; i++) {
        FrRawElement a, b;
        from_mpz(a, sample(i));
        from_mpz(b, sample(i));
        EXPECT_EQ(Fr_rawIsEq(a, b), 1) << "equal sample " << i;
        if (sample(i) != sample(0)) {
            from_mpz(b, sample(0));
            EXPECT_EQ(Fr_rawIsEq(a, b), 0) << "unequal sample " << i;
        }
    }
}

TEST(Fr_rawIsZero, only_returns_1_for_zero) {
    for (int i = 0; i < N_SAMPLES; i++) {
        FrRawElement a;
        from_mpz(a, sample(i));
        const int expected = (sample(i) == 0) ? 1 : 0;
        EXPECT_EQ(Fr_rawIsZero(a), expected) << "sample " << i;
    }
}

TEST(Fr_rawNeg, computes_p_minus_a_mod_p) {
    const mpz_class p = fr_modulus();
    for (int i = 0; i < N_SAMPLES; i++) {
        FrRawElement a, r;
        const mpz_class va = sample(i);
        from_mpz(a, va);
        Fr_rawNeg(r, a);
        const mpz_class expected = (p - va) % p;
        EXPECT_EQ(to_mpz(r), expected) << "sample " << i;
    }
}

#endif  // __USE_ASSEMBLY__
