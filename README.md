# Hardware-Aware Systems Benchmark Suite

Empirical measurements of hardware-level performance effects that matter for low-latency systems: TLB behavior, cache hierarchy, NUMA topology, branch prediction, memory layout, prefetching, and allocator overhead.

Each benchmark runs millions of iterations, measures the effect in nanoseconds, and explains *why* the hardware behaves this way — connecting the measurement to practical systems design decisions.

## Building

Requires a C++17 compiler and CMake 3.14+.

```bash
mkdir build && cd build
cmake ..
make
```

Builds with `-O2 -march=native` by default — benchmarking unoptimized code is meaningless.

## Running

```bash
# Run all benchmarks (~60-120 seconds)
./hw_bench all

# Run a specific benchmark
./hw_bench tlb
./hw_bench cache
./hw_bench numa
./hw_bench branch
./hw_bench access
./hw_bench prefetch
./hw_bench alloc
```

For best results:

```bash
# Pin to a single core (avoid scheduler migration)
taskset -c 0 ./hw_bench all

# For TLB benchmark (requires root)
echo 256 > /proc/sys/vm/nr_hugepages
```

## What's Measured

### 1. TLB & Huge Pages (`tlb`)

Compares random access latency across a 256 MB buffer using 4 KB pages vs 2 MB huge pages. With 4 KB pages, the buffer spans 65,536 pages — far more than the ~1,500-entry TLB can hold. Random access causes constant TLB misses, each costing 10-30 ns for a page table walk. With 2 MB huge pages, the same buffer spans only 128 pages — fits entirely in the TLB, eliminating misses.

**Why it matters:** DPDK uses huge pages for packet buffers. Any large pre-allocated data structure (hash maps, order book arrays) benefits from huge pages when access patterns are random.

### 2. Cache Line Effects (`cache`)

Three tests:

**False sharing:** Two threads each increment their own atomic counter. When both counters share a 64-byte cache line, every write by one core invalidates the other core's cached copy, causing 10-50x slowdown. Padding with `alignas(64)` eliminates the interference.

**Cache hierarchy:** Pointer-chase traversal through arrays of increasing size reveals the L1/L2/L3/RAM latency boundaries. Typical results: ~1 ns (L1), ~4 ns (L2), ~10 ns (L3), ~100 ns (RAM).

**Sequential vs random access:** Same 64 MB buffer, different access patterns. Sequential access lets the hardware prefetcher pre-load cache lines — effectively free. Random access defeats prefetching, hitting main memory on most accesses. Typical penalty: 10-100x.

**Why it matters:** False sharing between producer and consumer atomics (e.g., head/tail in a lock-free queue) is a classic performance bug. Understanding cache hierarchy sizes tells you how much data can stay "hot." Sequential access patterns should be preferred wherever possible.

### 3. NUMA Topology (`numa`)

On multi-socket servers, memory attached to your CPU socket is "local" (~100 ns access). Memory on the other socket is "remote" (~150-200 ns) because the access crosses the inter-socket link. Allocating data structures on the wrong NUMA node silently doubles your memory latency.

**Why it matters:** Packet buffers, order books, and hash maps must be allocated on the same NUMA node as the thread that accesses them. DPDK's `rte_socket_id()` exists specifically for this.

*Note: Requires a multi-socket server to show the effect. Single-socket machines will show equal latency.*

### 4. Branch Prediction (`branch`)

**Sorted vs unsorted:** The classic benchmark — conditional sum over sorted vs unsorted data. The branch predictor learns the sorted pattern (all false, then all true) but can't predict random data near the threshold, causing pipeline flushes on every misprediction (~15-20 cycles).

**Branchy vs branchless:** Replaces the if/else with arithmetic bit masking that produces the same result without a branch instruction. No prediction needed, no pipeline flushes. Typical speedup: 2-5x on unpredictable data.

**Why it matters:** Hot-path code with unpredictable conditions (market data validation, price threshold checks) should use branchless techniques or `__builtin_expect` hints to minimize misprediction penalties.

### 5. Access Patterns: SoA vs AoS (`access`)

**Subset access:** Read only 2 of 8 fields across many elements. SoA (Struct-of-Arrays) loads only the fields you need into cache. AoS (Array-of-Structs) loads all 8 fields per element, wasting 75% of cache bandwidth.

**Full access:** Read all 8 fields of each element. AoS keeps everything for one element in the same cache line. SoA requires fetching from 8 separate arrays.

Both tests run at element counts from 10 to 1,000,000 to show where cache effects cause the crossover.

**Why it matters:** Order book design (bid prices, ask prices, bid sizes, ask sizes as separate arrays vs interleaved structs) should be guided by the actual access pattern of the calculator, not by convention.

### 6. Software Prefetching (`prefetch`)

**Linked list traversal:** A shuffled linked list (worst case for the hardware prefetcher). Manual `__builtin_prefetch` of the next node while processing the current one overlaps memory latency with computation. Tests 1-ahead and 2-ahead prefetch distances.

**Strided array access:** Access every Nth element of a 64 MB array at various strides. The hardware prefetcher handles small strides automatically. Manual prefetching helps at large strides where the hardware can't predict the pattern.

**Why it matters:** When iterating across order books for different securities, the next book may not be adjacent in memory. Prefetching it while processing the current book hides the memory latency.

### 7. Memory Allocators (`alloc`)

**Allocation speed:** Compares malloc, a pool allocator (pre-allocated fixed-size free list), and an arena allocator (bump pointer). Pool: ~8x faster than malloc. Arena: ~50-100x faster than malloc.

**Latency jitter:** Measures the distribution of individual allocation latencies. malloc occasionally spikes 100x+ above median when requesting pages from the OS. Pool and arena have deterministic latency.

**Why it matters:** Every `std::unordered_map::emplace`, every `std::string` construction, every `std::vector::push_back` that triggers growth calls malloc. In the hot path, these hidden allocations cause latency spikes. Pre-allocation, pool allocators, and arena allocators eliminate the jitter.

## Project Structure

```
hardware-benchmarks/
├── CMakeLists.txt
├── README.md
└── src/
    ├── benchmark_utils.h      — timing, barriers, formatting, CPU utils
    ├── tlb_huge_pages.h       — TLB miss penalty measurement
    ├── cache_lines.h          — false sharing, hierarchy, sequential vs random
    ├── numa_topology.h        — local vs remote NUMA node latency
    ├── branch_prediction.h    — sorted vs unsorted, branchy vs branchless
    ├── access_patterns.h      — SoA vs AoS at various sizes
    ├── prefetching.h          — manual prefetch for lists and strided access
    ├── allocators.h           — malloc vs pool vs arena speed and jitter
    └── main.cpp               — entry point, runs selected benchmarks
```
