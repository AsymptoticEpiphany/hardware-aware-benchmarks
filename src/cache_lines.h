#pragma once

#include "benchmark_utils.h"
#include <atomic>
#include <thread>
#include <new>

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
    // -----------------------------------------------------------------------

    struct CountersPacked {
        std::atomic<uint64_t> a{0};  // likely on same cache line
        std::atomic<uint64_t> b{0};  // 8 bytes after a
    };

    struct CountersPadded {
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> a{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> b{0};
    };

    template<typename Counters>
    static double run_counter_test(Counters& counters, int iterations) {
        auto worker = [](std::atomic<uint64_t>& counter, int n) {
            for (int i = 0; i < n; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        };

        auto start = now();
        std::thread t1(worker, std::ref(counters.a), iterations);
        std::thread t2(worker, std::ref(counters.b), iterations);
        t1.join();
        t2.join();
        auto end = now();

        return elapsed_ns(start, end) / iterations;
    }

    static void run_false_sharing() {
        printf("  --- False Sharing ---\n\n");

        constexpr int ITERATIONS = 50'000'000;

        CountersPacked packed;
        CountersPadded padded;

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
        printf("       Every write by core 0 invalidates core 1's cache.\n");
        printf("       Padded counters are on separate lines — no interference.\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Cache Hierarchy
    // -----------------------------------------------------------------------
    // Sequential read through arrays of increasing size. When the array
    // fits in L1, access is ~1ns. When it spills to L2, latency jumps.
    // When it spills to L3, another jump. Beyond L3, main memory: ~100ns.
    // -----------------------------------------------------------------------

    static void run_cache_hierarchy() {
        printf("  --- Cache Hierarchy Latency ---\n\n");

        // Test sizes: 4KB to 256MB
        size_t sizes[] = {
            4*KB, 8*KB, 16*KB, 32*KB, 48*KB, 64*KB,       // L1 range
            128*KB, 256*KB, 512*KB, 1*MB,                    // L2 range
            2*MB, 4*MB, 8*MB, 16*MB, 32*MB,                 // L3 range
            64*MB, 128*MB, 256*MB                            // RAM
        };

        constexpr int ACCESSES = 10'000'000;

        printf("  %-15s %12s  %s\n", "Buffer Size", "Latency", "Region");
        print_separator();

        for (size_t size : sizes) {
            size_t n = size / sizeof(uint64_t);
            std::vector<uint64_t> data(n, 0);

            // Create a pointer-chase chain: each element contains the
            // index of the next element to visit. This defeats the
            // hardware prefetcher (which only handles sequential access)
            // and measures true cache latency.
            std::vector<size_t> chain(n);
            for (size_t i = 0; i < n; ++i) {
                chain[i] = (i + 127) % n;  // stride of 127 (prime, not power of 2)
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
            if (size <= 48*KB)       region = "← L1 (~32-48 KB)";
            else if (size <= 1*MB)   region = "← L2 (~256 KB - 1 MB)";
            else if (size <= 32*MB)  region = "← L3 (~8-32 MB)";
            else                     region = "← Main Memory";

            char size_str[32];
            if (size >= MB)
                snprintf(size_str, sizeof(size_str), "%zu MB", size / MB);
            else
                snprintf(size_str, sizeof(size_str), "%zu KB", size / KB);

            printf("  %-15s %9.1f ns  %s\n", size_str, ns, region);
        }
        printf("\n");
    }

    // -----------------------------------------------------------------------
    // TEST 3: Sequential vs Random Access
    // -----------------------------------------------------------------------
    // The hardware prefetcher detects sequential access patterns and loads
    // the next cache line before you need it. Random access defeats this.
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

        // Random access (same data, different pattern)
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
        printf("       miss cache and go to main memory (~100ns).\n\n");
    }
};

} // namespace hwbench
