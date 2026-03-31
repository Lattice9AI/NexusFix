// timestamp_parse_bench.cpp
// TICKET_443: SIMD Timestamp Parsing Benchmark
//
// Compares scalar vs AVX2 timestamp parsing performance
// Uses nfx::bench utilities (benchmark_utils.hpp)

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <string>

#include "nexusfix/parser/timestamp_parser.hpp"
#include "benchmark_utils.hpp"

using namespace nfx;
using namespace nfx::bench;

// ============================================================================
// Print helpers
// ============================================================================

static void print_stats(const char* name, const LatencyStats& stats) {
    std::cout << "\n=== " << name << " ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Iterations: " << stats.count << "\n";
    std::cout << "  Min:    " << std::setw(10) << stats.min_ns << " ns\n";
    std::cout << "  Mean:   " << std::setw(10) << stats.mean_ns << " ns\n";
    std::cout << "  P50:    " << std::setw(10) << stats.p50_ns << " ns\n";
    std::cout << "  P90:    " << std::setw(10) << stats.p90_ns << " ns\n";
    std::cout << "  P99:    " << std::setw(10) << stats.p99_ns << " ns\n";
    std::cout << "  P99.9:  " << std::setw(10) << stats.p999_ns << " ns\n";
    std::cout << "  Max:    " << std::setw(10) << stats.max_ns << " ns\n";
    std::cout << "  StdDev: " << std::setw(10) << stats.stddev_ns << " ns\n";
}

// ============================================================================
// Test Data
// ============================================================================

// Typical FIX timestamp embedded in message context (32+ bytes for safe AVX2 load)
static const std::string TIMESTAMP_IN_MSG =
    "20240115-10:30:00.123\x01" "37=ORDER123456789";

// ============================================================================
// Benchmarks
// ============================================================================

static LatencyStats bench_scalar(size_t iterations, double freq_ghz) {
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    std::string_view input{TIMESTAMP_IN_MSG};

    warmup_icache([&]() {
        auto r = parse_timestamp_scalar(input);
        compiler_barrier();
        (void)r;
    });

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t elapsed;
        {
            ScopedTimer timer(elapsed);
            auto result = parse_timestamp_scalar(input);
            compiler_barrier();
            (void)result;
        }
        cycles.push_back(elapsed);
    }

    LatencyStats stats;
    stats.compute(cycles, freq_ghz);
    return stats;
}

#if NFX_SIMD_AVAILABLE
static LatencyStats bench_simd(size_t iterations, double freq_ghz) {
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    std::string_view input{TIMESTAMP_IN_MSG};

    warmup_icache([&]() {
        auto r = parse_timestamp_simd(input);
        compiler_barrier();
        (void)r;
    });

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t elapsed;
        {
            ScopedTimer timer(elapsed);
            auto result = parse_timestamp_simd(input);
            compiler_barrier();
            (void)result;
        }
        cycles.push_back(elapsed);
    }

    LatencyStats stats;
    stats.compute(cycles, freq_ghz);
    return stats;
}
#endif

static LatencyStats bench_dispatch(size_t iterations, double freq_ghz) {
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    std::string_view input{TIMESTAMP_IN_MSG};

    warmup_icache([&]() {
        auto r = parse_timestamp(input);
        compiler_barrier();
        (void)r;
    });

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t elapsed;
        {
            ScopedTimer timer(elapsed);
            auto result = parse_timestamp(input);
            compiler_barrier();
            (void)result;
        }
        cycles.push_back(elapsed);
    }

    LatencyStats stats;
    stats.compute(cycles, freq_ghz);
    return stats;
}

static LatencyStats bench_epoch_conversion(size_t iterations, double freq_ghz) {
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    std::string_view input{TIMESTAMP_IN_MSG};
    auto ts = parse_timestamp(input);

    warmup_icache([&]() {
        auto r = to_epoch_ms(*ts);
        compiler_barrier();
        (void)r;
    });

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t elapsed;
        {
            ScopedTimer timer(elapsed);
            auto epoch = to_epoch_ms(*ts);
            compiler_barrier();
            (void)epoch;
        }
        cycles.push_back(elapsed);
    }

    LatencyStats stats;
    stats.compute(cycles, freq_ghz);
    return stats;
}

