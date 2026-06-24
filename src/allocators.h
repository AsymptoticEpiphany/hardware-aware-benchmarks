#pragma once

#include "benchmark_utils.h"
#include <vector>
#include <cstdlib>
#include <new>

namespace hwbench {

// ---------------------------------------------------------------------------
// Memory Allocator Benchmark
// ---------------------------------------------------------------------------
//
// malloc/new is a general-purpose allocator — it handles any size, any
// thread, any allocation pattern. This generality costs ~50-100ns per
// allocation, and under thread contention it can spike to microseconds.
//
// Trading systems use specialized allocators to eliminate this cost:
//   - Pool allocator: pre-allocate N objects of fixed size, O(1) alloc/free
//   - Arena allocator: bump a pointer forward, free everything at once
//   - Both achieve ~1-5ns per allocation with zero fragmentation
//
// WHY IT MATTERS FOR TRADING:
//   Every std::unordered_map insert, every std::string construction,
//   every std::vector::push_back that triggers growth calls malloc.
//   In the hot path, these hidden allocations cause latency jitter.
//   Pool and arena allocators eliminate the jitter entirely.
// ---------------------------------------------------------------------------

class AllocatorBenchmark {
public:
    static void run() {
        print_header("MEMORY ALLOCATORS");
        run_allocation_speed();
        run_allocation_jitter();
    }

private:
    // A minimal pool allocator for fixed-size objects
    class PoolAllocator {
    public:
        PoolAllocator(size_t object_size, size_t count)
            : object_size_(object_size)
        {
            // Pre-allocate a single contiguous block
            pool_ = static_cast<uint8_t*>(std::malloc(object_size * count));

            // Build a free list: each free slot points to the next
            for (size_t i = 0; i < count - 1; ++i) {
                *reinterpret_cast<void**>(pool_ + i * object_size) =
                    pool_ + (i + 1) * object_size;
            }
            *reinterpret_cast<void**>(pool_ + (count - 1) * object_size) = nullptr;
            free_list_ = pool_;
        }

        ~PoolAllocator() { std::free(pool_); }

        void* allocate() {
            if (!free_list_) return nullptr;
            void* ptr = free_list_;
            free_list_ = *static_cast<void**>(free_list_);
            return ptr;
        }

        void deallocate(void* ptr) {
            *static_cast<void**>(ptr) = free_list_;
            free_list_ = ptr;
        }

    private:
        uint8_t* pool_;
        void*    free_list_;
        size_t   object_size_;
    };

    // A minimal arena (bump) allocator — allocate forward, free all at once
    class ArenaAllocator {
    public:
        ArenaAllocator(size_t capacity)
            : buffer_(static_cast<uint8_t*>(std::malloc(capacity)))
            , capacity_(capacity)
            , offset_(0)
        {}

        ~ArenaAllocator() { std::free(buffer_); }

        void* allocate(size_t size) {
            // Align to 8 bytes
            size = (size + 7) & ~7ULL;
            if (offset_ + size > capacity_) return nullptr;
            void* ptr = buffer_ + offset_;
            offset_ += size;
            return ptr;
        }

        void reset() { offset_ = 0; }  // "free" everything at once

    private:
        uint8_t* buffer_;
        size_t   capacity_;
        size_t   offset_;
    };

    // -----------------------------------------------------------------------
    // TEST 1: Allocation Speed
    // -----------------------------------------------------------------------

    static void run_allocation_speed() {
        printf("  --- Allocation Speed ---\n\n");

        constexpr size_t OBJECT_SIZE = 64;  // typical order/message size
        constexpr int    NUM_ALLOCS  = 1'000'000;

        // Pre-allocate storage for pointers (so the vector doesn't
        // interfere with the measurement)
        std::vector<void*> ptrs(NUM_ALLOCS);

        // ----- malloc -----
        auto start = now();
        for (int i = 0; i < NUM_ALLOCS; ++i) {
            ptrs[i] = std::malloc(OBJECT_SIZE);
            do_not_optimize(ptrs[i]);
        }
        auto end = now();
        double malloc_alloc_ns = elapsed_ns(start, end) / NUM_ALLOCS;

        start = now();
        for (int i = 0; i < NUM_ALLOCS; ++i) {
            std::free(ptrs[i]);
        }
        end = now();
        double malloc_free_ns = elapsed_ns(start, end) / NUM_ALLOCS;

        // ----- Pool allocator -----
        PoolAllocator pool(OBJECT_SIZE, NUM_ALLOCS);

        start = now();
        for (int i = 0; i < NUM_ALLOCS; ++i) {
            ptrs[i] = pool.allocate();
            do_not_optimize(ptrs[i]);
        }
        end = now();
        double pool_alloc_ns = elapsed_ns(start, end) / NUM_ALLOCS;

        start = now();
        for (int i = 0; i < NUM_ALLOCS; ++i) {
            pool.deallocate(ptrs[i]);
        }
        end = now();
        double pool_free_ns = elapsed_ns(start, end) / NUM_ALLOCS;

        // ----- Arena allocator -----
        ArenaAllocator arena(OBJECT_SIZE * NUM_ALLOCS);

        start = now();
        for (int i = 0; i < NUM_ALLOCS; ++i) {
            ptrs[i] = arena.allocate(OBJECT_SIZE);
            do_not_optimize(ptrs[i]);
        }
        end = now();
        double arena_alloc_ns = elapsed_ns(start, end) / NUM_ALLOCS;

        start = now();
        arena.reset();  // one call frees everything
        end = now();
        double arena_free_ns = elapsed_ns(start, end);  // total, not per-alloc

        printf("  Object size: %zu bytes | Allocations: %d\n\n", OBJECT_SIZE, NUM_ALLOCS);
        printf("  %-20s %10s %10s %10s\n", "Allocator", "Alloc", "Free", "Total");
        print_separator();
        printf("  %-20s %7.1f ns %7.1f ns %7.1f ns\n",
               "malloc/free", malloc_alloc_ns, malloc_free_ns,
               malloc_alloc_ns + malloc_free_ns);
        printf("  %-20s %7.1f ns %7.1f ns %7.1f ns\n",
               "Pool allocator", pool_alloc_ns, pool_free_ns,
               pool_alloc_ns + pool_free_ns);
        printf("  %-20s %7.1f ns %7.1f ns %7.1f ns\n",
               "Arena allocator", arena_alloc_ns, 0.0,
               arena_alloc_ns);
        print_separator();
        printf("  %-45s %7.1fx\n", "Pool vs malloc speedup",
               (malloc_alloc_ns + malloc_free_ns) /
               (pool_alloc_ns + pool_free_ns));
        printf("  %-45s %7.1fx\n", "Arena vs malloc speedup",
               (malloc_alloc_ns + malloc_free_ns) / arena_alloc_ns);

        printf("\n  WHY: malloc searches free lists, handles arbitrary sizes,\n");
        printf("       and may lock internally. Pool allocator pops from a\n");
        printf("       pre-built free list — one pointer dereference. Arena\n");
        printf("       just bumps a pointer forward — even faster.\n\n");
    }

