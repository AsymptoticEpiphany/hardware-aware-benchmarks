cmake_minimum_required(VERSION 3.14)
project(hardware_benchmarks VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Build with optimizations — benchmarking unoptimized code is meaningless
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -DNDEBUG")

# ---------------------------------------------------------------------------
# Benchmark executable
# ---------------------------------------------------------------------------
add_executable(hw_bench src/main.cpp)

target_include_directories(hw_bench PRIVATE src)

# Link pthread for std::thread
target_link_libraries(hw_bench PRIVATE pthread)

# Link numa if available (for NUMA benchmark)
find_library(NUMA_LIB numa)
if(NUMA_LIB)
    target_link_libraries(hw_bench PRIVATE ${NUMA_LIB})
    target_compile_definitions(hw_bench PRIVATE HAS_LIBNUMA)
endif()
