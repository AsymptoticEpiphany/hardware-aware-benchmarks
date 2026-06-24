#include "benchmark_utils.h"
#include "tlb_huge_pages.h"
#include "cache_lines.h"
#include "numa_topology.h"
#include "branch_prediction.h"
#include "access_patterns.h"
#include "prefetching.h"
#include "allocators.h"

#include <cstdio>
#include <cstring>

using namespace hwbench;

static void print_usage(const char* program) {
    printf("Hardware-Aware Systems Benchmark Suite\n\n");
    printf("Usage: %s [benchmark]\n\n", program);
    printf("Benchmarks:\n");
    printf("  all          Run all benchmarks (default)\n");
    printf("  tlb          TLB & huge pages\n");
    printf("  cache        Cache line effects (false sharing, hierarchy)\n");
    printf("  numa         NUMA topology\n");
    printf("  branch       Branch prediction\n");
    printf("  access       Access patterns (SoA vs AoS)\n");
    printf("  prefetch     Software prefetching\n");
    printf("  alloc        Memory allocators (malloc vs pool vs arena)\n");
    printf("\nExample: %s cache\n", program);
    printf("         %s all\n\n", program);
    printf("For best results:\n");
    printf("  - Build with: g++ -O2 -march=native\n");
    printf("  - Pin to a core: taskset -c 0 ./%s\n", program);
    printf("  - Disable turbo boost for consistent results\n");
    printf("  - For TLB test: echo 256 > /proc/sys/vm/nr_hugepages\n\n");
}

int main(int argc, char* argv[]) {
    const char* which = (argc > 1) ? argv[1] : "all";

    if (strcmp(which, "-h") == 0 || strcmp(which, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    // Pin to core 0 for consistent results
    if (pin_to_core(0)) {
        printf("Pinned to CPU core 0\n");
    }

    printf("Hardware-Aware Systems Benchmark Suite\n");
    printf("Build: -O2 -march=native\n");
    printf("Each benchmark runs millions of iterations for statistical\n");
    printf("significance. Total runtime: ~60-120 seconds for all.\n");

    bool run_all = (strcmp(which, "all") == 0);

    if (run_all || strcmp(which, "tlb") == 0)
        TlbBenchmark::run();

    if (run_all || strcmp(which, "cache") == 0)
        CacheLineBenchmark::run();

    if (run_all || strcmp(which, "numa") == 0)
        NumaBenchmark::run();

    if (run_all || strcmp(which, "branch") == 0)
        BranchBenchmark::run();

    if (run_all || strcmp(which, "access") == 0)
        AccessPatternBenchmark::run();

    if (run_all || strcmp(which, "prefetch") == 0)
        PrefetchBenchmark::run();

    if (run_all || strcmp(which, "alloc") == 0)
        AllocatorBenchmark::run();

    printf("\n============================================================\n");
    printf("  DONE\n");
    printf("============================================================\n\n");

    return 0;
}
