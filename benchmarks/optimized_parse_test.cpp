// Test optimized parse approach
#include <iostream>
#include <span>
#include <vector>
#include <algorithm>
#include <numeric>
#include "nexusfix/parser/simd_scanner.hpp"
#include "nexusfix/parser/consteval_parser.hpp"
#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/types/error.hpp"
#include "benchmark_utils.hpp"

namespace {

struct LatencyStats {
    std::vector<uint64_t> samples;
    void add(uint64_t cycles) { samples.push_back(cycles); }
    void compute() { std::sort(samples.begin(), samples.end()); }
    uint64_t p50() const { return samples[samples.size() / 2]; }
    uint64_t mean() const {
        return std::accumulate(samples.begin(), samples.end(), 0ULL) / samples.size();
    }
};

const char* EXEC_REPORT =
    "8=FIX.4.4\x01" "9=136\x01" "35=8\x01" "49=SENDER\x01" "56=TARGET\x01"
    "34=1\x01" "52=20231215-10:30:00.000\x01" "37=ORDER123\x01" "17=EXEC456\x01"
    "150=0\x01" "39=0\x01" "55=AAPL\x01" "54=1\x01" "38=100\x01" "44=150.50\x01"
    "151=100\x01" "14=0\x01" "6=0\x01" "10=000\x01";

constexpr size_t MSG_LEN = 159;

// Original approach (calls find_equals for each field)
uint64_t parse_original(std::span<const char> data) {
    auto soh_positions = nfx::simd::scan_soh(data);
    const char* __restrict ptr = data.data();

    size_t field_start = 0;
    int field_count = 0;

    for (size_t i = 0; i < soh_positions.count; ++i) {
        size_t field_end = soh_positions[i];

        // Find '=' separator - THIS IS THE BOTTLENECK
        size_t eq_pos = nfx::simd::find_equals(data, field_start);

        if (eq_pos >= field_end) {
            return 0; // Error
        }

        // Parse tag
        int tag = 0;
        for (size_t j = field_start; j < eq_pos; ++j) {
            tag = tag * 10 + (ptr[j] - '0');
        }

        field_count++;
        field_start = field_end + 1;
    }

    return field_count;
}

// Optimized approach (simple scalar search for '=' since tag is short)
uint64_t parse_optimized(std::span<const char> data) {
    auto soh_positions = nfx::simd::scan_soh(data);
    const char* __restrict ptr = data.data();

    size_t field_start = 0;
    int field_count = 0;

    for (size_t i = 0; i < soh_positions.count; ++i) {
        size_t field_end = soh_positions[i];

        // Simple scalar search for '=' (tag is typically 2-4 chars, SIMD overhead not worth it)
        size_t eq_pos = field_start;
        while (eq_pos < field_end && ptr[eq_pos] != '=') {
            eq_pos++;
        }

        if (eq_pos >= field_end) {
            return 0; // Error
        }

        // Parse tag
        int tag = 0;
        for (size_t j = field_start; j < eq_pos; ++j) {
            tag = tag * 10 + (ptr[j] - '0');
        }

        field_count++;
        field_start = field_end + 1;
    }

    return field_count;
}

} // anonymous namespace

int main() {
    std::cout << "Optimized Parse Test\n";
    std::cout << "====================\n\n";

    std::span<const char> data(EXEC_REPORT, MSG_LEN);
    constexpr size_t iterations = 100'000;

    if (!nfx::bench::bind_to_core(0)) {
        std::cerr << "Warning: Failed to bind to core 0\n";
    }

    // Warmup
    for (size_t i = 0; i < 10000; ++i) {
        [[maybe_unused]] auto r1 = parse_original(data);
        [[maybe_unused]] auto r2 = parse_optimized(data);
    }

    // Benchmark original
    LatencyStats stats_original;
    for (size_t i = 0; i < iterations; ++i) {
        auto start = nfx::bench::rdtsc_vm_safe();
        [[maybe_unused]] auto result = parse_original(data);
        auto end = nfx::bench::rdtsc_vm_safe();
        stats_original.add(end - start);
    }
    stats_original.compute();

    // Benchmark optimized
    LatencyStats stats_optimized;
    for (size_t i = 0; i < iterations; ++i) {
        auto start = nfx::bench::rdtsc_vm_safe();
        [[maybe_unused]] auto result = parse_optimized(data);
        auto end = nfx::bench::rdtsc_vm_safe();
        stats_optimized.add(end - start);
    }
    stats_optimized.compute();

    std::cout << "Original (find_equals per field):  P50=" << stats_original.p50()
              << " cycles, mean=" << stats_original.mean() << "\n";
    std::cout << "Optimized (scalar search):         P50=" << stats_optimized.p50()
              << " cycles, mean=" << stats_optimized.mean() << "\n";

    double speedup = static_cast<double>(stats_original.p50()) / stats_optimized.p50();
    std::cout << "\nSpeedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";

    return 0;
}
