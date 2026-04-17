// Apple Silicon thread-count + scheduler-hint tuning.
//
// On M-series hardware the system reports total logical cores via
// `omp_get_max_threads()` — e.g. 14 on M4 Pro = 10 P-cores + 4 E-cores.
// The E-cores run ~3x slower per core than P-cores; with OpenMP's default
// `schedule(static)` the 4 E-core threads become stragglers that dilate
// wall-clock for uniform CPU-bound workloads (grinding, Poseidon2 Merkle,
// NTT). Measured: GRINDING_CPU_BENCH/25 at 2313 ms with 14 threads (54%
// CPU utilization) vs 2106 ms with 10 threads (83% utilization) — 9%
// real-time win for the same total work.
//
// Strategy here:
//   1. At library load, query hw.perflevel0.logicalcpu (the P-core count)
//      via sysctl. Fall back silently on non-macOS / non-arm64 / failure.
//   2. Cap OMP thread count at the P-core count — but respect an explicit
//      user-set OMP_NUM_THREADS. The user may want all cores for a
//      particular workload, or may be running inside a container with
//      manual placement.
//   3. Set the main thread's QoS to USER_INITIATED. OMP worker threads
//      spawned after this point inherit QoS from the parent, which biases
//      the macOS scheduler to place them on P-cores. This is the only
//      public-API mechanism on macOS that hints "prefer performance
//      cores" — there is no hard pinning in userspace.
//
// Honest caveats:
//   - QoS is a *hint*, not a guarantee. The kernel can still migrate
//     threads to E-cores under system load.
//   - Capping at N = P-core-count doesn't pin to P-cores either; the
//     scheduler decides placement. But with N threads on a system with
//     >=N P-cores and USER_INITIATED QoS, placement is overwhelmingly
//     P-core in practice.
//   - Some workloads (small mixed-phase e2e with serial bottlenecks)
//     may be neutral or slightly worse with the cap because the E-cores
//     do contribute useful work during non-straggler-bound sections.
//     Users can override via `OMP_NUM_THREADS=14` if preferred.
// ----------------------------------------------------------------------------

#include "platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if PIL2_ARCH_ARM64 && defined(__APPLE__)
  #include <sys/sysctl.h>
  #include <pthread.h>
  #include <dispatch/dispatch.h>  // for qos_class_t
  #define PIL2_HAVE_APPLE_TUNING 1
#else
  #define PIL2_HAVE_APPLE_TUNING 0
#endif

#if defined(_OPENMP)
  #include <omp.h>
  #define PIL2_HAVE_OMP 1
#else
  #define PIL2_HAVE_OMP 0
#endif

namespace {

#if PIL2_HAVE_APPLE_TUNING
// Returns the number of performance cores on Apple Silicon, or 0 on failure.
int apple_p_core_count() {
    int count = 0;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.perflevel0.logicalcpu", &count, &size, nullptr, 0) == 0 && count > 0) {
        return count;
    }
    return 0;
}
#endif

bool user_set_omp_num_threads() {
    const char* v = std::getenv("OMP_NUM_THREADS");
    return v != nullptr && v[0] != '\0';
}

// Library-load hook. Runs before main() (or before first dylib use from Rust).
// Best-effort: any failure falls back silently to system defaults.
void pil2_tune_apple_silicon_threads() {
#if PIL2_HAVE_APPLE_TUNING
    // Prefer P-cores via QoS hint. Setting on the main thread first; OMP
    // workers spawned later inherit from the parent.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);

    if (user_set_omp_num_threads()) {
        return;  // Respect explicit override.
    }

  #if PIL2_HAVE_OMP
    int p_cores = apple_p_core_count();
    if (p_cores > 0) {
        // Don't INCREASE thread count if the system already has fewer threads
        // (unusual but possible in some container setups).
        int current_max = omp_get_max_threads();
        int target = (p_cores < current_max) ? p_cores : current_max;
        omp_set_num_threads(target);
        if (std::getenv("PIL2_LOG_THREADS")) {
            std::fprintf(stderr,
                "[pil2] Apple Silicon thread tuning: %d P-cores detected; capping OMP at %d "
                "(set OMP_NUM_THREADS to override).\n",
                p_cores, target);
        }
    }
  #endif
#endif
}

// Run at dylib load.
struct ThreadTuner {
    ThreadTuner() { pil2_tune_apple_silicon_threads(); }
} pil2_thread_tuner_instance_;

}  // anonymous namespace
