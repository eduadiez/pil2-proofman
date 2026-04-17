#include <benchmark/benchmark.h>
#include "../src/goldilocks_base_field.hpp"
#include "../src/goldilocks_base_field_pack.hpp"
#include "../src/goldilocks_cubic_extension.hpp"
#include "../src/goldilocks_cubic_extension_pack.hpp"
#include "../src/platform.hpp"
#if PIL2_HAS_NEON
#include "../src/goldilocks_neon.hpp"
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif
#include "omp.h"

static void ADD_OP_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0 = Goldilocks::zero(), term1 = Goldilocks::zero(), term2 = Goldilocks::zero();
    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        ;
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::add(term2, term0, term1);
            term0 = term1;
            term1 = term2;
        }
    }
    assert(Goldilocks::toU64(term2) == 9315693631868018522ULL);
}
static void SUB_OP_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0 = Goldilocks::zero(), term1 = Goldilocks::zero(), term2 = Goldilocks::zero();
    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        ;
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::sub(term2, term0, term1);
            term0 = term1;
            term1 = term2;
        }
    }
    assert(Goldilocks::toU64(term2) == 17916187359919173389ULL);
}
static void MUL_OP_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0 = Goldilocks::zero(), term1 = Goldilocks::zero(), term2 = Goldilocks::zero();
    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        ;
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::mul(term2, term0, term1);
            term0 = term1;
            term1 = term2;
        }
    }
    assert(Goldilocks::toU64(term2) == 1922281271747280077ULL);
}
static void INV_OP_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0 = Goldilocks::zero(), term1 = Goldilocks::zero();
    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();

        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::inv(term0, term1);
            term1 = term0;
        }
    }
    assert(Goldilocks::toU64(term0) == 3ULL);
}

#ifdef __AVX2__
static void ADD_OP_AVX_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0, term1;
    __m256i term0_, term1_, term2_ = _mm256_setzero_si256();

    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        Goldilocks::set_avx(term0_, term0, term0, term0, term0);
        Goldilocks::set_avx(term1_, term1, term1, term1, term1);
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::add_avx(term2_, term0_, term1_);
            term0_ = term1_;
            term1_ = term2_;
        }
    }
    Goldilocks::Element res[4];
    Goldilocks::store_avx(res, term2_);
    assert(Goldilocks::toU64(res[0]) == 9315693631868018522ULL);
}
static void SUB_OP_AVX_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0, term1;
    __m256i term0_, term1_, term2_ = _mm256_setzero_si256();

    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        Goldilocks::set_avx(term0_, term0, term0, term0, term0);
        Goldilocks::set_avx(term1_, term1, term1, term1, term1);
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::sub_avx(term2_, term0_, term1_);
            term0_ = term1_;
            term1_ = term2_;
        }
    }
    Goldilocks::Element res[4];
    Goldilocks::store_avx(res, term2_);
    assert(Goldilocks::toU64(res[0]) == 17916187359919173389ULL);

}
static void MUL_OP_AVX_BENCH(benchmark::State &state)
{
    Goldilocks::Element term0, term1;
    __m256i term0_, term1_, term2_ = _mm256_setzero_si256();

    // Benchmark
    for (auto _ : state)
    {
        term0 = Goldilocks::one() + Goldilocks::one();
        term1 = Goldilocks::one() + Goldilocks::one() + Goldilocks::one();
        Goldilocks::set_avx(term0_, term0, term0, term0, term0);
        Goldilocks::set_avx(term1_, term1, term1, term1, term1);
        for (uint64_t i = 0; i < 1000000; i++)
        {
            Goldilocks::mult_avx(term2_, term0_, term1_);
            term0_ = term1_;
            term1_ = term2_;
        }
    }
    Goldilocks::Element res[4];
    Goldilocks::store_avx(res, term2_);
    assert(Goldilocks::toU64(res[0]) == 1922281271747280077ULL);
}
#endif

