#pragma once

#include "benchmark_utils.h"
#include <vector>
#include <random>

namespace hwbench {

// ---------------------------------------------------------------------------
// Memory Access Patterns Benchmark: SoA vs AoS
// ---------------------------------------------------------------------------
//
// Struct-of-Arrays (SoA) stores each field in its own contiguous array.
// Array-of-Structs (AoS) stores all fields for one element together.
//
// SoA wins when you access a SUBSET of fields across many elements
// (only the data you need is loaded into cache).
// AoS wins when you access ALL fields of each element
// (all data for one element is in the same cache line).
//
// This benchmark measures both patterns at increasing element counts
// to find where cache effects cause the crossover.
// ---------------------------------------------------------------------------

class AccessPatternBenchmark {
public:
    static void run() {
        print_header("ACCESS PATTERNS: SoA vs AoS");
        run_subset_access();
        run_full_access();
    }

private:
    // AoS layout: all fields interleaved per element
    struct alignas(32) AoSEntry {
        int64_t  bid_price;
        uint32_t bid_size;
        int64_t  ask_price;
        uint32_t ask_size;
        int64_t  last_price;
        uint32_t last_size;
        uint64_t timestamp;
        uint32_t flags;
    };  // 52 bytes + padding = 64 bytes per entry

    // SoA layout: each field in its own array
    struct SoABook {
        std::vector<int64_t>  bid_prices;
        std::vector<uint32_t> bid_sizes;
        std::vector<int64_t>  ask_prices;
        std::vector<uint32_t> ask_sizes;
        std::vector<int64_t>  last_prices;
        std::vector<uint32_t> last_sizes;
        std::vector<uint64_t> timestamps;
        std::vector<uint32_t> flags;

