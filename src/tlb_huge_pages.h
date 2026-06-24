#pragma once

#include "benchmark_utils.h"
#include <random>
#include <cstdlib>

namespace hwbench {

// ---------------------------------------------------------------------------
// TLB (Translation Lookaside Buffer) & Huge Pages Benchmark
// ---------------------------------------------------------------------------
//
// WHAT THIS MEASURES:
//   Every memory access requires translating a virtual address to a physical
//   address. The CPU caches these translations in the TLB. When the TLB
//   doesn't have the translation (a "TLB miss"), the CPU must walk the page
//   table in memory — a multi-level lookup that costs ~10-30ns per miss.
//
//   With 4KB pages, a 256MB buffer spans 65,536 pages. The TLB typically
//   holds ~1,500 entries. Random access across all 65K pages THRASHES the
//   TLB — almost every access is a miss.
//
//   With 2MB huge pages, the same 256MB buffer spans only 128 pages. The
//   TLB can hold all 128 entries easily. Almost every access is a TLB hit.
//
// WHY IT MATTERS FOR TRADING:
//   Order book arrays, hash maps, and packet buffers can span many pages.
//   If your hot data touches thousands of 4KB pages, TLB misses silently
//   add 10-30ns of jitter to every access. Huge pages eliminate this.
//   DPDK uses huge pages for exactly this reason.
// ---------------------------------------------------------------------------

class TlbBenchmark {
public:
    static void run() {
        print_header("TLB & HUGE PAGES");

        constexpr size_t BUFFER_SIZE = 256 * MB;
        constexpr int    NUM_ACCESSES = 10'000'000;
        constexpr int    WARMUP = 100'000;

        // Generate random access indices (same pattern for both tests)
        std::mt19937_64 rng(42);
        size_t num_elements = BUFFER_SIZE / sizeof(uint64_t);
        std::vector<size_t> indices(NUM_ACCESSES);
        for (auto& idx : indices) {
            idx = rng() % num_elements;
        }

        // ----- Regular 4KB pages -----
        printf("  Allocating %zu MB with 4KB pages...\n", BUFFER_SIZE / MB);
        void* regular = alloc_regular_pages(BUFFER_SIZE);
        if (!regular) {
            printf("  ERROR: Failed to allocate regular pages\n");
            return;
        }

        // Touch all pages to ensure they're mapped
        memset(regular, 0, BUFFER_SIZE);
        auto* regular_data = static_cast<uint64_t*>(regular);

        // Warmup
        for (int i = 0; i < WARMUP; ++i) {
            do_not_optimize(regular_data[indices[i]]);
        }

        // Benchmark: random reads across 256MB with 4KB pages
        auto start = now();
        uint64_t sum = 0;
        for (int i = 0; i < NUM_ACCESSES; ++i) {
            sum += regular_data[indices[i]];
            do_not_optimize(sum);
        }
        auto end = now();
        double ns_per_access_4k = elapsed_ns(start, end) / NUM_ACCESSES;

        size_t pages_4k = BUFFER_SIZE / PAGE_SIZE_4K;
        printf("  4KB pages: %zu pages for %zu MB\n", pages_4k, BUFFER_SIZE / MB);
        print_result("Random read latency (4KB pages)", ns_per_access_4k, "ns/access");

        free_pages(regular, BUFFER_SIZE);

        // ----- 2MB Huge pages -----
        printf("\n  Allocating %zu MB with 2MB huge pages...\n", BUFFER_SIZE / MB);
        void* huge = alloc_huge_pages(BUFFER_SIZE);
        if (!huge) {
            printf("  WARNING: Huge page allocation failed.\n");
            printf("  To enable: echo 256 > /proc/sys/vm/nr_hugepages\n");
            printf("  (requires root)\n\n");
            print_separator();
            print_result("Estimated TLB miss penalty", 10.0, "- 30 ns (not measured)");
            return;
        }

        memset(huge, 0, BUFFER_SIZE);
        auto* huge_data = static_cast<uint64_t*>(huge);

        // Warmup
        for (int i = 0; i < WARMUP; ++i) {
            do_not_optimize(huge_data[indices[i]]);
        }

        // Benchmark: same random reads with 2MB pages
        start = now();
        sum = 0;
        for (int i = 0; i < NUM_ACCESSES; ++i) {
            sum += huge_data[indices[i]];
            do_not_optimize(sum);
        }
        end = now();
        double ns_per_access_2m = elapsed_ns(start, end) / NUM_ACCESSES;

        size_t pages_2m = BUFFER_SIZE / PAGE_SIZE_2M;
        printf("  2MB pages: %zu pages for %zu MB\n", pages_2m, BUFFER_SIZE / MB);
        print_result("Random read latency (2MB pages)", ns_per_access_2m, "ns/access");

        free_pages(huge, BUFFER_SIZE);

        // ----- Results -----
        print_separator();
        double penalty = ns_per_access_4k - ns_per_access_2m;
        double speedup = ns_per_access_4k / ns_per_access_2m;
        print_result("TLB miss penalty (4KB - 2MB)", penalty, "ns");
        printf("  %-45s %10.2fx\n", "Speedup from huge pages", speedup);
        printf("\n  WHY: 4KB pages need %zu TLB entries (TLB holds ~1500).\n", pages_4k);
        printf("       2MB pages need only %zu entries — fits entirely in TLB.\n", pages_2m);
    }
};

} // namespace hwbench