#ifdef __AVX2__
BENCHMARK(ADD_OP_AVX_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(SUB_OP_AVX_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(MUL_OP_AVX_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();
#endif
   
BENCHMARK(ADD_OP_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();
BENCHMARK(SUB_OP_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();
BENCHMARK(MUL_OP_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(INV_OP_BENCH)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

// ---- op_pack microbenches (STARK expression-evaluator hot path) ----
// NROWS_PACK=128 rows per call; run many calls per iteration to amortize loop.
static constexpr uint64_t OP_PACK_N = 128;
static constexpr uint64_t OP_PACK_CALLS = 10000;

static void OP_PACK_ADD_BENCH(benchmark::State &state)
{
    alignas(16) Goldilocks::Element a[OP_PACK_N], b[OP_PACK_N], c[OP_PACK_N];
    for (uint64_t i = 0; i < OP_PACK_N; ++i) { a[i].fe = i * 3 + 1; b[i].fe = i * 5 + 7; }
    for (auto _ : state) {
        for (uint64_t k = 0; k < OP_PACK_CALLS; ++k) {
            Goldilocks::op_pack(OP_PACK_N, 0, c, a, false, b, false);
            benchmark::DoNotOptimize(c);
        }
    }
}
static void OP_PACK_SUB_BENCH(benchmark::State &state)
{
    alignas(16) Goldilocks::Element a[OP_PACK_N], b[OP_PACK_N], c[OP_PACK_N];
    for (uint64_t i = 0; i < OP_PACK_N; ++i) { a[i].fe = i * 3 + 1; b[i].fe = i * 5 + 7; }
    for (auto _ : state) {
        for (uint64_t k = 0; k < OP_PACK_CALLS; ++k) {
            Goldilocks::op_pack(OP_PACK_N, 1, c, a, false, b, false);
            benchmark::DoNotOptimize(c);
        }
    }
}
static void OP_PACK_MUL_BENCH(benchmark::State &state)
{
    alignas(16) Goldilocks::Element a[OP_PACK_N], b[OP_PACK_N], c[OP_PACK_N];
    for (uint64_t i = 0; i < OP_PACK_N; ++i) { a[i].fe = i * 3 + 1; b[i].fe = i * 5 + 7; }
    for (auto _ : state) {
        for (uint64_t k = 0; k < OP_PACK_CALLS; ++k) {
            Goldilocks::op_pack(OP_PACK_N, 2, c, a, false, b, false);
            benchmark::DoNotOptimize(c);
        }
    }
}

BENCHMARK(OP_PACK_ADD_BENCH)->Unit(benchmark::kMicrosecond)->UseRealTime();
BENCHMARK(OP_PACK_SUB_BENCH)->Unit(benchmark::kMicrosecond)->UseRealTime();
BENCHMARK(OP_PACK_MUL_BENCH)->Unit(benchmark::kMicrosecond)->UseRealTime();

// ---- Goldilocks3 (cubic extension) op_pack microbench ----
// Each row does 6 field muls + ~12 adds/subs — heavier than base field op_pack.
// Layout: a[0..N), a[N..2N), a[2N..3N) hold the 3 coordinates; same for b.
static void OP_PACK_CUBIC_MUL_BENCH(benchmark::State &state)
{
    alignas(16) Goldilocks::Element a[3 * OP_PACK_N], b[3 * OP_PACK_N], c[3 * OP_PACK_N];
    for (uint64_t i = 0; i < 3 * OP_PACK_N; ++i) { a[i].fe = i * 3 + 1; b[i].fe = i * 5 + 7; }
    for (auto _ : state) {
        for (uint64_t k = 0; k < OP_PACK_CALLS; ++k) {
            Goldilocks3::op_pack(OP_PACK_N, 2, c, a, false, b, false);
            benchmark::DoNotOptimize(c);
        }
    }
}
BENCHMARK(OP_PACK_CUBIC_MUL_BENCH)->Unit(benchmark::kMicrosecond)->UseRealTime();

#if PIL2_HAS_NEON
// Chained mul: each iter depends on the previous (FIB-like).
// Scalar MUL_OP_BENCH does 1 mul per iter with dep chain → 1 mul per cycle
// best case. NEON version keeps the same chain but computes 2 independent
// chains in the 2 lanes → should be ~2× throughput if NEON really delivers
// lane-parallelism when OoO can't help.
static void MUL_OP_NEON_BENCH(benchmark::State &state)
{
    for (auto _ : state)
    {
        uint64x2_t term0 = vdupq_n_u64(2);
        uint64x2_t term1 = vdupq_n_u64(3);
        uint64x2_t term2 = vdupq_n_u64(0);
        for (uint64_t i = 0; i < 1000000; i++)
        {
            term2 = Goldilocks_neon::gl_mul(term0, term1);
            term0 = term1;
            term1 = term2;
        }
        benchmark::DoNotOptimize(term2);
    }
}
BENCHMARK(MUL_OP_NEON_BENCH)->Unit(benchmark::kMicrosecond)->UseRealTime();
#endif
