#include "ntt_goldilocks.hpp"
#include "platform.hpp"
#include <memory>
#if PIL2_HAS_NEON
#include "ntt_goldilocks_neon.hpp"
#endif

//Explicar extend parameter
//Explicar inverse parameter
//Extension parameter

static inline uint64_t BR(uint64_t x, uint64_t domainPow)
{
#if defined(__aarch64__)
    // aarch64 has a single-instruction bit reversal (RBIT). clang's
    // __builtin_bitreverse32 lowers to `rbit w_reg, w_reg` (1-2 cycles)
    // — replaces the 10+ ops of the portable shift-mask sequence below.
    // Bit-identical semantics: reverse low 32 bits, then shift down to
    // keep the top `domainPow` bits of the reversed value.
    return (uint64_t)(__builtin_bitreverse32((uint32_t)x)) >> (32 - domainPow);
#else
    x = (x >> 16) | (x << 16);                              //swaps 32bit halves of x
    x = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);  //swaps 16bit halves of 32bit halves
    x = ((x & 0xF0F0F0F0) >> 4) | ((x & 0x0F0F0F0F) << 4);  //swaps 8bit halves of 16bit halves
    x = ((x & 0xCCCCCCCC) >> 2) | ((x & 0x33333333) << 2);  //swaps 4bit halves of 8bit halves
    return (((x & 0xAAAAAAAA) >> 1) | ((x & 0x55555555) << 1)) >> (32 - domainPow); //swaps 2bit halves of 4bit halves
#endif
}

/**
 * @brief Iterations of the NTT algorithm
 * 
 * @param dst destination pointer
 * @param src source pointer
 * @param nrows number of rows (power of 2)
 * @param offset_cols offset of the first column considered
 * @param ncols number of columns considered
 * @param ncols_all total number of columns
 * @param nphase number of phases of the NTT
 * @param aux auxiliary buffer
 * @param inverse if true, computes the inverse NTT
 * @param extend if true, multiplies the result by r_ (adoc optimization for the LDE)
 * */
