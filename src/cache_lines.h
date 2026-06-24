#pragma once

#include "benchmark_utils.h"
#include <atomic>
#include <thread>
#include <new>
#include <random>
#include <numeric>

namespace hwbench {

// ---------------------------------------------------------------------------
// Cache Line Effects Benchmark
// ---------------------------------------------------------------------------
//
// Three tests:
//   1. FALSE SHARING: two threads writing to adjacent vs padded atomics
//   2. CACHE HIERARCHY: measure L1/L2/L3/RAM latency boundaries
//   3. SEQUENTIAL vs RANDOM: hardware prefetcher effectiveness
// ---------------------------------------------------------------------------

class CacheLineBenchmark {
public:
    static void run() {
        print_header("CACHE LINE EFFECTS");
        run_false_sharing();
        run_cache_hierarchy();
        run_sequential_vs_random();
    }

private:
    // -----------------------------------------------------------------------
    // TEST 1: False Sharing
    // -----------------------------------------------------------------------
    // Two threads each increment their own counter. When the counters share
    // a cache line, every write by one core invalidates the other core's
    // cached copy, causing constant cache-line bouncing across the
    // interconnect.
    //
    // IMPORTANT: threads must be pinned to different PHYSICAL cores.
    // Core 0 and core 1 are typically hyperthreads on the same physical
    // core (shared L1 cache). Core 0 and core 2 are typically different
    // physical cores with separate L1 caches. Without this pinning,
    // false sharing may not manifest because both threads share the
    // same cache.
    // -----------------------------------------------------------------------

    struct CountersPacked {
        std::atomic<uint64_t> a{0};  // likely on same cache line
        std::atomic<uint64_t> b{0};  // 8 bytes after a
    };

    struct CountersPadded {
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> a{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> b{0};
    };

    // Detect the second physical core. On most x86 systems:
    //   Core 0, 1 = hyperthreads on physical core 0
    //   Core 2, 3 = hyperthreads on physical core 1
    // We want cores 0 and 2 for separate physical cores.
    static int get_second_core() {
        int n = static_cast<int>(std::thread::hardware_concurrency());
        if (n >= 4) {
            return 2;  // skip hyperthread sibling
        }
        if (n >= 2) {
            return 1;  // only 2 cores available, use what we have
        }
        return 0;
    }

