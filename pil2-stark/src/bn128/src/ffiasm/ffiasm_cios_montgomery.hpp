#ifndef PIL2_FFIASM_CIOS_MONTGOMERY_HPP
#define PIL2_FFIASM_CIOS_MONTGOMERY_HPP

// Pure-C++ CIOS Montgomery arithmetic for the BN128 scalar field Fr.
//
// Provides drop-in replacements for the Fr_raw* assembly functions when
// building without __USE_ASSEMBLY__ (e.g. Darwin / arm64). On Linux this
// translation unit is excluded by the #ifndef __USE_ASSEMBLY__ guard so
// the asm symbols win.
//
// Public function signatures and the four primary constants
// (Fr_q, Fr_R3, Fr_rawq, Fr_rawR3) are declared in fr.hpp. This header
// only exposes the two CIOS-internal constants (R^2 and the Montgomery
// factor n0), which are useful to unit tests that probe the arithmetic
// directly.

#include "fr.hpp"

#ifndef __USE_ASSEMBLY__

// CIOS Montgomery factor: n0 = -p^-1 mod 2^64.
// Verified vs pil2-stark/src/bn128/src/ffiasm/fr.asm symbol `np`.
extern const uint64_t FR_N0;

// R^2 mod p, used to map values from natural form to Montgomery form via
//   Fr_rawToMontgomery(out, in) == Fr_rawMMul(out, in, R^2)
// Verified vs fr.asm symbol `R2`.
extern const uint64_t Fr_R2[Fr_N64];

#endif // __USE_ASSEMBLY__

#endif // PIL2_FFIASM_CIOS_MONTGOMERY_HPP
