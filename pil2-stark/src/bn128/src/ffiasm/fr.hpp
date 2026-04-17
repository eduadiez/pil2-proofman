#ifndef __FR_H
#define __FR_H

#include <stdint.h>
#include <string>
#include <gmp.h>
#include <iostream>
#include <cassert>
#include <cstdlib>

#define Fr_N64 4
#define Fr_SHORT 0x00000000
#define Fr_LONG 0x80000000
#define Fr_LONGMONTGOMERY 0xC0000000
typedef uint64_t FrRawElement[Fr_N64];
typedef struct __attribute__((__packed__)) {
    int32_t shortVal;
    uint32_t type;
    FrRawElement longVal;
} FrElement;
typedef FrElement *PFrElement;

// ---------------------------------------------------------------------------
// Constants and raw operations are provided unconditionally:
//   - On Linux (__USE_ASSEMBLY__ defined) by fr.asm.
//   - On Darwin / arm64 (no asm) by ffiasm_cios_montgomery.cpp (CIOS).
// Both providers expose the exact same C ABI; only the implementation site
// changes per platform.
// ---------------------------------------------------------------------------

extern FrElement Fr_q;
extern FrElement Fr_R3;
extern FrRawElement Fr_rawq;
extern FrRawElement Fr_rawR3;

extern "C" void Fr_rawCopy(FrRawElement pRawResult, const FrRawElement pRawA);
extern "C" void Fr_rawSwap(FrRawElement pRawResult, FrRawElement pRawA);
extern "C" void Fr_rawAdd(FrRawElement pRawResult, const FrRawElement pRawA, const FrRawElement pRawB);
extern "C" void Fr_rawSub(FrRawElement pRawResult, const FrRawElement pRawA, const FrRawElement pRawB);
extern "C" void Fr_rawNeg(FrRawElement pRawResult, const FrRawElement pRawA);
extern "C" void Fr_rawMMul(FrRawElement pRawResult, const FrRawElement pRawA, const FrRawElement pRawB);
extern "C" void Fr_rawMSquare(FrRawElement pRawResult, const FrRawElement pRawA);
extern "C" void Fr_rawMMul1(FrRawElement pRawResult, const FrRawElement pRawA, uint64_t pRawB);
extern "C" void Fr_rawToMontgomery(FrRawElement pRawResult, const FrRawElement &pRawA);
extern "C" void Fr_rawFromMontgomery(FrRawElement pRawResult, const FrRawElement &pRawA);
extern "C" int Fr_rawIsEq(const FrRawElement pRawA, const FrRawElement pRawB);
extern "C" int Fr_rawIsZero(const FrRawElement pRawB);

// ---------------------------------------------------------------------------
// Non-raw FrElement-based operations remain platform-conditional. The asm
// implements them on Linux; on Darwin they stay unimplemented and abort
// loudly if reached (none of them lie on the aggregation/recursion path
// today, per the Part 2 Task 7 trace).
// ---------------------------------------------------------------------------

#ifdef __USE_ASSEMBLY__