static LatencyStats bench_full_pipeline(size_t iterations, double freq_ghz) {
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    std::string_view input{TIMESTAMP_IN_MSG};

    warmup_icache([&]() {
        auto ts = parse_timestamp(input);
        auto epoch = to_epoch_ms(*ts);
        compiler_barrier();
        (void)epoch;
    });

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t elapsed;
        {
            ScopedTimer timer(elapsed);
            auto ts = parse_timestamp(input);
            auto epoch = to_epoch_ms(*ts);
            compiler_barrier();
            (void)epoch;
        }
        cycles.push_back(elapsed);
    }

    LatencyStats stats;
    stats.compute(cycles, freq_ghz);
    return stats;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    size_t iterations = 100000;

    if (argc > 1) {
        iterations = std::stoul(argv[1]);
    }

    std::cout << "==========================================================\n";
    std::cout << "  NexusFIX Timestamp Parse Benchmark (TICKET_443)\n";
    std::cout << "==========================================================\n\n";

    std::cout << "Configuration:\n";
    std::cout << "  Iterations: " << iterations << "\n";
#if NFX_SIMD_AVAILABLE
    std::cout << "  SIMD:       AVX2 available\n";
#else
    std::cout << "  SIMD:       Scalar only\n";
#endif
    std::cout << "  Input:      \"" << TIMESTAMP_IN_MSG.substr(0, FIX_TIMESTAMP_LEN) << "\"\n";
    std::cout << "  Timing:     rdtsc_vm_safe (lfence serialized)\n";

    // Bind to core for stable measurements
    if (bind_to_core(0)) {
        std::cout << "  CPU Core:   0 (pinned)\n";
    }

    // Calibrate CPU frequency
    std::cout << "\nCalibrating CPU frequency (busy-wait)...\n";
    double freq_ghz = estimate_cpu_freq_ghz_busy();
    std::cout << "  CPU frequency: " << std::fixed << std::setprecision(3)
              << freq_ghz << " GHz\n";

    // ========================================================================
    // Individual benchmarks
    // ========================================================================

    std::cout << "\n----------------------------------------------------------\n";
    std::cout << "  Scalar vs SIMD Timestamp Parsing\n";
    std::cout << "----------------------------------------------------------\n";

    auto scalar_stats = bench_scalar(iterations, freq_ghz);
    print_stats("Scalar (parse_timestamp_scalar)", scalar_stats);

#if NFX_SIMD_AVAILABLE
    auto simd_stats = bench_simd(iterations, freq_ghz);
    print_stats("AVX2 (parse_timestamp_simd)", simd_stats);
#endif

    auto dispatch_stats = bench_dispatch(iterations, freq_ghz);
    print_stats("Dispatch (parse_timestamp)", dispatch_stats);

    auto epoch_stats = bench_epoch_conversion(iterations, freq_ghz);
    print_stats("Epoch Conversion (to_epoch_ms)", epoch_stats);

    auto pipeline_stats = bench_full_pipeline(iterations, freq_ghz);
    print_stats("Full Pipeline (parse + epoch)", pipeline_stats);

    // ========================================================================
    // Comparison
    // ========================================================================

#if NFX_SIMD_AVAILABLE
    std::cout << "\n----------------------------------------------------------\n";
    std::cout << "  Scalar vs AVX2 Comparison\n";
    std::cout << "----------------------------------------------------------\n\n";

    print_comparison_header("Scalar", "AVX2");
    print_comparison("Mean", scalar_stats.mean_ns, simd_stats.mean_ns);
    print_comparison("P50", scalar_stats.p50_ns, simd_stats.p50_ns);
    print_comparison("P99", scalar_stats.p99_ns, simd_stats.p99_ns);
    print_comparison("P99.9", scalar_stats.p999_ns, simd_stats.p999_ns);

    double speedup = scalar_stats.p50_ns / simd_stats.p50_ns;
    std::cout << "\n  Speedup (P50): " << std::fixed << std::setprecision(1)
              << speedup << "x\n";
#endif

    // ========================================================================
    // Summary
    // ========================================================================

    std::cout << "\n==========================================================\n";
    std::cout << "  TICKET_443 Performance Summary\n";
    std::cout << "==========================================================\n\n";

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Scalar parse:     " << std::setw(8) << scalar_stats.p50_ns << " ns (P50)\n";
#if NFX_SIMD_AVAILABLE
    std::cout << "  AVX2 parse:       " << std::setw(8) << simd_stats.p50_ns << " ns (P50)\n";
    std::cout << "  SIMD speedup:     " << std::setw(8) << (scalar_stats.p50_ns / simd_stats.p50_ns) << "x\n";
#endif
    std::cout << "  Epoch conversion: " << std::setw(8) << epoch_stats.p50_ns << " ns (P50)\n";
    std::cout << "  Full pipeline:    " << std::setw(8) << pipeline_stats.p50_ns << " ns (P50)\n";
    std::cout << "==========================================================\n";

    return 0;
}