        void resize(size_t n) {
            bid_prices.resize(n, 100'000'000'000LL);
            bid_sizes.resize(n, 100);
            ask_prices.resize(n, 101'000'000'000LL);
            ask_sizes.resize(n, 200);
            last_prices.resize(n, 100'500'000'000LL);
            last_sizes.resize(n, 50);
            timestamps.resize(n, 1000000);
            flags.resize(n, 1);
        }
    };

    // -----------------------------------------------------------------------
    // TEST 1: Subset Access (only bid_price and ask_price)
    // -----------------------------------------------------------------------
    // This is the case where SoA should win — we only touch 2 of 8 fields.
    // With AoS, loading bid_price also loads bid_size, ask_price, ask_size...
    // into the cache line, wasting bandwidth.
    // -----------------------------------------------------------------------

    static void run_subset_access() {
        printf("  --- Subset Access (2 of 8 fields: bid_price + ask_price) ---\n\n");

        size_t counts[] = {10, 100, 1000, 10000, 100000, 1000000};
        constexpr int ITERATIONS = 100;

        printf("  %-12s %12s %12s %10s\n",
               "Elements", "AoS (ns/el)", "SoA (ns/el)", "Winner");
        print_separator();

        for (size_t n : counts) {
            // Setup AoS
            std::vector<AoSEntry> aos(n);
            for (size_t i = 0; i < n; ++i) {
                aos[i].bid_price = 100'000'000'000LL + i;
                aos[i].ask_price = 101'000'000'000LL + i;
            }

            // Setup SoA
            SoABook soa;
            soa.resize(n);

            // Benchmark AoS: compute mid-price for all elements
            auto start = now();
            for (int iter = 0; iter < ITERATIONS; ++iter) {
                double total = 0;
                for (size_t i = 0; i < n; ++i) {
                    double mid = static_cast<double>(
                        aos[i].bid_price + aos[i].ask_price) / 2.0;
                    total += mid;
                    do_not_optimize(total);
                }
                do_not_optimize(total);
            }
            auto end_aos = now();
            double aos_ns = elapsed_ns(start, end_aos) / (static_cast<double>(n) * ITERATIONS);

            // Benchmark SoA: same computation
            start = now();
            for (int iter = 0; iter < ITERATIONS; ++iter) {
                double total = 0;
                for (size_t i = 0; i < n; ++i) {
                    double mid = static_cast<double>(
                        soa.bid_prices[i] + soa.ask_prices[i]) / 2.0;
                    total += mid;
                    do_not_optimize(total);
                }
                do_not_optimize(total);
            }
            auto end_soa = now();
            double soa_ns = elapsed_ns(start, end_soa) / (static_cast<double>(n) * ITERATIONS);

            char count_str[32];
            if (n >= 1000000)
                snprintf(count_str, sizeof(count_str), "%zuM", n / 1000000);
            else if (n >= 1000)
                snprintf(count_str, sizeof(count_str), "%zuK", n / 1000);
            else
                snprintf(count_str, sizeof(count_str), "%zu", n);

            const char* winner = (soa_ns < aos_ns) ? "SoA" : "AoS";
            printf("  %-12s %9.2f ns %9.2f ns %10s\n",
                   count_str, aos_ns, soa_ns, winner);
        }

        printf("\n  WHY: SoA loads ONLY bid_prices and ask_prices into cache.\n");
        printf("       AoS loads ALL 8 fields per element — 6 of which are\n");
        printf("       wasted. At large N, this waste means more cache misses.\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Full Access (all fields of each element)
    // -----------------------------------------------------------------------
    // When you need every field, AoS should be equal or better because
    // all data for one element is in the same cache line.
    // -----------------------------------------------------------------------

    static void run_full_access() {
        printf("  --- Full Access (all 8 fields per element) ---\n\n");

        size_t counts[] = {10, 100, 1000, 10000, 100000, 1000000};
        constexpr int ITERATIONS = 100;

        printf("  %-12s %12s %12s %10s\n",
               "Elements", "AoS (ns/el)", "SoA (ns/el)", "Winner");
        print_separator();

        for (size_t n : counts) {
            std::vector<AoSEntry> aos(n);
            for (size_t i = 0; i < n; ++i) {
                aos[i] = {100'000'000'000LL + static_cast<int64_t>(i),
                          100, 101'000'000'000LL + static_cast<int64_t>(i),
                          200, 100'500'000'000LL, 50, 1000000ULL + i, 1};
            }

            SoABook soa;
            soa.resize(n);

            // AoS: touch all fields
            auto start = now();
            for (int iter = 0; iter < ITERATIONS; ++iter) {
                uint64_t total = 0;
                for (size_t i = 0; i < n; ++i) {
                    total += aos[i].bid_price + aos[i].ask_price +
                             aos[i].bid_size + aos[i].ask_size +
                             aos[i].last_price + aos[i].last_size +
                             aos[i].timestamp + aos[i].flags;
                    do_not_optimize(total);
                }
                do_not_optimize(total);
            }
            auto end_aos = now();
            double aos_ns = elapsed_ns(start, end_aos) / (static_cast<double>(n) * ITERATIONS);

            // SoA: touch all arrays
            start = now();
            for (int iter = 0; iter < ITERATIONS; ++iter) {
                uint64_t total = 0;
                for (size_t i = 0; i < n; ++i) {
                    total += soa.bid_prices[i] + soa.ask_prices[i] +
                             soa.bid_sizes[i] + soa.ask_sizes[i] +
                             soa.last_prices[i] + soa.last_sizes[i] +
                             soa.timestamps[i] + soa.flags[i];
                    do_not_optimize(total);
                }
                do_not_optimize(total);
            }
            auto end_soa = now();
            double soa_ns = elapsed_ns(start, end_soa) / (static_cast<double>(n) * ITERATIONS);

            char count_str[32];
            if (n >= 1000000)
                snprintf(count_str, sizeof(count_str), "%zuM", n / 1000000);
            else if (n >= 1000)
                snprintf(count_str, sizeof(count_str), "%zuK", n / 1000);
            else
                snprintf(count_str, sizeof(count_str), "%zu", n);

            const char* winner = (soa_ns < aos_ns) ? "SoA" : "AoS";
            printf("  %-12s %9.2f ns %9.2f ns %10s\n",
                   count_str, aos_ns, soa_ns, winner);
        }

        printf("\n  WHY: When you need ALL fields, AoS keeps everything for\n");
        printf("       one element in the same cache line — one fetch gets\n");
        printf("       everything. SoA requires fetching from 8 separate\n");
        printf("       arrays, potentially 8 different cache lines.\n");
        printf("       The difference is small at 10 elements (fits in L1\n");
        printf("       either way) but grows with N.\n\n");
    }
};

} // namespace hwbench
