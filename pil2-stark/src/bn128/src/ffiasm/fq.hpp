#ifndef __FQ_H
#define __FQ_H

#include <stdint.h>
#include <string>
#include <gmp.h>
#include <iostream>
#include <cassert>
#include <cstdlib>

#define Fq_N64 4
#define Fq_SHORT 0x00000000
#define Fq_LONG 0x80000000
#define Fq_LONGMONTGOMERY 0xC0000000
typedef uint64_t FqRawElement[Fq_N64];
typedef struct __attribute__((__packed__)) {
    int32_t shortVal;
    uint32_t type;
    FqRawElement longVal;
} FqElement;
typedef FqElement *PFqElement;

#ifdef __USE_ASSEMBLY__
extern FqElement Fq_q;
extern FqElement Fq_R3;
extern FqRawElement Fq_rawq;
extern FqRawElement Fq_rawR3;

extern "C" void Fq_copy(PFqElement r, PFqElement a);
extern "C" void Fq_copyn(PFqElement r, PFqElement a, int n);
extern "C" void Fq_add(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_sub(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_neg(PFqElement r, PFqElement a);
extern "C" void Fq_mul(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_square(PFqElement r, PFqElement a);
extern "C" void Fq_band(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_bor(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_bxor(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_bnot(PFqElement r, PFqElement a);
extern "C" void Fq_shl(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_shr(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_eq(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_neq(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_lt(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_gt(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_leq(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_geq(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_land(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_lor(PFqElement r, PFqElement a, PFqElement b);
extern "C" void Fq_lnot(PFqElement r, PFqElement a);
extern "C" void Fq_toNormal(PFqElement r, PFqElement a);
extern "C" void Fq_toLongNormal(PFqElement r, PFqElement a);
extern "C" void Fq_toMontgomery(PFqElement r, PFqElement a);

extern "C" int Fq_isTrue(PFqElement pE);
extern "C" int Fq_toInt(PFqElement pE);

extern "C" void Fq_rawCopy(FqRawElement pRawResult, const FqRawElement pRawA);
extern "C" void Fq_rawSwap(FqRawElement pRawResult, FqRawElement pRawA);
extern "C" void Fq_rawAdd(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB);
extern "C" void Fq_rawSub(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB);
extern "C" void Fq_rawNeg(FqRawElement pRawResult, const FqRawElement pRawA);
extern "C" void Fq_rawMMul(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB);
extern "C" void Fq_rawMSquare(FqRawElement pRawResult, const FqRawElement pRawA);
extern "C" void Fq_rawMMul1(FqRawElement pRawResult, const FqRawElement pRawA, uint64_t pRawB);
extern "C" void Fq_rawToMontgomery(FqRawElement pRawResult, const FqRawElement &pRawA);
extern "C" void Fq_rawFromMontgomery(FqRawElement pRawResult, const FqRawElement &pRawA);
extern "C" int Fq_rawIsEq(const FqRawElement pRawA, const FqRawElement pRawB);
extern "C" int Fq_rawIsZero(const FqRawElement pRawB);

extern "C" void FqP_fail();
#else
static FqElement Fq_q;
static FqElement Fq_R3;
static FqRawElement Fq_rawq;
static FqRawElement Fq_rawR3;

// Stubs for builds without __USE_ASSEMBLY__ (e.g. Darwin/arm64 today).
// Calling any of these is a programmer error: the bn128 path is not
// wired on this platform yet. Abort unconditionally so that misroutes
// surface immediately instead of silently corrupting the proof.
[[noreturn]] inline void Fq_unimplemented(const char* fn) {
    std::cerr << fn << " not implemented in C++ code "
                 "(build without __USE_ASSEMBLY__)." << std::endl;
    std::abort();
}

inline void Fq_copy(PFqElement r, PFqElement a)                                                       { Fq_unimplemented("Fq_copy"); }
inline void Fq_copyn(PFqElement r, PFqElement a, int n)                                               { Fq_unimplemented("Fq_copyn"); }
inline void Fq_add(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_add"); }
inline void Fq_sub(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_sub"); }
inline void Fq_neg(PFqElement r, PFqElement a)                                                        { Fq_unimplemented("Fq_neg"); }
inline void Fq_mul(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_mul"); }
inline void Fq_square(PFqElement r, PFqElement a)                                                     { Fq_unimplemented("Fq_square"); }
inline void Fq_band(PFqElement r, PFqElement a, PFqElement b)                                         { Fq_unimplemented("Fq_band"); }
inline void Fq_bor(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_bor"); }
inline void Fq_bxor(PFqElement r, PFqElement a, PFqElement b)                                         { Fq_unimplemented("Fq_bxor"); }
inline void Fq_bnot(PFqElement r, PFqElement a)                                                       { Fq_unimplemented("Fq_bnot"); }
inline void Fq_shl(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_shl"); }
inline void Fq_shr(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_shr"); }
inline void Fq_eq(PFqElement r, PFqElement a, PFqElement b)                                           { Fq_unimplemented("Fq_eq"); }
inline void Fq_neq(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_neq"); }
inline void Fq_lt(PFqElement r, PFqElement a, PFqElement b)                                           { Fq_unimplemented("Fq_lt"); }
inline void Fq_gt(PFqElement r, PFqElement a, PFqElement b)                                           { Fq_unimplemented("Fq_gt"); }
inline void Fq_leq(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_leq"); }
inline void Fq_geq(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_geq"); }
inline void Fq_land(PFqElement r, PFqElement a, PFqElement b)                                         { Fq_unimplemented("Fq_land"); }
inline void Fq_lor(PFqElement r, PFqElement a, PFqElement b)                                          { Fq_unimplemented("Fq_lor"); }
inline void Fq_lnot(PFqElement r, PFqElement a)                                                       { Fq_unimplemented("Fq_lnot"); }
inline void Fq_toNormal(PFqElement r, PFqElement a)                                                   { Fq_unimplemented("Fq_toNormal"); }
inline void Fq_toLongNormal(PFqElement r, PFqElement a)                                               { Fq_unimplemented("Fq_toLongNormal"); }
inline void Fq_toMontgomery(PFqElement r, PFqElement a)                                               { Fq_unimplemented("Fq_toMontgomery"); }

inline int Fq_isTrue(PFqElement pE)                                                                   { Fq_unimplemented("Fq_isTrue"); }
inline int Fq_toInt(PFqElement pE)                                                                    { Fq_unimplemented("Fq_toInt"); }

inline void Fq_rawCopy(FqRawElement pRawResult, const FqRawElement pRawA)                             { Fq_unimplemented("Fq_rawCopy"); }
inline void Fq_rawSwap(FqRawElement pRawResult, FqRawElement pRawA)                                   { Fq_unimplemented("Fq_rawSwap"); }
inline void Fq_rawAdd(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB)    { Fq_unimplemented("Fq_rawAdd"); }
inline void Fq_rawSub(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB)    { Fq_unimplemented("Fq_rawSub"); }
inline void Fq_rawNeg(FqRawElement pRawResult, const FqRawElement pRawA)                              { Fq_unimplemented("Fq_rawNeg"); }
inline void Fq_rawMMul(FqRawElement pRawResult, const FqRawElement pRawA, const FqRawElement pRawB)   { Fq_unimplemented("Fq_rawMMul"); }
inline void Fq_rawMSquare(FqRawElement pRawResult, const FqRawElement pRawA)                          { Fq_unimplemented("Fq_rawMSquare"); }
inline void Fq_rawMMul1(FqRawElement pRawResult, const FqRawElement pRawA, uint64_t pRawB)            { Fq_unimplemented("Fq_rawMMul1"); }
inline void Fq_rawToMontgomery(FqRawElement pRawResult, const FqRawElement &pRawA)                    { Fq_unimplemented("Fq_rawToMontgomery"); }
inline void Fq_rawFromMontgomery(FqRawElement pRawResult, const FqRawElement &pRawA)                  { Fq_unimplemented("Fq_rawFromMontgomery"); }
inline int Fq_rawIsEq(const FqRawElement pRawA, const FqRawElement pRawB)                             { Fq_unimplemented("Fq_rawIsEq"); }
inline int Fq_rawIsZero(const FqRawElement pRawB)                                                     { Fq_unimplemented("Fq_rawIsZero"); }
inline void FqP_fail()                                                                                { Fq_unimplemented("FqP_fail"); }
#endif


// Pending functions to convert

void FqP_str2element(PFqElement pE, char const*s);
char *FqP_element2str(PFqElement pE);
void FqP_idiv(PFqElement r, PFqElement a, PFqElement b);
void FqP_mod(PFqElement r, PFqElement a, PFqElement b);
void FqP_inv(PFqElement r, PFqElement a);
void FqP_div(PFqElement r, PFqElement a, PFqElement b);
void FqP_pow(PFqElement r, PFqElement a, PFqElement b);

class RawFq {

public:
    const static int N64 = Fq_N64;
    const static int MaxBits = 254;


    struct Element {
        FqRawElement v;
    };

private:
    Element fZero;
    Element fOne;
    Element fNegOne;

public:

    RawFq();
    ~RawFq();

    const Element &zero() { return fZero; };
    const Element &one() { return fOne; };
    const Element &negOne() { return fNegOne; };
    Element set(int value);
    void set(Element &r, int value);

    void fromString(Element &r, const std::string &n, uint32_t radix = 10);
    std::string toString(const Element &a, uint32_t radix = 10);

    void inline copy(Element &r, const Element &a) { Fq_rawCopy(r.v, a.v); };
    void inline swap(Element &a, Element &b) { Fq_rawSwap(a.v, b.v); };
    void inline add(Element &r, const Element &a, const Element &b) { Fq_rawAdd(r.v, a.v, b.v); };
    void inline sub(Element &r, const Element &a, const Element &b) { Fq_rawSub(r.v, a.v, b.v); };
    void inline mul(Element &r, const Element &a, const Element &b) { Fq_rawMMul(r.v, a.v, b.v); };

    Element inline add(const Element &a, const Element &b) { Element r; Fq_rawAdd(r.v, a.v, b.v); return r;};
    Element inline sub(const Element &a, const Element &b) { Element r; Fq_rawSub(r.v, a.v, b.v); return r;};
    Element inline mul(const Element &a, const Element &b) { Element r; Fq_rawMMul(r.v, a.v, b.v); return r;};

    Element inline neg(const Element &a) { Element r; Fq_rawNeg(r.v, a.v); return r; };
    Element inline square(const Element &a) { Element r; Fq_rawMSquare(r.v, a.v); return r; };

    Element inline add(int a, const Element &b) { return add(set(a), b);};
    Element inline sub(int a, const Element &b) { return sub(set(a), b);};
    Element inline mul(int a, const Element &b) { return mul(set(a), b);};

    Element inline add(const Element &a, int b) { return add(a, set(b));};
    Element inline sub(const Element &a, int b) { return sub(a, set(b));};
    Element inline mul(const Element &a, int b) { return mul(a, set(b));};
    
    void inline mul1(Element &r, const Element &a, uint64_t b) { Fq_rawMMul1(r.v, a.v, b); };
    void inline neg(Element &r, const Element &a) { Fq_rawNeg(r.v, a.v); };
    void inline square(Element &r, const Element &a) { Fq_rawMSquare(r.v, a.v); };
    void inv(Element &r, const Element &a);
    void div(Element &r, const Element &a, const Element &b);
    void exp(Element &r, const Element &base, uint8_t* scalar, unsigned int scalarSize);

    void inline toMontgomery(Element &r, const Element &a) { Fq_rawToMontgomery(r.v, a.v); };
    void inline fromMontgomery(Element &r, const Element &a) { Fq_rawFromMontgomery(r.v, a.v); };
    int inline eq(const Element &a, const Element &b) { return Fq_rawIsEq(a.v, b.v); };
    int inline isZero(const Element &a) { return Fq_rawIsZero(a.v); };

    void toMpz(mpz_t r, const Element &a);
    void fromMpz(Element &a, const mpz_t r);

    int toRprBE(const Element &element, uint8_t *data, int bytes);
    int fromRprBE(Element &element, const uint8_t *data, int bytes);
    
    int toRprLE(const Element &element, uint8_t *data, int bytes);
    int fromRprLE(Element &element, const uint8_t *data, int bytes);
    
    int bytes ( void ) { return Fq_N64 * 8; };
    
    void fromUI(Element &r, unsigned long int v);

    static RawFq field;

};


#endif // __FQ_H