void NTT_Goldilocks::NTT_iters(Goldilocks::Element *dst, Goldilocks::Element *src, uint64_t nrows, uint64_t offset_cols, uint64_t ncols, uint64_t ncols_all, uint64_t nphase, Goldilocks::Element *aux, bool inverse, bool extend)
{
    Goldilocks::Element *dst_;
    if (dst != NULL)
    {
        dst_ = dst;
    }
    else
    {
        dst_ = src;
    }
    Goldilocks::Element *a = dst_;
    uint64_t strideA = ncols_all;
    uint64_t offsetA = offset_cols;
    Goldilocks::Element *a2 = aux;
    uint64_t strideA2 = ncols;
    uint64_t offsetA2 = 0;
    Goldilocks::Element *tmp;
    uint64_t strideTmp;
    uint64_t offsetTmp;

    uint64_t domainPow = log2(nrows);
    assert(((uint64_t)1 << domainPow) == nrows);
    if (nphase < 1 || domainPow == 0)
    {
        nphase = 1;
    }
    else if (nphase > domainPow)
    {
        nphase = domainPow;
    }
    uint64_t maxBatchPow = s / nphase;
    uint64_t res = s % nphase;
    if (res > 0)
    {
        maxBatchPow += 1;
    }
    bool iseven = true;
    tmp = a;
    strideTmp = strideA;
    offsetTmp = offsetA;
    
    if (nphase % 2 == 1)
    {
        iseven = false;
        tmp = a2;
        strideTmp = strideA2;
        offsetTmp = offsetA2;
    }
    reversePermutation(tmp, strideTmp, offsetTmp, src, ncols_all, offset_cols, nrows, ncols);
    if (iseven == false)
    {
        tmp = a2;
        strideTmp = strideA2;
        offsetTmp = offsetA2;
        a2 = a;
        strideA2 = strideA;
        offsetA2 = offsetA;
        a = tmp;
        strideA = strideTmp;
        offsetA = offsetTmp;
    }

    omp_set_dynamic(0);
    omp_set_num_threads(nThreads);
    uint64_t count = 1;
    for (uint64_t s = 1; s <= domainPow; s += maxBatchPow, ++count)
    {
        if (res > 0 && count == res + 1 && maxBatchPow > 1)
        {
            maxBatchPow -= 1;
        }
        uint64_t sInc = s + maxBatchPow <= domainPow ? maxBatchPow : domainPow - s + 1;
        uint64_t rs = s - 1;
        uint64_t re = domainPow - 1;
        uint64_t rb = 1 << rs;
        uint64_t rm = (1 << (re - rs)) - 1;
        uint64_t batchSize = 1 << sInc;
        uint64_t nBatches = nrows / batchSize;

        int chunk1 = nBatches / nThreads;
        if (chunk1 == 0)
        {
            chunk1 = 1;
        }

        // ----- Phase A: butterflies (in-place on a) -----
#pragma omp parallel for schedule(static, chunk1)
        for (uint64_t b = 0; b < nBatches; b++)
        {
            for (uint64_t si = 0; si < sInc; si++)
            {
                uint64_t m = 1 << (s + si);
                uint64_t mdiv2 = m >> 1;
                uint64_t mdiv2i = 1 << si;
                uint64_t mi = mdiv2i * 2;
                for (uint64_t i = 0; i < (batchSize >> 1); i++)
                {
                    uint64_t ki = b * batchSize + (i / mdiv2i) * mi;
                    uint64_t ji = i % mdiv2i;

                    uint64_t offset1 = (ki + ji + mdiv2i) * strideA + offsetA;
                    uint64_t offset2 = (ki + ji) * strideA + offsetA;

                    uint64_t j = (b * batchSize / 2 + i);
                    j = (j & rm) * rb + (j >> (re - rs));
                    j = j % mdiv2;

                    Goldilocks::Element w = root(s + si, j);
#if PIL2_HAS_NEON
                    ntt_neon_butterfly(a, offset1, offset2, w, ncols);
#else
                    for (uint64_t k = 0; k < ncols; ++k)
                    {
                        Goldilocks::Element t = w * a[offset1 + k];
                        Goldilocks::Element u = a[offset2 + k];

                        Goldilocks::add(a[offset2 + k], t, u);
                        Goldilocks::sub(a[offset1 + k], u, t);
                    }
#endif
                }
            }
        }

        // ----- Phase B: reorganize a → a2 for next NTT phase -----
        // Split from the butterfly phase so the copy can use a different
        // (cache-friendly) loop structure. The implicit barrier at the end
        // of the butterfly `omp parallel for` ensures all writes to `a` are
        // visible before any thread reads `a` in the copy phase.
        if (s + maxBatchPow <= domainPow || !inverse)
        {
            // Normal phase (not-last or not-inverse): tile over b to make
            // writes to a2 contiguous within a tile. Original pattern was:
            //   for b { for x in 0..batchSize { memcpy stride nBatches } }
            // → batchSize strided writes per b (stride nBatches * ncols).
            // Tiled variant:
            //   for b_block step TILE_B { for x { for b in tile { memcpy } } }
            // → TILE_B contiguous writes per x, total TILE_B * batchSize writes
            // fit in ~L1 per tile. Reads from `a` are strided within the tile
            // but prefetcher-friendly (sequential across b).
            constexpr uint64_t TILE_B = 64;
#pragma omp parallel for schedule(static)
            for (uint64_t b_block = 0; b_block < nBatches; b_block += TILE_B)
            {
                uint64_t b_end = b_block + TILE_B < nBatches ? b_block + TILE_B : nBatches;
                for (uint64_t x = 0; x < batchSize; x++)
                {
                    for (uint64_t b = b_block; b < b_end; b++)
                    {
                        uint64_t offset_a2 = (x * nBatches + b) * strideA2 + offsetA2;
                        uint64_t offset_a = (b * batchSize + x) * strideA + offsetA;
                        std::memcpy(&a2[offset_a2], &a[offset_a], ncols * sizeof(Goldilocks::Element));
                    }
                }
            }
        }
        else
        {
            // Last-phase scale (extend or INTT): already uses the fused
            // ntt_neon_scale path. Keep the per-b structure since the
            // scale already amortises the per-row cost.
#pragma omp parallel for schedule(static, chunk1)
            for (uint64_t b = 0; b < nBatches; b++)
            {
                if (extend)
                {
                    for (uint64_t x = 0; x < batchSize; x++)
                    {
                        uint64_t dsty = intt_idx((x * nBatches + b), nrows);
                        uint64_t offset_a2 = dsty * strideA2 + offsetA2;
                        uint64_t offset_a = (b * batchSize + x) * strideA + offsetA;
#if PIL2_HAS_NEON
                        ntt_neon_scale(a2, offset_a2, a, offset_a, r_[dsty], ncols);
#else
                        for (uint64_t k = 0; k < ncols; k++)
                        {
                            Goldilocks::mul(a2[offset_a2 + k], a[offset_a + k], r_[dsty]);
                        }
#endif
                    }
                }
                else
                {
                    assert(inverse);
                    for (uint64_t x = 0; x < batchSize; x++)
                    {
                        uint64_t dsty = intt_idx((x * nBatches + b), nrows);
                        uint64_t offset_a2 = dsty * strideA2 + offsetA2;
                        uint64_t offset_a = (b * batchSize + x) * strideA + offsetA;
#if PIL2_HAS_NEON
                        ntt_neon_scale(a2, offset_a2, a, offset_a, powTwoInv[domainPow], ncols);
#else
                        for (uint64_t k = 0; k < ncols; k++)
                        {
                            Goldilocks::mul(a2[offset_a2 + k], a[offset_a + k], powTwoInv[domainPow]);
                        }
#endif
                    }
                }
            }
        }
        tmp = a2;
        strideTmp = strideA2;
        offsetTmp = offsetA2;
        a2 = a;
        strideA2 = strideA;
        offsetA2 = offsetA;
        a = tmp;
        strideA = strideTmp;
        offsetA = offsetTmp;
    }
    if (a != dst_)
    {
        if (nrows > 1)
        {
            assert(0); // should never need this copy...
        }
        Goldilocks::parcpy(dst_, a, nrows * ncols, nThreads);
    }
}