    template<typename Counters>
    static double run_counter_test(Counters& counters, int iterations) {
        int core_b = get_second_core();

        auto worker = [](std::atomic<uint64_t>& counter, int n, int core) {
            pin_to_core(core);
            for (int i = 0; i < n; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        };

        auto start = now();
        std::thread t1(worker, std::ref(counters.a), iterations, 0);
        std::thread t2(worker, std::ref(counters.b), iterations, core_b);
        t1.join();
        t2.join();
        auto end = now();

        return elapsed_ns(start, end) / iterations;
    }

    static void run_false_sharing() {
        printf("  --- False Sharing ---\n\n");

        constexpr int ITERATIONS = 50'000'000;
        int core_b = get_second_core();

        CountersPacked packed;
        CountersPadded padded;

        printf("  Thread A pinned to core 0, Thread B pinned to core %d\n", core_b);
        printf("  (different physical cores to ensure separate L1 caches)\n\n");

        printf("  Packed counters: both on same cache line\n");
        printf("    sizeof(CountersPacked) = %zu bytes\n", sizeof(packed));
        printf("    &a = %p, &b = %p (distance: %zu bytes)\n\n",
               (void*)&packed.a, (void*)&packed.b,
               (size_t)((char*)&packed.b - (char*)&packed.a));

        printf("  Padded counters: each on own cache line\n");
        printf("    sizeof(CountersPadded) = %zu bytes\n", sizeof(padded));
        printf("    &a = %p, &b = %p (distance: %zu bytes)\n\n",
               (void*)&padded.a, (void*)&padded.b,
               (size_t)((char*)&padded.b - (char*)&padded.a));

        double packed_ns = run_counter_test(packed, ITERATIONS);
        double padded_ns = run_counter_test(padded, ITERATIONS);

        print_result("Packed (false sharing)", packed_ns, "ns/increment");
        print_result("Padded (no false sharing)", padded_ns, "ns/increment");
        print_separator();
        printf("  %-45s %10.1fx\n", "False sharing penalty",
               packed_ns / padded_ns);
        printf("\n  WHY: Packed counters share a 64-byte cache line.\n");
        printf("       Every write by core 0 invalidates core %d's cache.\n", core_b);
        printf("       Padded counters are on separate lines — no interference.\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Cache Hierarchy
    // -----------------------------------------------------------------------
    // Pointer-chase through arrays of increasing size using a random
    // permutation. Each element points to a random other element, forming
    // a single Hamiltonian cycle. This completely defeats the hardware
    // prefetcher and measures true memory access latency at each level.
    // -----------------------------------------------------------------------

    static void run_cache_hierarchy() {
        printf("  --- Cache Hierarchy Latency ---\n\n");

        size_t sizes[] = {
            4*KB, 8*KB, 16*KB, 32*KB, 48*KB, 64*KB,
            128*KB, 256*KB, 512*KB, 1*MB,
            2*MB, 4*MB, 8*MB, 16*MB, 32*MB,
            64*MB, 128*MB, 256*MB
        };

        constexpr int ACCESSES = 10'000'000;

        printf("  %-15s %12s  %s\n", "Buffer Size", "Latency", "Region");
        print_separator();

        for (size_t size : sizes) {
            size_t n = size / sizeof(size_t);

            // Build a random pointer-chase cycle (Sattolo's algorithm).
            // Every element points to a different element, forming a single
            // cycle that visits all elements exactly once. The access pattern
            // is completely random — the hardware prefetcher cannot predict it.
            std::vector<size_t> chain(n);
            std::iota(chain.begin(), chain.end(), 0);
            std::mt19937_64 rng(42);
            for (size_t i = n - 1; i > 0; --i) {
                size_t j = rng() % i;  // Sattolo: j < i, not j <= i
                std::swap(chain[i], chain[j]);
            }

            // Warmup
            size_t idx = 0;
            for (int i = 0; i < 100000; ++i) {
                idx = chain[idx];
                do_not_optimize(idx);
            }

            // Measure
            auto start = now();
            idx = 0;
            for (int i = 0; i < ACCESSES; ++i) {
                idx = chain[idx];
                do_not_optimize(idx);
            }
            auto end = now();

            double ns = elapsed_ns(start, end) / ACCESSES;

            const char* region;
            if (size <= 48*KB) {
                region = "← L1 (~32-48 KB)";
            }
            else if (size <= 1*MB) {
                region = "← L2 (~256 KB - 1 MB)";
            }
            else if (size <= 32*MB) {
                region = "← L3 (~8-32 MB)";
            }
            else {
                region = "← Main Memory";
            }

            char size_str[32];
            if (size >= MB) {
                snprintf(size_str, sizeof(size_str), "%zu MB", size / MB);
            }
            else {
                snprintf(size_str, sizeof(size_str), "%zu KB", size / KB);
            }

            printf("  %-15s %9.1f ns  %s\n", size_str, ns, region);
        }
        printf("\n");
    }

    // -----------------------------------------------------------------------
    // TEST 3: Sequential vs Random Access
    // -----------------------------------------------------------------------

    static void run_sequential_vs_random() {
        printf("  --- Sequential vs Random Access ---\n\n");

        constexpr size_t SIZE = 64 * MB;
        constexpr int ACCESSES = 10'000'000;
        size_t n = SIZE / sizeof(uint64_t);

        std::vector<uint64_t> data(n, 1);

        // Sequential access
        auto start = now();
        uint64_t sum = 0;
        for (int i = 0; i < ACCESSES; ++i) {
            sum += data[i % n];
            do_not_optimize(sum);
        }
        auto end = now();
        double sequential_ns = elapsed_ns(start, end) / ACCESSES;

        // Random access
        std::mt19937_64 rng(42);
        std::vector<size_t> random_indices(ACCESSES);
        for (auto& idx : random_indices) {
            idx = rng() % n;
        }

        start = now();
        sum = 0;
        for (int i = 0; i < ACCESSES; ++i) {
            sum += data[random_indices[i]];
            do_not_optimize(sum);
        }
        end = now();
        double random_ns = elapsed_ns(start, end) / ACCESSES;

        print_result("Sequential read (64 MB buffer)", sequential_ns, "ns/access");
        print_result("Random read (64 MB buffer)", random_ns, "ns/access");
        print_separator();
        printf("  %-45s %10.1fx\n", "Random access penalty", random_ns / sequential_ns);
        printf("\n  WHY: Sequential access lets the hardware prefetcher load\n");
        printf("       the next cache line before you need it — effectively free.\n");
        printf("       Random access defeats prefetching — every access may\n");
        printf("       miss cache and go to main memory.\n\n");
    }
};

} // namespace hwbench
