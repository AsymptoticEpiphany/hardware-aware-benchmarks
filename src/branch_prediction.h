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
// NOTE: Simple conditional additions like "if (x >= 128) sum += x" may
// be converted to CMOV (conditional move) by the compiler at -O2, which
// eliminates the branch entirely and makes sorted vs unsorted identical.
// We use an if/else with different operations on each side to force a
// real branch instruction.
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
    // Force a real branch by using an if/else with different work in each
    // path. The compiler cannot convert this to CMOV because both paths
    // have distinct side effects.
    // -----------------------------------------------------------------------

    // Prevent the compiler from combining the two branch paths
    static uint64_t __attribute__((noinline)) hot_path(int val, uint64_t sum) {
        return sum + val * 3 + 1;
    }

    static uint64_t __attribute__((noinline)) cold_path(int val, uint64_t sum) {
        return sum ^ static_cast<uint64_t>(val);
    }

    static void run_sorted_vs_unsorted() {
        printf("  --- Sorted vs Unsorted Data ---\n\n");

        constexpr int N = 32768;
        constexpr int ITERATIONS = 1000;

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<int> data(N);
        for (auto& v : data) {
            v = dist(rng);
        }

        // Unsorted: branch predictor can't learn the pattern
        uint64_t sum = 0;
        auto start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                if (data[i] >= 128) {
                    sum = hot_path(data[i], sum);
                }
                else {
                    sum = cold_path(data[i], sum);
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
                    sum = hot_path(data[i], sum);
                }
                else {
                    sum = cold_path(data[i], sum);
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
    // without a branch instruction.
    // -----------------------------------------------------------------------

    static void run_branchy_vs_branchless() {
        printf("  --- Branchy vs Branchless ---\n\n");

        constexpr int N = 32768;
        constexpr int ITERATIONS = 1000;

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<int> data(N);
        for (auto& v : data) {
            v = dist(rng);
        }

        // Branchy: if/else with noinline functions forces real branches
        uint64_t sum = 0;
        auto start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                if (data[i] >= 128) {
                    sum = hot_path(data[i], sum);
                }
                else {
                    sum = cold_path(data[i], sum);
                }
            }
            do_not_optimize(sum);
        }
        auto end = now();
        double branchy_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        // Branchless: bit masking — no branch instruction at all
        sum = 0;
        start = now();
        for (int iter = 0; iter < ITERATIONS; ++iter) {
            for (int i = 0; i < N; ++i) {
                int above = -(data[i] >= 128);  // 0 or -1 (all bits set)
                int below = ~above;
                uint64_t hot_val  = static_cast<uint64_t>(data[i] * 3 + 1);
                uint64_t cold_val = static_cast<uint64_t>(data[i]);
                sum += (hot_val & above) | (cold_val & below);
            }
            do_not_optimize(sum);
        }
        end = now();
        double branchless_ns = elapsed_ns(start, end) / (static_cast<double>(N) * ITERATIONS);

        print_result("Branchy (if/else, unsorted data)", branchy_ns, "ns/element");
        print_result("Branchless (arithmetic mask)", branchless_ns, "ns/element");
        print_separator();
        printf("  %-45s %10.1fx\n", "Branchless speedup", branchy_ns / branchless_ns);
        printf("\n  WHY: Branchless code uses arithmetic (bit masking) instead\n");
        printf("       of if/else. The CPU executes the same instructions\n");
        printf("       regardless of the data — no prediction needed, no\n");
        printf("       pipeline flushes.\n\n");
        printf("  TRADING USE: In hot-path code, replace unpredictable branches\n");
        printf("       with conditional moves or arithmetic. Example: clamping\n");
        printf("       a price to a range, selecting between bid/ask, etc.\n\n");
    }
};

} // namespace hwbench
