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

extern "C" void Fr_rawCopy(FrRawElement, const FrRawElement)                                          { Fr_cios_stub("Fr_rawCopy"); }
extern "C" void Fr_rawSwap(FrRawElement, FrRawElement)                                                { Fr_cios_stub("Fr_rawSwap"); }
extern "C" void Fr_rawAdd(FrRawElement, const FrRawElement, const FrRawElement)                       { Fr_cios_stub("Fr_rawAdd"); }
extern "C" void Fr_rawSub(FrRawElement, const FrRawElement, const FrRawElement)                       { Fr_cios_stub("Fr_rawSub"); }
extern "C" void Fr_rawNeg(FrRawElement, const FrRawElement)                                           { Fr_cios_stub("Fr_rawNeg"); }
extern "C" void Fr_rawMMul(FrRawElement, const FrRawElement, const FrRawElement)                      { Fr_cios_stub("Fr_rawMMul"); }
extern "C" void Fr_rawMSquare(FrRawElement, const FrRawElement)                                       { Fr_cios_stub("Fr_rawMSquare"); }
extern "C" void Fr_rawMMul1(FrRawElement, const FrRawElement, uint64_t)                               { Fr_cios_stub("Fr_rawMMul1"); }
extern "C" void Fr_rawToMontgomery(FrRawElement, const FrRawElement&)                                 { Fr_cios_stub("Fr_rawToMontgomery"); }
extern "C" void Fr_rawFromMontgomery(FrRawElement, const FrRawElement&)                               { Fr_cios_stub("Fr_rawFromMontgomery"); }
extern "C" int  Fr_rawIsEq(const FrRawElement, const FrRawElement)                                    { Fr_cios_stub("Fr_rawIsEq"); }
extern "C" int  Fr_rawIsZero(const FrRawElement)                                                      { Fr_cios_stub("Fr_rawIsZero"); }

#endif // __USE_ASSEMBLY__
