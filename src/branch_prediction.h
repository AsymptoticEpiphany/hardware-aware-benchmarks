#pragma once

#include "benchmark_utils.h"
#include <vector>
#include <algorithm>
#include <random>

namespace hwbench {

// ---------------------------------------------------------------------------
// Branch Prediction Benchmark
// ---------------------------------------------------------------------------
//
// Modern CPUs predict which way branches will go and execute speculatively.
// When the prediction is correct (~97% for predictable patterns), there's
// no penalty. When it's wrong, the CPU flushes its pipeline and restarts
// — costing ~15-20 cycles (~5-7ns).
//
// WHY IT MATTERS FOR TRADING:
//   Hot-path code with unpredictable branches (random data through if/else)
//   suffers constant misprediction penalties. Sorted data, branch-free
//   arithmetic, and __builtin_expect hints can eliminate this.
// ---------------------------------------------------------------------------

class BranchBenchmark {
public:
    static void run() {
        print_header("BRANCH PREDICTION");
        run_sorted_vs_unsorted();
        run_branchy_vs_branchless();
    }

private:
    // -----------------------------------------------------------------------
    // TEST 1: Sorted vs Unsorted Data
    // -----------------------------------------------------------------------
    // The classic branch prediction benchmark. Sum elements > 128 in an
    // array. With sorted data, the branch predictor learns the pattern
    // (all < 128, then all > 128). With random data, the branch is
    // unpredictable near the threshold.
    // -----------------------------------------------------------------------

    static void run_sorted_vs_unsorted() {
        printf("  --- Sorted vs Unsorted Data ---\n\n");

        constexpr int N = 32768;
        constexpr int ITERATIONS = 1000;

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<int> data(N);
        for (auto& v : data) v = dist(rng);

        // Unsorted: branch predictor can't learn the pattern
        uint64_t sum = 0;
        auto start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                if (data[i] >= 128) {
                    sum += data[i];
                }
            }
            do_not_optimize(sum);
        }
        auto end = now();
        double unsorted_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        // Sort the data: branch predictor sees a clean transition
        std::sort(data.begin(), data.end());

        sum = 0;
        start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                if (data[i] >= 128) {
                    sum += data[i];
                }
            }
            do_not_optimize(sum);
        }
        end = now();
        double sorted_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        print_result("Unsorted (unpredictable branches)", unsorted_ns, "ns/element");
        print_result("Sorted (predictable branches)", sorted_ns, "ns/element");
        print_separator();
        printf("  %-45s %10.1fx\n", "Misprediction penalty", unsorted_ns / sorted_ns);
        printf("\n  WHY: Sorted data has a clean transition point — the predictor\n");
        printf("       learns 'always not taken' then 'always taken'. Unsorted\n");
        printf("       data is ~50%% unpredictable near the threshold, causing\n");
        printf("       pipeline flushes on every misprediction (~15-20 cycles).\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Branchy vs Branchless Code
    // -----------------------------------------------------------------------
    // Replace the if/else with arithmetic that produces the same result
    // without a branch instruction. The CPU never mispredicts because
    // there's nothing to predict.
    // -----------------------------------------------------------------------

    static void run_branchy_vs_branchless() {
        printf("  --- Branchy vs Branchless ---\n\n");

        constexpr int N = 32768;
        constexpr int ITERATIONS = 1000;

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<int> data(N);
        for (auto& v : data) v = dist(rng);

        // Branchy: if/else
        uint64_t sum = 0;
        auto start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                if (data[i] >= 128) {
                    sum += data[i];
                }
            }
            do_not_optimize(sum);
        }
        auto end = now();
        double branchy_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        // Branchless: multiply by the condition (0 or 1)
        // No branch instruction generated — just conditional move or multiply
        sum = 0;
        start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                int mask = -(data[i] >= 128);  // 0 or -1 (all bits set)
                sum += (data[i] & mask);
            }
            do_not_optimize(sum);
        }
        end = now();
        double branchless_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        print_result("Branchy (if/else, unsorted data)", branchy_ns, "ns/element");
        print_result("Branchless (arithmetic mask)", branchless_ns, "ns/element");
        print_separator();
        printf("  %-45s %10.1fx\n", "Branchless speedup", branchy_ns / branchless_ns);
        printf("\n  WHY: Branchless code uses arithmetic (conditional move or\n");
        printf("       bit masking) instead of if/else. The CPU executes the\n");
        printf("       same instructions regardless of the data — no prediction\n");
        printf("       needed, no pipeline flushes.\n\n");
        printf("  TRADING USE: In hot-path code, replace unpredictable branches\n");
        printf("       with conditional moves or arithmetic. Example: clamping\n");
        printf("       a price to a range, selecting between bid/ask, etc.\n\n");
    }
};

} // namespace hwbench