    // -----------------------------------------------------------------------
    // TEST 2: Allocation Latency Jitter
    // -----------------------------------------------------------------------
    // In trading, consistency matters more than average speed. malloc can
    // spike when it needs to request memory from the OS (mmap/brk) or
    // when another thread holds the allocator lock. Pool/arena never spike.
    //
    // We time batches of allocations to amortize the overhead of
    // clock_gettime (~20ns) which would otherwise dominate the
    // measurement for fast allocators like pool and arena.
    // -----------------------------------------------------------------------

    static void run_allocation_jitter() {
        printf("  --- Allocation Latency Jitter ---\n\n");

        constexpr size_t OBJECT_SIZE  = 64;
        constexpr int    BATCH_SIZE   = 100;
        constexpr int    NUM_BATCHES  = 10'000;
        constexpr int    TOTAL_ALLOCS = BATCH_SIZE * NUM_BATCHES;

        std::vector<double> malloc_times(NUM_BATCHES);
        std::vector<double> pool_times(NUM_BATCHES);
        std::vector<double> arena_times(NUM_BATCHES);
        std::vector<void*>  ptrs(BATCH_SIZE);

        // Measure malloc latencies in batches of 100
        for (int b = 0; b < NUM_BATCHES; ++b) {
            auto start = now();
            for (int i = 0; i < BATCH_SIZE; ++i) {
                ptrs[i] = std::malloc(OBJECT_SIZE);
                do_not_optimize(ptrs[i]);
            }
            auto end = now();
            malloc_times[b] = elapsed_ns(start, end) / BATCH_SIZE;
            for (int i = 0; i < BATCH_SIZE; ++i) {
                std::free(ptrs[i]);
            }
        }

        // Measure pool latencies in batches of 100
        PoolAllocator pool(OBJECT_SIZE, TOTAL_ALLOCS);
        for (int b = 0; b < NUM_BATCHES; ++b) {
            auto start = now();
            for (int i = 0; i < BATCH_SIZE; ++i) {
                ptrs[i] = pool.allocate();
                do_not_optimize(ptrs[i]);
            }
            auto end = now();
            pool_times[b] = elapsed_ns(start, end) / BATCH_SIZE;
            for (int i = 0; i < BATCH_SIZE; ++i) {
                pool.deallocate(ptrs[i]);
            }
        }

        // Measure arena latencies in batches of 100
        ArenaAllocator arena(OBJECT_SIZE * BATCH_SIZE);
        for (int b = 0; b < NUM_BATCHES; ++b) {
            auto start = now();
            for (int i = 0; i < BATCH_SIZE; ++i) {
                ptrs[i] = arena.allocate(OBJECT_SIZE);
                do_not_optimize(ptrs[i]);
            }
            auto end = now();
            arena_times[b] = elapsed_ns(start, end) / BATCH_SIZE;
            arena.reset();  // reset for next batch
        }

        auto malloc_stats = compute_stats(malloc_times);
        auto pool_stats   = compute_stats(pool_times);
        auto arena_stats  = compute_stats(arena_times);

        printf("  Batch size: %d allocations per timing sample\n\n", BATCH_SIZE);
        printf("  %-12s %8s %8s %8s %8s\n",
               "Allocator", "Median", "P99", "Max", "Jitter");
        print_separator();
        printf("  %-12s %5.0f ns %5.0f ns %5.0f ns %5.0f ns\n",
               "malloc", malloc_stats.median, malloc_stats.p99,
               malloc_stats.max, malloc_stats.max - malloc_stats.median);
        printf("  %-12s %5.0f ns %5.0f ns %5.0f ns %5.0f ns\n",
               "Pool", pool_stats.median, pool_stats.p99,
               pool_stats.max, pool_stats.max - pool_stats.median);
        printf("  %-12s %5.0f ns %5.0f ns %5.0f ns %5.0f ns\n",
               "Arena", arena_stats.median, arena_stats.p99,
               arena_stats.max, arena_stats.max - arena_stats.median);

        printf("\n  WHY: malloc's p99 and max latency spike when it needs to\n");
        printf("       request pages from the OS or contends with another thread\n");
        printf("       on the allocator lock. Pool and arena have deterministic\n");
        printf("       latency — no OS interaction, no locks, no surprises.\n");
        printf("       In trading, jitter kills.\n\n");
    }
};

} // namespace hwbench
