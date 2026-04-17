# Goldilocks benchmark baselines — comparison notes

Two reference files in this directory:

| File | Host | When | Build mode |
|---|---|---|---|
| `zisk1.txt` | Linux x86_64, 32-core 5.7GHz | Before PR #465's bench reorg | Scalar + AVX + AVX-512 (multi-variant) |
| `apple-silicon-m4pro-scalar.txt` | macOS arm64, M4 Pro 14-core | After PR #465 bench reorg, before any NEON code lands | Scalar only (no AVX on Darwin, NEON not yet wired) |
| `apple-silicon-m4pro-neon-w8.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 35 — NEON W=8 wired (naive per-lane gl_mul) | Scalar + NEON W=8 |
| `apple-silicon-m4pro-neon-w8-paired.txt` | macOS arm64, M4 Pro 14-core | Part 5 Task 35.5 — paired-asm gl_mul | Scalar + NEON W=8 (paired) |

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

| Bench | Scalar | NEON Task 35 (per-lane scalar) | NEON Task 35.5 (paired asm) |
|---|---|---|---|
| `PERMUTE_W8`  | 173-186 ms | 170 ms (parity) | 169-174 ms (~5% faster) |
| `COMPRESS_W8` | 173-177 ms | 175 ms (parity) | 164-169 ms (~5% faster) |

**Honest read:** Task 35.5's paired-asm `gl_mul` (manually interleaving
the two lanes' `mul`+`umulh` to keep both Apple Silicon integer-mul pipes
busy each cycle) gives a real but modest ~5% win, not the 1.5× the per-
primitive theoretical maximum suggests. Reasons the win is small:

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
