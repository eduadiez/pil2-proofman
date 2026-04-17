#include "ffiasm_cios_montgomery.hpp"

#ifndef __USE_ASSEMBLY__

#include <cstdint>
#include <cstdlib>
#include <iostream>

// ---------------------------------------------------------------------------
// BN128 scalar field Fr — Montgomery constants.
// Limb 0 is least significant. All values verified verbatim against
// pil2-stark/src/bn128/src/ffiasm/fr.asm.
// ---------------------------------------------------------------------------

FrElement Fr_q = {
    0,                  // shortVal
    Fr_LONG,            // type
    {0x43e1f593f0000001ULL, 0x2833e84879b97091ULL,
     0xb85045b68181585dULL, 0x30644e72e131a029ULL}
};

FrElement Fr_R3 = {
    0,
    Fr_LONG,
    {0x5e94d8e1b4bf0040ULL, 0x2a489cbe1cfbb6b8ULL,
     0x893cc664a19fcfedULL, 0x0cf8594b7fcc657cULL}
};

FrRawElement Fr_rawq = {
    0x43e1f593f0000001ULL, 0x2833e84879b97091ULL,
    0xb85045b68181585dULL, 0x30644e72e131a029ULL
};

FrRawElement Fr_rawR3 = {
    0x5e94d8e1b4bf0040ULL, 0x2a489cbe1cfbb6b8ULL,
    0x893cc664a19fcfedULL, 0x0cf8594b7fcc657cULL
};

const uint64_t FR_N0 = 0xc2e1f593efffffffULL;

const uint64_t Fr_R2[Fr_N64] = {
    0x1bb8e645ae216da7ULL, 0x53fe3ab1e35c59e3ULL,
    0x8c49833d53bb8085ULL, 0x0216d0b17f4e44a5ULL
};

// ---------------------------------------------------------------------------
// Stubbed raw operations — to be replaced with real CIOS arithmetic in
// subsequent commits (Tasks 17–21). Each one std::abort()s loudly so an
// inadvertent call surfaces immediately rather than producing garbage.
// ---------------------------------------------------------------------------

[[noreturn]] static void Fr_cios_stub(const char* fn) {
    std::cerr << fn << " not yet implemented in CIOS Montgomery." << std::endl;
    std::abort();
}

// ---- Trivial limb-level ops (no Montgomery arithmetic) -------------------

extern "C" void Fr_rawCopy(FrRawElement r, const FrRawElement a) {
    r[0] = a[0]; r[1] = a[1]; r[2] = a[2]; r[3] = a[3];
}

extern "C" void Fr_rawSwap(FrRawElement a, FrRawElement b) {
    for (int i = 0; i < Fr_N64; i++) {
        uint64_t tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

extern "C" int Fr_rawIsEq(const FrRawElement a, const FrRawElement b) {
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]) ? 1 : 0;
}

extern "C" int Fr_rawIsZero(const FrRawElement a) {
    return (a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0) ? 1 : 0;
}

// Additive inverse mod p. For canonical input a in [0, p), returns
// (p - a) mod p — i.e. 0 if a == 0, else p - a.
extern "C" void Fr_rawNeg(FrRawElement r, const FrRawElement a) {
    if (Fr_rawIsZero(a)) {
        r[0] = r[1] = r[2] = r[3] = 0;
        return;
    }
    // r = p - a as a 4-limb subtraction with borrow. a < p ensures no
    // borrow out of the MSB.
    int64_t borrow = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __int128 diff = (__int128)Fr_rawq[i] - (__int128)a[i] - borrow;
        if (diff < 0) {
            r[i] = (uint64_t)(diff + ((__int128)1 << 64));
            borrow = 1;
        } else {
            r[i] = (uint64_t)diff;
            borrow = 0;
        }
    }
}

// Modular addition. For canonical inputs a, b in [0, p) the 4-limb sum
// fits in 255 bits (since p < 2^254), so the carry out is always 0. The
// reduction is a single conditional subtract of p.
//
// Implementation strategy: compute the subtraction unconditionally and
// pick which result to keep based on the borrow flag — branch-light, no
// secret-dependent comparisons (constant-time is not a goal here, but
// the pattern is clean).
extern "C" void Fr_rawAdd(FrRawElement r, const FrRawElement a, const FrRawElement b) {
    uint64_t sum[Fr_N64];
    __uint128_t carry = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __uint128_t s = (__uint128_t)a[i] + b[i] + carry;
        sum[i] = (uint64_t)s;
        carry = s >> 64;
    }
    (void)carry;  // 0 for canonical inputs

    uint64_t diff[Fr_N64];
    int64_t borrow = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __int128 d = (__int128)sum[i] - Fr_rawq[i] - borrow;
        if (d < 0) {
            diff[i] = (uint64_t)(d + ((__int128)1 << 64));
            borrow = 1;
        } else {
            diff[i] = (uint64_t)d;
            borrow = 0;
        }
    }
    // borrow == 0 means sum >= p, use the reduced diff. Otherwise sum < p.
    Fr_rawCopy(r, borrow == 0 ? diff : sum);
}

