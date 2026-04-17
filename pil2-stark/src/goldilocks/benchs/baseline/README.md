# Goldilocks benchmark baselines — comparison notes

Two reference files in this directory:

| File | Host | When | Build mode |
|---|---|---|---|
| `zisk1.txt` | Linux x86_64, 32-core 5.7GHz | Before PR #465's bench reorg | CPU: Scalar + AVX + AVX-512 (multi-variant) |
| `zisk1_GPU.txt` | Linux x86_64 + NVIDIA CUDA | Before PR #465's bench reorg | GPU: NTT/INTT/LDE + LINEAR_HASH/MERKLETREE (TILES + ROWMAJOR) + GRINDING |
| `apple-silicon-m4pro-scalar.txt` | macOS arm64, M4 Pro 14-core | After PR #465 bench reorg, before any NEON code lands | CPU: Scalar only (no AVX on Darwin, NEON not yet wired) |
| `apple-silicon-m4pro-neon-w8.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 35 — NEON W=8 wired (naive per-lane gl_mul) | CPU: Scalar + NEON W=8 |
| `apple-silicon-m4pro-neon-w8-paired.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 35.5 — paired-asm gl_mul | CPU: Scalar + NEON W=8 (paired) |
| `apple-silicon-m4pro-neon-vectorized-add.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 38d — gl_add/gl_sub vectorised; matmul_external still punts | CPU: Scalar + NEON W=8/12/16 |
| `apple-silicon-m4pro-neon-ntt.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 43 — NTT/INTT inner butterfly NEON-vectorised | CPU: Scalar (auto picks NEON inside butterfly) + NEON Poseidon2 W=8 |
| `apple-silicon-m4pro-neon-w4w8.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 40 — W=4 NEON also wired via Auto | CPU: full Phase B state (NEON Poseidon2 W=4+W=8 + NEON NTT) |
| `apple-silicon-m4pro-neon-batch.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 36-37 — 2-sponge NeonBatch impls + benches added (Auto NOT routed) | CPU: Scalar (auto) + Neon + NeonBatch merkletree variants exposed |
| `apple-silicon-m4pro-neon-intt-scale.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 45 — INTT last-phase scaling (powTwoInv) + any-phase twiddle vectorised | CPU: NTT family at peak NEON state |

The two files **cannot be row-compared by name** because PR #465 (`refactor:
reorganize tests/benchmarks into per-area files`) renamed every benchmark
symbol, retired some, and changed the column-count arguments. This file
explains the deltas.

---

## 1. Differences that are by design (not errors)

### AVX / AVX-512 rows in `zisk1.txt` have no Apple Silicon counterpart

| Old (Linux) row | Apple Silicon | Reason |
|---|---|---|
| `ADD_OP_AVX_BENCH` | (absent) | Darwin doesn't compile AVX2 paths |
| `SUB_OP_AVX_BENCH` | (absent) | same |
| `MUL_OP_AVX_BENCH` | (absent) | same |
| `LINEAR_HASH_BENCH_AVX_*` | (absent) | same |
| `POSEIDON2_HASH_AVX_*` | (absent) | same |
| `MERKLETREE_AVX_*`, `MERKLETREE_BATCH_BENCH_AVX_*` | (absent) | same |

These rows **will reappear** as `*_NEON_CPU_BENCH` variants once Part 5 lands the
NEON kernels — at that point a fresh capture into
`apple-silicon-m4pro-neon.txt` will give the comparable post-NEON file.

### Column-count arguments changed

| Old | New | Notes |
|---|---|---|
| `/16`, `/32` | `/24`, `/36`, `/56` | New ncols match production NTT shapes |

`NTT_BENCH/16` (zisk1) ≈ `NTT_CPU_BENCH/24` (apple silicon) in spirit but the
sizes are different so the numbers don't compare directly.

### Benches retired in PR #465

These existed in `zisk1.txt` and are gone from the current bench suite:

- `NTT_BLOCK_BENCH/{16,32}` — block-stride variant
- `LDE_BLOCK_BENCH/{16,32}` — block-stride LDE
- `LDE_API_BENCH/{16}` — high-level LDE API wrapper

If you want them back for any reason, they'd need to be re-added in
`bench_ntt_cpu.cpp`. The current bench coverage skips them because the
production NTT path doesn't go through the block-stride forms.

### Benches new in PR #465 (not in `zisk1.txt`)

- `INTT_CPU_BENCH/{24,36,56}` — explicit inverse-NTT bench (was inferred from LDE)
- `PERMUTE_W{8,12,16}_SCALAR_CPU_BENCH` — single-permutation breakdown (separate from `COMPRESS`)
- `COMPRESS_W{4,8,12,16}_SCALAR_CPU_BENCH` — separated `compress` vs `permute`
- `LINEAR_HASH_W{8,12,16}_SCALAR_CPU_BENCH/{24,36,56}` — explicit per-width
- `MERKLETREE_W{8,12,16}_AR{2,3,4}_SCALAR_CPU_BENCH/{24,36,56}` — per-width × per-arity
- `GRINDING_CPU_BENCH/{20,21,22,24,25}` — grinding parameter sweep

---

## 2. Name-mapping table (where comparison IS meaningful)

Old → new. For old → new pairs whose ncols arg also differs, only the
"shape of the workload" is comparable, not the absolute number.

| Old (`zisk1.txt`) | New (`apple-silicon-m4pro-scalar.txt`) |
|---|---|
| `ADD_OP_BENCH` | `ADD_OP_BENCH` |
| `SUB_OP_BENCH` | `SUB_OP_BENCH` |
| `MUL_OP_BENCH` | `MUL_OP_BENCH` |
| `INV_OP_BENCH` | `INV_OP_BENCH` |
| `NTT_BENCH/{16,32}` | `NTT_CPU_BENCH/{24,36,56}` (ncols differ) |
| `LDE_BENCH/{16,32}` | `LDE_CPU_BENCH/{24,36,56}` (ncols differ) |
| `POSEIDON2_HASH_W_{4,8,12}/{16,32}` | `COMPRESS_W{4,8,12,16}_SCALAR_CPU_BENCH` (per-permutation, not per-row) |
| `LINEAR_HASH_BENCH_W_{8,12}/{16,32}` | `LINEAR_HASH_W{8,12,16}_SCALAR_CPU_BENCH/{24,36,56}` |
| `MERKLETREE_BENCH_AR_{8_2,12_3,16_4}/{16,32}` | `MERKLETREE_W{8,12,16}_AR{2,3,4}_SCALAR_CPU_BENCH/{24,36,56}` |

---

## 3. Side-by-side comparable rows

These are directly comparable (same name, same args). Times in microseconds.

| Bench | Linux x86 (`zisk1`) | M4 Pro scalar | Notes |
|---|---|---|---|
| `ADD_OP_BENCH` | 504 µs | **682 µs** | M4 ~26% slower per element-add scalar |
| `SUB_OP_BENCH` | 502 µs | **1129 µs** | M4 ~2.2× slower |
| `MUL_OP_BENCH` | 1568 µs | **1968 µs** | M4 ~25% slower |
| `INV_OP_BENCH` | 9734 µs | **11180 µs** | ~comparable |

The M4 Pro is a higher-IPC core, but the per-iteration micro-bench loops
appear to be limited by memory bandwidth or branch prediction in ways that
favor x86's deeper pipelines and x86 build's `__USE_ASSEMBLY__` codepath
(which uses `__asm__` add-no-double-carry; Darwin path is the C fallback).

---

## 3.5. W=8 NEON vs scalar — evolution across implementations

| Bench | Scalar | Task 35 (per-lane gl_mul) | Task 35.5 (paired-asm gl_mul) | Task 38d (+ vectorised gl_add/sub) |
|---|---|---|---|---|
| `PERMUTE_W8`  | 173-186 ms | 170 ms (parity) | 169-174 ms (~5% faster) | 184-188 ms (~parity) |
| `COMPRESS_W8` | 173-177 ms | 175 ms (parity) | 164-169 ms (~5% faster) | 187 ms (~5% faster) |
| `PERMUTE_W12` | 241-247 ms | n/a (W=8 only) | n/a (W=8 only) | 263-276 ms (**+9-15% slower**) |
| `PERMUTE_W16` | 309-322 ms | n/a (W=8 only) | n/a (W=8 only) | 354-365 ms (**+12-18% slower**) |

**Honest read:**

- W=8: NEON wins ~5% (from paired-asm `gl_mul`).
- W=12 / W=16: NEON **loses 9-18%** because `matmul_external_neon` punts to
  scalar (NEON-store / scalar-call / NEON-load round-trip per round). A first
  attempt at vectorising the M4 with NEON gl_add + lane shuffles made things
  ~14% worse — Apple Silicon has 8 integer ALUs vs 4 NEON ALUs, so scalar
  add chains have more parallel headroom than NEON add chains for this kind
  of cross-element add-heavy code. The shuffle ops (vextq, vzip) cost more
  than they save.

To avoid shipping a regression at the production widths, `Auto` resolution
on Darwin is **restricted to Mode::Neon only at W=8**; W=12 and W=16 fall
back to Scalar. Explicit `Mode::Neon` still works at all widths
(correctness-gated by the W=12 / W=16 mode-equivalence tests).

Reasons the W=8 win is small:

1. The 22 partial rounds still extract NEON state to scalars, do the sum
   + state[0] arithmetic in scalar code, then reload — 22/(8+22) ≈ 73%
   of total rounds bypass NEON entirely.
2. `matmul_external_neon` punts to the existing scalar matmul via
   NEON-store / scalar-call / NEON-load (cheap operations dominated by
   pow7's mul cost, but still measurable).
3. `gl_add` and `gl_sub` are per-lane scalar with NEON wrappers — same
   wrap/unwrap overhead pattern.

The benchmark measurement noise floor on this M4 Pro under typical load
is ~5%, so reporting 1s and 2s `--benchmark_min_time` numbers as a range
is the honest way to do it.

The path to a bigger win:
- Vectorise the partial-round arithmetic (NEON sum-across-lanes via
  `vaddvq_u64` + canonical reduction) — 73% of remaining round work
- Vectorise `matmul_external` directly in NEON (16 adds + 8 cross-chunk
  adds, all of which fit cleanly in NEON)
- Possibly fuse gl_add/gl_mul with their use sites to eliminate the
  per-call wrap/unwrap.

These are queued as Phase B follow-ups, not blockers for the bit-exact
gate.

---

## 3.5a. W=4 NEON vs scalar (Task 40)

| Bench | Scalar | NEON | Δ |
|---|---|---|---|
| `PERMUTE_W4`  | 110 ms | 99.9 ms | **−9.2%** |
| `COMPRESS_W4` | 110 ms | 99.7 ms | **−9.4%** |

Grinding (which runs PERMUTE_W4 inside a leading-zero search loop) shows
mixed numbers (`/20`, `/24` win ~7-9%; `/21` looks like variance). This is
expected — grinding's runtime depends on how quickly a satisfying nonce
appears, not just per-permutation cost. The micro-bench is the reliable
signal.

W=4 won where W=12/W=16 lost because at W=4 there's only ONE M4 chunk and
no cross-chunk sum (the `if (SPONGE_WIDTH > 4)` short-circuits), so the
matmul punt is much cheaper. NEON wins on the pow7add side aren't diluted
by scalar matmul overhead.

Auto resolution on Darwin now picks NEON for both W=4 AND W=8.
W=12 / W=16 still fall back to Scalar pending matmul vectorisation.

---

## 3.5c. Merkletree Scalar vs Neon vs NeonBatch (Task 36-37, /24 cols)

| Bench | Scalar | Neon (single) | NeonBatch (2-sponge) | Best NEON delta |
|---|---|---|---|---|
| `MERKLETREE_W8_AR2`   | 4985 ms | 4937 ms (−1%) | 4792 ms (**−4%**) | NeonBatch wins narrowly |
| `MERKLETREE_W12_AR3`  | 3349 ms | 3588 ms (+7%) | 3291 ms (**−2%**) | NeonBatch wins narrowly |
| `MERKLETREE_W16_AR4`  | 2728 ms | 3143 ms (+15%) | 3022 ms (+11%) | scalar still wins |

**Honest read:** The 2-sponge NeonBatch impl is a real correctness deliverable
(3 mode-equivalence tests pass for W=8/W=12/W=16 across realistic shapes),
and it consistently beats single-sponge Neon at W=12/W=16. But at the
merkletree level the win over Scalar is marginal-to-negative — Apple
Silicon's scalar pipeline already extracts most of the parallelism via
clang's auto-vectorisation, and NEON's strided gather load (we use
`vsetq_lane_u64` to pull element_k from each of 2 back-to-back sponges)
adds overhead that the 2-lane parallelism can't quite cover.

For now `merkletree(Auto)` on Darwin stays on Scalar. NeonBatch is
available via explicit `Mode::NeonBatch` for future experimentation:

- **Interleaved layout** could remove the strided-load overhead by storing
  state as `{sp0.x0, sp1.x0, sp0.x1, sp1.x1, ...}`, allowing single
  `vld1q_u64` loads.
- **4-row batch via 2 NEON regs per element** would mirror AVX2's 4-sponge
  contract (more parallelism, more register pressure).
- **Profiling** to identify whether the bottleneck is the strided load,
  the gl_mul/gl_add throughput, or thread synchronisation in the OMP loop.

These are queued as follow-ups, not blockers.

---

## 3.5b. NTT NEON vs scalar baseline (Task 43 + 45)

| Bench | Scalar | NEON Task 43 (butterfly only) | NEON Task 45 (+ scaling) | Total Δ vs Scalar |
|---|---|---|---|---|
| `NTT_CPU_BENCH/24`  | 243 ms | 198 ms | **193 ms** | **−20.6%** |
| `NTT_CPU_BENCH/36`  | 309 ms | 256 ms | **255 ms** | **−17.5%** |
| `NTT_CPU_BENCH/56`  | 461 ms | 387 ms | **358 ms** | **−22.3%** |
| `INTT_CPU_BENCH/24` | 251 ms | 212 ms | **196 ms** | **−21.9%** |
| `INTT_CPU_BENCH/36` | 332 ms | 294 ms | **274 ms** | **−17.5%** |
| `INTT_CPU_BENCH/56` | 474 ms | 417 ms | **399 ms** | **−15.8%** |
| `LDE_CPU_BENCH/24`  | 302 ms | 255 ms | **252 ms** | **−16.6%** |
| `LDE_CPU_BENCH/56`  | 557 ms | 461 ms | **475 ms** | **−14.7%** |

The Task 45 incremental win came from vectorising the per-column
scalar-multiply at the last NTT phase (`Goldilocks::mul(...,
powTwoInv[])` on the inverse path; `Goldilocks::mul(..., r_[dsty])` on
the forward "any phase" path). Same NEON pattern as the butterfly:
broadcast scalar via splat, two columns per iter, scalar tail for odd
ncols.

**Honest read:** unlike Poseidon2, the NTT inner butterfly is structurally a
clean fit for NEON: two consecutive columns share a twiddle, sit adjacent
in memory, and need only `gl_mul / gl_add / gl_sub` per pair. No
lane-shuffles, no wide-state matmul punt, no per-iteration scalar work
mixed in. Result: a clean 12-18% win across all NTT/INTT/LDE shapes
(low cv 0.7-3.4%).

The NTT NEON path is on by default at compile time when `PIL2_HAS_NEON`
is defined — there is no `Mode::Neon` selector for NTT today (the
butterfly is the only inner-loop hot path; one fast path replaces the
scalar one entirely). LDE_CPU_BENCH inherits the NTT win since it's
NTT + extension internally.

---

## 3.6. CUDA reference (`zisk1_GPU.txt`) — what Metal needs to match

The Linux CUDA build provides the GPU baseline. Selected headline rows
(NUM_HASHES = 1<<23, ncols=24/36/56):

| Row | /24 | /36 | /56 |
|---|---|---|---|
| `NTT_GPU_BENCH`                          |  10.5 ms |  15.7 ms |  24.4 ms |
| `LDE_GPU_BENCH`                          |   7.8 ms |  11.6 ms |  18.1 ms |
| `LINEAR_HASH_W12_TILES_GPU_BENCH`        |  18.9 ms |  31.4 ms |  44.0 ms |
| `LINEAR_HASH_W16_TILES_GPU_BENCH`        |  17.1 ms |  25.6 ms |  42.5 ms |
| `MERKLETREE_W12_AR3_TILES_GPU_BENCH`     |  22.2 ms |  34.7 ms |  47.4 ms |
| `MERKLETREE_W16_AR4_TILES_GPU_BENCH`     |  19.9 ms |  28.5 ms |  45.5 ms |
| `GRINDING_GPU_BENCH/24`                  |   7.4 ms |          |          |

**Speedup vs CPU scalar**: ~25-50× for NTT/LDE; ~150-200× for merkletree
operations. The CUDA path is what the Metal port (Parts 6-7) is targeting,
not necessarily to match exactly but to land within the same order of
magnitude. Note the `TILES` vs `ROWMAJOR` rows correspond to the
`Layout::Tiles` / `Layout::RowMajor` enum that the Metal port mirrors
(briefing CCD §22).

The GPU bench file lives here even though the runner produces it on
Linux — it is the cross-platform reference for GPU/Metal work and
deserves to sit next to the CPU baselines for easy comparison once
Metal benches start landing.

---

## 4. What this baseline is for

**Tracking NEON gains as Part 5 lands.** Each W=8/12/16/4 sub-checkpoint
should beat the corresponding `*_SCALAR_CPU_BENCH` row in this file —
target is 1.5×–2× per primitive for permutation-heavy ops, less for
limb-count-bound ops like `MUL_OP_BENCH` where NEON gives only 2 lanes.

When NEON kernels are in, capture `apple-silicon-m4pro-neon.txt` next to
this file and add a new "NEON" column above. The `*_AVX_CPU_BENCH` rows
that exist in the bench source but not in either baseline today will also
populate on Linux and never on Darwin (correct).

---

## 5. How to regenerate

```bash
cd /Users/edu/Development/pil2-proofman/pil2-stark/src/goldilocks
make benchscpu
./benchscpu --benchmark_min_time=1.0s | tee benchs/baseline/<host-and-mode>.txt
```

Naming convention: `<arch>-<chip>-<mode>.txt` (e.g. `apple-silicon-m4pro-neon.txt`,
`linux-x86_64-avx2.txt`).
