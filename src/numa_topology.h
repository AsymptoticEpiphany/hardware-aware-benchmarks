#pragma once

#include "benchmark_utils.h"
#include <fstream>
#include <string>
#include <random>

#ifdef HAS_LIBNUMA
#include <numaif.h>
#endif

namespace hwbench {

// ---------------------------------------------------------------------------
// NUMA (Non-Uniform Memory Access) Benchmark
// ---------------------------------------------------------------------------
//
// Multi-socket servers have memory attached to each CPU socket. Accessing
// memory on your local socket is ~100ns. Accessing memory on the remote
// socket crosses the inter-socket link (Intel UPI / AMD Infinity Fabric)
// and costs ~150-200ns — up to 2x slower.
//
// Trading systems pin threads to specific cores and allocate memory on
// the local NUMA node to avoid this penalty. DPDK's rte_socket_id()
// exists specifically for this.
//
// On a single-socket machine (most desktops, WSL2), this test will show
// no difference because there's only one memory controller.
// ---------------------------------------------------------------------------

class NumaBenchmark {
public:
    static void run() {
        print_header("NUMA TOPOLOGY");

        int num_nodes = detect_numa_nodes();
        printf("  Detected %d NUMA node(s)\n\n", num_nodes);

        if (num_nodes < 2) {
            printf("  Single NUMA node — skipping remote memory test.\n");
            printf("  (NUMA effects only visible on multi-socket servers.)\n\n");
            printf("  On a dual-socket server you would see:\n");
            printf("    Local memory access:  ~100 ns\n");
            printf("    Remote memory access: ~150-200 ns\n");
            printf("    Penalty: ~50-100 ns per access (1.5-2x slower)\n\n");
            printf("  This is why DPDK uses rte_socket_id() to allocate\n");
            printf("  packet buffers on the same NUMA node as the receive core.\n\n");

            // Still run the local memory test for reference
            run_local_only();
            return;
        }

        run_numa_comparison(num_nodes);
    }

private:
    static int detect_numa_nodes() {
        // Check how many NUMA nodes exist
        std::ifstream f("/sys/devices/system/node/online");
        if (!f.is_open()) return 1;
        std::string line;
        std::getline(f, line);
        // Parse "0-1" or "0-3" etc.
        auto dash = line.find('-');
        if (dash == std::string::npos) return 1;
        int max_node = std::stoi(line.substr(dash + 1));
        return max_node + 1;
    }

    static void run_local_only() {
        constexpr size_t SIZE = 64 * MB;
        constexpr int ACCESSES = 5'000'000;

        void* mem = alloc_regular_pages(SIZE);
        if (!mem) return;
        memset(mem, 0, SIZE);

        auto* data = static_cast<uint64_t*>(mem);
        size_t n = SIZE / sizeof(uint64_t);

        std::mt19937_64 rng(42);
        std::vector<size_t> indices(ACCESSES);
        for (auto& idx : indices) idx = rng() % n;

        // Warmup
        for (int i = 0; i < 100000; ++i) do_not_optimize(data[indices[i]]);

        auto start = now();
        uint64_t sum = 0;
        for (int i = 0; i < ACCESSES; ++i) {
            sum += data[indices[i]];
            do_not_optimize(sum);
        }
        auto end = now();
        double ns = elapsed_ns(start, end) / ACCESSES;

        print_result("Local memory random access", ns, "ns/access");
        free_pages(mem, SIZE);
    }

    static void run_numa_comparison(int num_nodes) {
#ifdef HAS_LIBNUMA
        constexpr size_t SIZE = 64 * MB;
        constexpr int ACCESSES = 5'000'000;

        std::mt19937_64 rng(42);
        size_t n = SIZE / sizeof(uint64_t);
        std::vector<size_t> indices(ACCESSES);
        for (auto& idx : indices) idx = rng() % n;

        for (int node = 0; node < num_nodes && node < 4; ++node) {
            void* mem = alloc_regular_pages(SIZE);
            if (!mem) continue;

            // Bind memory to specific NUMA node
            unsigned long nodemask = 1UL << node;
            if (mbind(mem, SIZE, MPOL_BIND, &nodemask, num_nodes + 1,
                      MPOL_MF_MOVE | MPOL_MF_STRICT) != 0) {
                printf("  WARNING: mbind to node %d failed (need root or "
                       "CAP_SYS_NICE)\n", node);
                free_pages(mem, SIZE);
                continue;
            }

            memset(mem, 0, SIZE);
            auto* data = static_cast<uint64_t*>(mem);

            // Warmup
            for (int i = 0; i < 100000; ++i) do_not_optimize(data[indices[i]]);

            auto start = now();
            uint64_t sum = 0;
            for (int i = 0; i < ACCESSES; ++i) {
                sum += data[indices[i]];
                do_not_optimize(sum);
            }
            auto end = now();
            double ns = elapsed_ns(start, end) / ACCESSES;

            char label[64];
            snprintf(label, sizeof(label), "NUMA node %d random access", node);
            print_result(label, ns, "ns/access");

            free_pages(mem, SIZE);
        }

        printf("\n  WHY: Remote NUMA access crosses the inter-socket link\n");
        printf("       (Intel UPI or AMD Infinity Fabric), adding ~50-100ns.\n");
        printf("       Always allocate hot data on the same node as the\n");
        printf("       thread that accesses it.\n\n");
#else
        (void)num_nodes;
        printf("  NUMA binding not available on this platform.\n\n");
#endif
    }
};

} // namespace hwbench