void NTT_Goldilocks::NTT(Goldilocks::Element *dst, Goldilocks::Element *src, uint64_t size, uint64_t ncols, Goldilocks::Element *buffer, uint64_t nphase, uint64_t nblock, bool inverse, bool extend)
{
    if (ncols == 0 || size == 0)
    {
        return;
    }
    if (nblock < 1)
    {
        nblock = 1;
    }
    if (nblock > ncols)
    {
        nblock = ncols;
    }

    uint64_t offset_cols = 0;
    uint64_t ncols_block = ncols / nblock;
    uint64_t ncols_res = ncols % nblock;
    uint64_t ncols_alloc = ncols_block;
    if (ncols_res > 0)
    {
        ncols_alloc += 1;
    }
    Goldilocks::Element *aux = NULL;
    if (buffer == NULL)
    {
        aux = (Goldilocks::Element *)malloc(sizeof(Goldilocks::Element) * size * ncols_alloc);
        if(aux == NULL){
            std::cerr << "Error: NTT_Goldilocks::NTT: Memory allocation failed" << std::endl;   
            exit(1);
        }
    }
    else
    {
        aux = buffer;
    }
    
    for (uint64_t ib = 0; ib < nblock; ++ib)
    {
        uint64_t aux_ncols = ncols_block;
        if (ib < ncols_res)
            aux_ncols += 1;
        NTT_Goldilocks::NTT_iters(dst, src, size, offset_cols, aux_ncols, ncols, nphase, aux, inverse, extend);
        offset_cols += aux_ncols;
    }
    if (buffer == NULL)
    {
        free(aux);
    }
}
/**
 * @brief permutation of components of an array in bit-reversal order. If dst==src the permutation is performed on-site.
 *
 * @param dst destination pointer (may be equal to src)
 * @param strideDst stride between consecutive elements of the same column in dst array
 * @param offsetDst offset of the first element of the first column in dst array
 * @param src source pointer
 * @param strideSrc stride between consecutive elements of the same column in src array
 * @param offsetSrc offset of the first element of the first column in src array
 * @param nrows number rows
 * @param ncols number of columns being permuted
 */
