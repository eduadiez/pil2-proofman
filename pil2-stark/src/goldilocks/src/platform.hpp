#ifndef PIL2_PLATFORM_HPP
#define PIL2_PLATFORM_HPP

// Platform feature spine. Centralises the answer to "do we have feature X
// here?" so consumers can write `#if PIL2_HAS_AVX2` (always defined; usable
// in numeric contexts) instead of `#ifdef __AVX2__` (defined-or-not; easy
// to forget the negative branch).
//
// Every macro below is unconditionally defined to either 0 or 1.
//
// Usage:
//
//     #include "platform.hpp"
//
//     #if PIL2_HAS_AVX2
//         // AVX2 codepath
//     #elif PIL2_HAS_NEON
//         // NEON codepath
//     #else
//         // scalar fallback
//     #endif

// ----------------------------------------------------------------------------
// Architecture
// ----------------------------------------------------------------------------

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
  #define PIL2_ARCH_ARM64  1
#else
  #define PIL2_ARCH_ARM64  0
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
  #define PIL2_ARCH_X86_64 1
#else
  #define PIL2_ARCH_X86_64 0
#endif

// ----------------------------------------------------------------------------
// CPU SIMD
// ----------------------------------------------------------------------------

// NEON is mandatory on arm64; on 32-bit ARM it depends on -mfpu.
#if PIL2_ARCH_ARM64 || defined(__ARM_NEON)
  #define PIL2_HAS_NEON   1
#else
  #define PIL2_HAS_NEON   0
#endif

#if defined(__AVX2__)
  #define PIL2_HAS_AVX2   1
#else
  #define PIL2_HAS_AVX2   0
#endif

// __AVX512F__ is the standard compiler-defined macro for AVX-512 Foundation.
// __AVX512__ is a custom flag this codebase already uses (Linux Makefile
// defines it via -D when /proc/cpuinfo advertises avx512f). Accept either.
#if defined(__AVX512F__) || defined(__AVX512__)
  #define PIL2_HAS_AVX512 1
#else
  #define PIL2_HAS_AVX512 0
#endif

// ----------------------------------------------------------------------------
// GPU
// ----------------------------------------------------------------------------

// Metal is opt-in via the Cargo `metal` feature, wired through build.rs and
// the Makefile in Part 6. Until then, default to off so consumers can write
// `#if PIL2_HAS_METAL` without breaking pre-Part-6 builds.
#ifndef PIL2_HAS_METAL
  #define PIL2_HAS_METAL  0
#endif

#endif // PIL2_PLATFORM_HPP
