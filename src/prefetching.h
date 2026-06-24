#pragma once

#include "benchmark_utils.h"
#include <vector>
#include <random>

namespace hwbench {

// ---------------------------------------------------------------------------
// Software Prefetching Benchmark
// ---------------------------------------------------------------------------
//
// __builtin_prefetch(addr, rw, locality) tells the CPU to start loading
// a cache line before you actually need it. When the hardware prefetcher
// can't predict the access pattern (linked lists, hash table chasing,
// large strides), manual prefetching can hide memory latency.
//
// Parameters:
//   addr     — address to prefetch
//   rw       — 0 = read, 1 = write
//   locality — 0 = no temporal locality (use once), 3 = keep in all caches
//
// WHY IT MATTERS FOR TRADING:
//   When iterating across order books for different securities, the next
//   book might not be adjacent in memory. Prefetching it while processing
//   the current book hides the memory latency.
// ---------------------------------------------------------------------------

class PrefetchBenchmark {
public:
    static void run() {
        print_header("SOFTWARE PREFETCHING");
        run_linked_list_traversal();
        run_strided_access();
    }

private:
    // -----------------------------------------------------------------------
    // TEST 1: Linked List Traversal
    // -----------------------------------------------------------------------
    // Linked lists are the worst case for the hardware prefetcher — each
    // node points to a random location in memory. Manual prefetching of
    // the NEXT node while processing the current one can help.
    // -----------------------------------------------------------------------

    struct Node {
        Node*    next;
        uint64_t payload[7];  // pad to 64 bytes (one cache line)
    };

    static void run_linked_list_traversal() {
        printf("  --- Linked List Traversal ---\n\n");

        constexpr size_t NUM_NODES = 1'000'000;

        // Allocate nodes
        std::vector<Node> nodes(NUM_NODES);

        // Create a RANDOM linked list (shuffled order in memory)
        // This defeats the hardware prefetcher completely.
        std::vector<size_t> order(NUM_NODES);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937 rng(42);
        std::shuffle(order.begin(), order.end(), rng);

        for (size_t i = 0; i < NUM_NODES - 1; ++i) {
            nodes[order[i]].next = &nodes[order[i + 1]];
        }
        nodes[order[NUM_NODES - 1]].next = &nodes[order[0]]; // circular

        constexpr int TRAVERSALS = 2'000'000;

        // Without prefetch
        Node* current = &nodes[order[0]];
        // Warmup
        for (int i = 0; i < 100000; ++i) {
            current = current->next;
            do_not_optimize(current);
        }

        current = &nodes[order[0]];
        auto start = now();
        uint64_t sum = 0;
        for (int i = 0; i < TRAVERSALS; ++i) {
            sum += current->payload[0];
            current = current->next;
            do_not_optimize(sum);
        }
        auto end = now();
        double no_prefetch_ns = elapsed_ns(start, end) / TRAVERSALS;

        // With prefetch: prefetch the NEXT node while processing current
        current = &nodes[order[0]];
        // Warmup
        for (int i = 0; i < 100000; ++i) {
            __builtin_prefetch(current->next, 0, 0);
            current = current->next;
            do_not_optimize(current);
        }

        current = &nodes[order[0]];
        start = now();
        sum = 0;
        for (int i = 0; i < TRAVERSALS; ++i) {
            __builtin_prefetch(current->next, 0, 0);
            sum += current->payload[0];
            current = current->next;
            do_not_optimize(sum);
        }
        end = now();
        double prefetch_ns = elapsed_ns(start, end) / TRAVERSALS;

        // With prefetch 2-ahead: prefetch TWO nodes ahead
        current = &nodes[order[0]];
        // Warmup
        for (int i = 0; i < 100000; ++i) {
            if (current->next)
                __builtin_prefetch(current->next->next, 0, 0);
            current = current->next;
            do_not_optimize(current);
        }

        current = &nodes[order[0]];
        start = now();
        sum = 0;
        for (int i = 0; i < TRAVERSALS; ++i) {
            if (current->next)
                __builtin_prefetch(current->next->next, 0, 0);
            sum += current->payload[0];
            current = current->next;
            do_not_optimize(sum);
        }
        end = now();
        double prefetch2_ns = elapsed_ns(start, end) / TRAVERSALS;

        print_result("No prefetch", no_prefetch_ns, "ns/node");
        print_result("Prefetch 1 ahead", prefetch_ns, "ns/node");
        print_result("Prefetch 2 ahead", prefetch2_ns, "ns/node");
        print_separator();
        printf("  %-45s %10.1fx\n", "Speedup (1-ahead)", no_prefetch_ns / prefetch_ns);
        printf("  %-45s %10.1fx\n", "Speedup (2-ahead)", no_prefetch_ns / prefetch2_ns);
        printf("\n  WHY: Linked list nodes are scattered in memory. The hardware\n");
        printf("       prefetcher can't predict random pointer chasing.\n");
        printf("       Prefetching the next node while processing the current\n");
        printf("       one overlaps the memory latency with computation.\n");
        printf("       2-ahead gives more time for the prefetch to complete.\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Large-Stride Array Access
    // -----------------------------------------------------------------------
    // Access every Nth element of a large array. The hardware prefetcher
    // handles sequential access but may struggle with large strides.
    // -----------------------------------------------------------------------

    static void run_strided_access() {
        printf("  --- Strided Array Access ---\n\n");

        constexpr size_t SIZE = 64 * MB;
        size_t n = SIZE / sizeof(uint64_t);
        std::vector<uint64_t> data(n, 1);

        size_t strides[] = {1, 8, 16, 64, 256, 1024, 4096};
        constexpr int ACCESSES = 2'000'000;

        printf("  %-10s %14s %14s %10s\n",
               "Stride", "No prefetch", "With prefetch", "Speedup");
        print_separator();

        for (size_t stride : strides) {
            // Without prefetch
            auto start = now();
            uint64_t sum = 0;
            size_t idx = 0;
            for (int i = 0; i < ACCESSES; ++i) {
                sum += data[idx];
                idx = (idx + stride) % n;
                do_not_optimize(sum);
            }
            auto end = now();
            double no_pf = elapsed_ns(start, end) / ACCESSES;

            // With prefetch
            start = now();
            sum = 0;
            idx = 0;
            for (int i = 0; i < ACCESSES; ++i) {
                size_t next_idx = (idx + stride) % n;
                __builtin_prefetch(&data[next_idx], 0, 0);
                sum += data[idx];
                idx = next_idx;
                do_not_optimize(sum);
            }
            end = now();
            double with_pf = elapsed_ns(start, end) / ACCESSES;

            char stride_str[32];
            snprintf(stride_str, sizeof(stride_str), "%zu elements", stride);
            printf("  %-10s %11.2f ns %11.2f ns %9.2fx\n",
                   stride_str, no_pf, with_pf, no_pf / with_pf);
        }

        printf("\n  WHY: Small strides (1-16) → hardware prefetcher handles it.\n");
        printf("       Manual prefetch adds overhead for no benefit.\n");
        printf("       Large strides (256+) → hardware prefetcher can't predict.\n");
        printf("       Manual prefetch hides the cache miss latency.\n\n");
    }
};

} // namespace hwbench