// Modular subtraction. For canonical inputs a, b in [0, p) the 4-limb
// difference is in (-p, p); if it underflows (borrow out of MSB), add p
// back to land in [0, p).
extern "C" void Fr_rawSub(FrRawElement r, const FrRawElement a, const FrRawElement b) {
    uint64_t diff[Fr_N64];
    int64_t borrow = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __int128 d = (__int128)a[i] - b[i] - borrow;
        if (d < 0) {
            diff[i] = (uint64_t)(d + ((__int128)1 << 64));
            borrow = 1;
        } else {
            diff[i] = (uint64_t)d;
            borrow = 0;
        }
    }
    if (borrow == 0) {
        Fr_rawCopy(r, diff);
        return;
    }
    // Underflow: result is (a - b) + 2^256 in `diff`; adding p (mod 2^256)
    // recovers the canonical (a - b + p) since p < 2^256 and the high bit
    // cancels with the synthetic 2^256.
    __uint128_t carry = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __uint128_t s = (__uint128_t)diff[i] + Fr_rawq[i] + carry;
        r[i] = (uint64_t)s;
        carry = s >> 64;
    }
    (void)carry;  // discarded — represents the cancelling 2^256
}

// CIOS (Coarsely Integrated Operand Scanning) Montgomery multiplication.
// Reference: Koc, Acar, Kaliski, "Analyzing and Comparing Montgomery
// Multiplication Algorithms" (IEEE Micro, 1996), Algorithm CIOS.
//
// Computes r = a * b * R^-1 mod p, where R = 2^(64*N) = 2^256.
// Inputs a, b assumed in canonical [0, p). Result is canonical.
//
// The accumulator T is (N+2) limbs wide: N + 1 for the partial product
// and one extra to absorb the carry-out from the inner multiply-add.
extern "C" void Fr_rawMMul(FrRawElement r, const FrRawElement a, const FrRawElement b) {
    uint64_t T[Fr_N64 + 2] = {0, 0, 0, 0, 0, 0};

    for (int i = 0; i < Fr_N64; i++) {
        // Phase 1: T += a * b[i]
        uint64_t C = 0;
        for (int j = 0; j < Fr_N64; j++) {
            __uint128_t s = (__uint128_t)T[j] + (__uint128_t)a[j] * b[i] + C;
            T[j] = (uint64_t)s;
            C = (uint64_t)(s >> 64);
        }
        // Propagate the final carry into T[N] / T[N+1].
        __uint128_t s = (__uint128_t)T[Fr_N64] + C;
        T[Fr_N64]     = (uint64_t)s;
        T[Fr_N64 + 1] = (uint64_t)(s >> 64);

        // Phase 2: m = T[0] * n0 mod 2^64. This is the magic value that
        // makes T[0] + m*p[0] divisible by 2^64, clearing the low limb.
        uint64_t m = T[0] * FR_N0;  // implicit mod 2^64

        // Phase 3: T = (T + m*p) >> 64. The low limb of (T[0] + m*p[0])
        // is zero by construction, so we discard it and shift the rest
        // down by one word.
        s = (__uint128_t)T[0] + (__uint128_t)m * Fr_rawq[0];
        // (uint64_t)s == 0 here; only the carry matters.
        C = (uint64_t)(s >> 64);
        for (int j = 1; j < Fr_N64; j++) {
            s = (__uint128_t)T[j] + (__uint128_t)m * Fr_rawq[j] + C;
            T[j - 1] = (uint64_t)s;  // shifted-down store
            C = (uint64_t)(s >> 64);
        }
        s = (__uint128_t)T[Fr_N64] + C;
        T[Fr_N64 - 1] = (uint64_t)s;
        T[Fr_N64]     = T[Fr_N64 + 1] + (uint64_t)(s >> 64);
        T[Fr_N64 + 1] = 0;
    }

    // After N iterations T[0..N-1] is in [0, 2p) and T[N] is 0 or 1.
    // Compute T - p; if no borrow (i.e. T >= p) or T[N] was 1, use the
    // reduced value; otherwise keep T.
    uint64_t diff[Fr_N64];
    int64_t borrow = 0;
    for (int i = 0; i < Fr_N64; i++) {
        __int128 d = (__int128)T[i] - Fr_rawq[i] - borrow;
        if (d < 0) {
            diff[i] = (uint64_t)(d + ((__int128)1 << 64));
            borrow = 1;
        } else {
            diff[i] = (uint64_t)d;
            borrow = 0;
        }
    }
    const bool use_diff = (T[Fr_N64] != 0) || (borrow == 0);
    Fr_rawCopy(r, use_diff ? diff : T);
}

// Squaring is just MMul(a, a). A dedicated squarer would save ~25% (the
// off-diagonal limb products are computed twice in the multiply path),
// but correctness comes first; specialise later if it shows up in profiles.
extern "C" void Fr_rawMSquare(FrRawElement r, const FrRawElement a) {
    Fr_rawMMul(r, a, a);
}

// ---- Stubs still pending (Task 21) ---------------------------------------

extern "C" void Fr_rawMMul1(FrRawElement, const FrRawElement, uint64_t)                               { Fr_cios_stub("Fr_rawMMul1"); }
extern "C" void Fr_rawToMontgomery(FrRawElement, const FrRawElement&)                                 { Fr_cios_stub("Fr_rawToMontgomery"); }
extern "C" void Fr_rawFromMontgomery(FrRawElement, const FrRawElement&)                               { Fr_cios_stub("Fr_rawFromMontgomery"); }

#endif // __USE_ASSEMBLY__
