#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <numeric>
#include <string>
#include <thread>
#include <functional>

#include <sys/mman.h>
#include <sched.h>
#include <unistd.h>

namespace hwbench {

// ---------------------------------------------------------------------------
// Compiler optimization barriers
// ---------------------------------------------------------------------------
// Prevent the compiler from optimizing away reads/writes that have
// no visible side effect. Without this, the compiler might eliminate
// entire benchmark loops because "the result is never used."

template<typename T>
inline void do_not_optimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}

inline void memory_fence() {
    asm volatile("" ::: "memory");
}

// ---------------------------------------------------------------------------
// High-resolution timing
// ---------------------------------------------------------------------------

using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;

inline TimePoint now() { return Clock::now(); }

inline double elapsed_ns(TimePoint start, TimePoint end) {
    return std::chrono::duration<double, std::nano>(end - start).count();
}

inline double elapsed_us(TimePoint start, TimePoint end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

inline double elapsed_ms(TimePoint start, TimePoint end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct Stats {
    double min;
    double max;
    double median;
    double mean;
    double p99;
};

inline Stats compute_stats(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    size_t n = samples.size();
    Stats s;
    s.min = samples.front();
    s.max = samples.back();
    s.median = samples[n / 2];
    s.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / n;
    s.p99 = samples[static_cast<size_t>(n * 0.99)];
    return s;
}

// ---------------------------------------------------------------------------
// CPU pinning
// ---------------------------------------------------------------------------

inline bool pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
}

// ---------------------------------------------------------------------------
// Huge page allocation
// ---------------------------------------------------------------------------

inline void* alloc_huge_pages(size_t size) {
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

inline void* alloc_regular_pages(size_t size) {
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

inline void free_pages(void* ptr, size_t size) {
    munmap(ptr, size);
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

inline void print_header(const char* section) {
    printf("\n");
    printf("============================================================\n");
    printf("  %s\n", section);
    printf("============================================================\n\n");
}

inline void print_result(const char* label, double value, const char* unit) {
    printf("  %-45s %10.2f %s\n", label, value, unit);
}

inline void print_result_stats(const char* label, const Stats& s, const char* unit) {
    printf("  %-35s  min=%7.1f  median=%7.1f  p99=%7.1f  max=%7.1f %s\n",
           label, s.min, s.median, s.p99, s.max, unit);
}

inline void print_separator() {
    printf("  ----------------------------------------------------------\n");
}

inline void print_note(const char* note) {
    printf("  NOTE: %s\n", note);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t PAGE_SIZE_4K   = 4096;
constexpr size_t PAGE_SIZE_2M   = 2 * 1024 * 1024;
constexpr size_t KB = 1024;
constexpr size_t MB = 1024 * 1024;
constexpr size_t GB = 1024 * 1024 * 1024;

} // namespace hwbench