void NTT_Goldilocks::reversePermutation(Goldilocks::Element *dst, uint64_t strideDst, uint64_t offsetDst,  Goldilocks::Element *src, uint64_t strideSrc, uint64_t offsetSrc, uint64_t nrows, uint64_t ncols)
{
    uint32_t domainSize = log2(nrows);
    if (dst != src)
    {
        if (extension <= 1)
        {
#pragma omp parallel for schedule(static)
            for (uint64_t i = 0; i < nrows; i++)
            {
                uint64_t r = BR(i, domainSize);
                uint64_t offset_r1 = r * strideSrc + offsetSrc;
                uint64_t offset_i1 = i * strideDst + offsetDst;
                std::memcpy(&dst[offset_i1], &src[offset_r1], ncols * sizeof(Goldilocks::Element));
            }
        }
        else
        {
            //When the source is suposed to be an extension of a vector of size; nrows/extension, then we know that the source is zero from de component nrows/extension to nrows
            uint64_t ext_rows = nrows / extension;

#pragma omp parallel for schedule(static)
            for (uint64_t i = 0; i < nrows; i++)
            {
                uint64_t r = BR(i, domainSize);
                uint64_t offset_r1 = r * strideSrc + offsetSrc;
                uint64_t offset_i1 = i * strideDst + offsetDst;
                if (r < ext_rows)
                {
                    std ::memcpy(&dst[offset_i1], &src[offset_r1], ncols * sizeof(Goldilocks::Element));
                }
                else
                {
                    std::memset(&dst[offset_i1], 0, ncols * sizeof(Goldilocks::Element));
                }
            }
        }
    }
    else
    {
        uint32_t maxth = omp_get_max_threads();
        Goldilocks::Element **tmp_buffers = new Goldilocks::Element*[maxth];
        for (uint32_t i = 0; i < maxth; ++i)
        {
            tmp_buffers[i] = new Goldilocks::Element[ncols];
        }
        if (extension <= 1)
        {
#pragma omp parallel for schedule(static)
            for (uint64_t i = 0; i < nrows; i++)
            {
                uint64_t r = BR(i, domainSize);
                uint64_t offset_r = r * strideSrc + offsetSrc;
                uint64_t offset_i = i * strideDst + offsetDst;
                if (r < i)
                {
                    Goldilocks::Element* tmp = tmp_buffers[omp_get_thread_num()];
                    std::memcpy(&tmp[0], &src[offset_r], ncols * sizeof(Goldilocks::Element));
                    std::memcpy(&dst[offset_r], &src[offset_i], ncols * sizeof(Goldilocks::Element));
                    std::memcpy(&dst[offset_i], &tmp[0], ncols * sizeof(Goldilocks::Element));
                }
            }
        }
        else
        {
            //When the source is suposed to be an extension of a vector of size; nrows/extension, then we know that the source is zero from de component nrows/extension to nrows
            uint64_t ext_rows = nrows / extension;

#pragma omp parallel for schedule(static)
            for (uint64_t i = 0; i < nrows; i++)
            {
                uint64_t r = BR(i, domainSize);
                uint64_t offset_r = r * strideSrc + offsetSrc;
                uint64_t offset_i = i * strideDst + offsetDst;
                if (r < ext_rows)
                {
                    Goldilocks::Element* tmp = tmp_buffers[omp_get_thread_num()];
                    std::memcpy(&tmp[0], &src[offset_r], ncols * sizeof(Goldilocks::Element));
                    std::memcpy(&dst[offset_r], &src[offset_i], ncols * sizeof(Goldilocks::Element));
                    std::memcpy(&dst[offset_i], &tmp[0], ncols * sizeof(Goldilocks::Element));
                }
                else
                {
                    std::memset(&dst[offset_i], 0, ncols * sizeof(Goldilocks::Element));
                }
            }

        }
        for (uint32_t i = 0; i < maxth; ++i)
        {
            delete[] tmp_buffers[i];
        }
        delete[] tmp_buffers;
    }
}

void NTT_Goldilocks::LDE(Goldilocks::Element *output, Goldilocks::Element *input, uint64_t N_Extended, uint64_t N, uint64_t ncols, Goldilocks::Element *buffer, uint64_t nphase, uint64_t nblock)
{
    if (N == 0 || ncols == 0) {
        return;
    }

    // Cache the extension NTT across LDE calls. STARK proving calls LDE
    // multiple times per proof (one per stage's extendAndMerkelize) with
    // the same (N_Extended, factor) shape, and each construction repeats
    // an O(N_Extended) root-of-unity precompute + allocates
    // roots[N_Extended] + powTwoInv[log2(N_Extended)+1]. Reusing skips
    // both. Function-static so the class layout stays unchanged.
    //
    // Thread-safety: LDE is called from single-threaded prover top level
    // (not from inside OpenMP regions). A shared static cache is safe
    // here; the NTT object it references is used read-only during the
    // NTT() call below (OMP-internal parallelism over its read-only
    // roots/powTwoInv tables).
    static std::unique_ptr<NTT_Goldilocks> ntt_ext_cache;
    static uint64_t                        ntt_ext_cache_N      = 0;
    static int                             ntt_ext_cache_factor = 0;
    int factor = static_cast<int>(N_Extended / N);
    if (!ntt_ext_cache || ntt_ext_cache_N != N_Extended || ntt_ext_cache_factor != factor) {
        ntt_ext_cache.reset(new NTT_Goldilocks(N_Extended, nThreads, factor));
        ntt_ext_cache_N      = N_Extended;
        ntt_ext_cache_factor = factor;
    }
    NTT_Goldilocks& ntt_extension = *ntt_ext_cache;

    Goldilocks::Element *tmp = NULL;
    if (buffer == NULL)
    {
        tmp = (Goldilocks::Element *)malloc(N_Extended * ncols * sizeof(Goldilocks::Element));
        if(tmp == NULL){
            std::cerr << "Error: NTT_Goldilocks::LDE: Memory allocation failed" << std::endl;
            exit(1);
        }
    }
    else
    {
        tmp = buffer;
    }
    // TODO: Pre-compute r
    if (r == NULL)
    {
        computeR(N);
    }

    INTT(output, input, N, ncols, tmp, nphase, nblock, true);
    ntt_extension.NTT(output, output, N_Extended, ncols, tmp, nphase, nblock);

    if (buffer == NULL)
    {
        free(tmp);
    }
//#endif
}