extern "C" void Fr_copy(PFrElement r, PFrElement a);
extern "C" void Fr_copyn(PFrElement r, PFrElement a, int n);
extern "C" void Fr_add(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_sub(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_neg(PFrElement r, PFrElement a);
extern "C" void Fr_mul(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_square(PFrElement r, PFrElement a);
extern "C" void Fr_band(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_bor(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_bxor(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_bnot(PFrElement r, PFrElement a);
extern "C" void Fr_shl(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_shr(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_eq(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_neq(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_lt(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_gt(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_leq(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_geq(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_land(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_lor(PFrElement r, PFrElement a, PFrElement b);
extern "C" void Fr_lnot(PFrElement r, PFrElement a);
extern "C" void Fr_toNormal(PFrElement r, PFrElement a);
extern "C" void Fr_toLongNormal(PFrElement r, PFrElement a);
extern "C" void Fr_toMontgomery(PFrElement r, PFrElement a);

extern "C" int Fr_isTrue(PFrElement pE);
extern "C" int Fr_toInt(PFrElement pE);

extern "C" void Fr_fail();

#else

[[noreturn]] inline void Fr_unimplemented(const char* fn) {
    std::cerr << fn << " not implemented in C++ code "
                 "(build without __USE_ASSEMBLY__)." << std::endl;
    std::abort();
}

inline void Fr_copy(PFrElement r, PFrElement a)                                                      { Fr_unimplemented("Fr_copy"); }
inline void Fr_copyn(PFrElement r, PFrElement a, int n)                                              { Fr_unimplemented("Fr_copyn"); }
inline void Fr_add(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_add"); }
inline void Fr_sub(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_sub"); }
inline void Fr_neg(PFrElement r, PFrElement a)                                                       { Fr_unimplemented("Fr_neg"); }
inline void Fr_mul(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_mul"); }
inline void Fr_square(PFrElement r, PFrElement a)                                                    { Fr_unimplemented("Fr_square"); }
inline void Fr_band(PFrElement r, PFrElement a, PFrElement b)                                        { Fr_unimplemented("Fr_band"); }
inline void Fr_bor(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_bor"); }
inline void Fr_bxor(PFrElement r, PFrElement a, PFrElement b)                                        { Fr_unimplemented("Fr_bxor"); }
inline void Fr_bnot(PFrElement r, PFrElement a)                                                      { Fr_unimplemented("Fr_bnot"); }
inline void Fr_shl(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_shl"); }
inline void Fr_shr(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_shr"); }
inline void Fr_eq(PFrElement r, PFrElement a, PFrElement b)                                          { Fr_unimplemented("Fr_eq"); }
inline void Fr_neq(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_neq"); }
inline void Fr_lt(PFrElement r, PFrElement a, PFrElement b)                                          { Fr_unimplemented("Fr_lt"); }
inline void Fr_gt(PFrElement r, PFrElement a, PFrElement b)                                          { Fr_unimplemented("Fr_gt"); }
inline void Fr_leq(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_leq"); }
inline void Fr_geq(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_geq"); }
inline void Fr_land(PFrElement r, PFrElement a, PFrElement b)                                        { Fr_unimplemented("Fr_land"); }
inline void Fr_lor(PFrElement r, PFrElement a, PFrElement b)                                         { Fr_unimplemented("Fr_lor"); }
inline void Fr_lnot(PFrElement r, PFrElement a)                                                      { Fr_unimplemented("Fr_lnot"); }
inline void Fr_toNormal(PFrElement r, PFrElement a)                                                  { Fr_unimplemented("Fr_toNormal"); }
inline void Fr_toLongNormal(PFrElement r, PFrElement a)                                              { Fr_unimplemented("Fr_toLongNormal"); }
inline void Fr_toMontgomery(PFrElement r, PFrElement a)                                              { Fr_unimplemented("Fr_toMontgomery"); }

inline int Fr_isTrue(PFrElement pE)                                                                  { Fr_unimplemented("Fr_isTrue"); }
inline int Fr_toInt(PFrElement pE)                                                                   { Fr_unimplemented("Fr_toInt"); }

inline void Fr_fail()                                                                                { Fr_unimplemented("Fr_fail"); }

#endif // __USE_ASSEMBLY__


// Pending functions to convert

void Fr_str2element(PFrElement pE, char const*s);
void Fr_str2element(PFrElement pE, char const *s, unsigned int base);
char *Fr_element2str(PFrElement pE);
void Fr_idiv(PFrElement r, PFrElement a, PFrElement b);
void Fr_mod(PFrElement r, PFrElement a, PFrElement b);
void Fr_inv(PFrElement r, PFrElement a);
void Fr_div(PFrElement r, PFrElement a, PFrElement b);
void Fr_pow(PFrElement r, PFrElement a, PFrElement b);

class RawFr {

public:
    const static int N64 = Fr_N64;
    const static int MaxBits = 254;


    struct Element {
        FrRawElement v;
    };

private:
    Element fZero;
    Element fOne;
    Element fNegOne;

public:

    RawFr();
    ~RawFr();

    const Element &zero() { return fZero; };
    const Element &one() { return fOne; };
    const Element &negOne() { return fNegOne; };
    Element set(int value);
    void set(Element &r, int value);

    void fromString(Element &r, const std::string &n, uint32_t radix = 10);
    std::string toString(const Element &a, uint32_t radix = 10);

    void inline copy(Element &r, const Element &a) { Fr_rawCopy(r.v, a.v); };
    void inline swap(Element &a, Element &b) { Fr_rawSwap(a.v, b.v); };
    void inline add(Element &r, const Element &a, const Element &b) { Fr_rawAdd(r.v, a.v, b.v); };
    void inline sub(Element &r, const Element &a, const Element &b) { Fr_rawSub(r.v, a.v, b.v); };
    void inline mul(Element &r, const Element &a, const Element &b) { Fr_rawMMul(r.v, a.v, b.v); };

    Element inline add(const Element &a, const Element &b) { Element r; Fr_rawAdd(r.v, a.v, b.v); return r;};
    Element inline sub(const Element &a, const Element &b) { Element r; Fr_rawSub(r.v, a.v, b.v); return r;};
    Element inline mul(const Element &a, const Element &b) { Element r; Fr_rawMMul(r.v, a.v, b.v); return r;};

    Element inline neg(const Element &a) { Element r; Fr_rawNeg(r.v, a.v); return r; };
    Element inline square(const Element &a) { Element r; Fr_rawMSquare(r.v, a.v); return r; };

    Element inline add(int a, const Element &b) { return add(set(a), b);};
    Element inline sub(int a, const Element &b) { return sub(set(a), b);};
    Element inline mul(int a, const Element &b) { return mul(set(a), b);};

    Element inline add(const Element &a, int b) { return add(a, set(b));};
    Element inline sub(const Element &a, int b) { return sub(a, set(b));};
    Element inline mul(const Element &a, int b) { return mul(a, set(b));};
    
    void inline mul1(Element &r, const Element &a, uint64_t b) { Fr_rawMMul1(r.v, a.v, b); };
    void inline neg(Element &r, const Element &a) { Fr_rawNeg(r.v, a.v); };
    void inline square(Element &r, const Element &a) { Fr_rawMSquare(r.v, a.v); };
    void inv(Element &r, const Element &a);
    void div(Element &r, const Element &a, const Element &b);
    void exp(Element &r, const Element &base, uint8_t* scalar, unsigned int scalarSize);

    void inline toMontgomery(Element &r, const Element &a) { Fr_rawToMontgomery(r.v, a.v); };
    void inline fromMontgomery(Element &r, const Element &a) { Fr_rawFromMontgomery(r.v, a.v); };
    int inline eq(const Element &a, const Element &b) { return Fr_rawIsEq(a.v, b.v); };
    int inline isZero(const Element &a) { return Fr_rawIsZero(a.v); };

    void toMpz(mpz_t r, const Element &a);
    void fromMpz(Element &a, const mpz_t r);

    int toRprBE(const Element &element, uint8_t *data, int bytes);
    int fromRprBE(Element &element, const uint8_t *data, int bytes);
    int fromRprLE(Element &element, const uint8_t *data, int bytes);
    int toRprLE(const Element &element, uint8_t *data, int bytes);
    
    int bytes ( void ) { return Fr_N64 * 8; };
    
    void fromUI(Element &r, unsigned long int v);

    static RawFr field;

};


#endif // __FR_H


