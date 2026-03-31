// Measure field loop overhead only (excluding parse_header)
#include <iostream>
#include <span>
#include <vector>
#include <algorithm>
#include <numeric>
#include "nexusfix/parser/simd_scanner.hpp"
#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/interfaces/i_message.hpp"
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

// Test message starting AFTER header (skip BeginString, BodyLength, MsgType, etc.)
// This is the body: 37=ORDER123|17=EXEC456|150=0|39=0|55=AAPL|54=1|38=100|44=150.50|151=100|14=0|6=0|10=004|
const char* BODY_ONLY =
    "37=ORDER123\x01" "17=EXEC456\x01" "150=0\x01" "39=0\x01" "55=AAPL\x01"
    "54=1\x01" "38=100\x01" "44=150.50\x01" "151=100\x01" "14=0\x01"
    "6=0\x01" "10=004\x01";

constexpr size_t BODY_LEN = 98;  // Pre-calculated

// Simulate the field parsing loop
uint64_t parse_field_loop(std::span<const char> data) {
    auto soh_positions = nfx::simd::scan_soh(data);
    const char* __restrict ptr = data.data();

    size_t field_start = 0;
    int field_count = 0;

    for (size_t i = 0; i < soh_positions.count; ++i) {
        size_t field_end = soh_positions[i];

        // Find '=' separator (current implementation)
        size_t eq_pos = field_start;
        while (eq_pos < field_end && ptr[eq_pos] != nfx::fix::EQUALS) {
            eq_pos++;
        }

        if (eq_pos >= field_end) {
            return 0; // Error
        }

        // Parse tag
        int tag = 0;
        for (size_t j = field_start; j < eq_pos; ++j) {
            char c = ptr[j];
            if (c < '0' || c > '9') {
                return 0; // Error
            }
            tag = tag * 10 + (c - '0');
        }

        field_count++;
        field_start = field_end + 1;
    }

    return field_count;
}

} // anonymous namespace

int main() {
    std::cout << "Field Loop Only Benchmark\n";
    std::cout << "=========================\n";
    std::cout << "Message body: 12 fields, 98 bytes (no header)\n\n";

    std::span<const char> data(BODY_ONLY, BODY_LEN);
    constexpr size_t iterations = 100'000;

    if (!nfx::bench::bind_to_core(0)) {
        std::cerr << "Warning: Failed to bind to core 0\n";
    }

    // Warmup
    for (size_t i = 0; i < 10000; ++i) {
        [[maybe_unused]] auto result = parse_field_loop(data);
    }

    // Measure
    LatencyStats stats;
    for (size_t i = 0; i < iterations; ++i) {
        auto start = nfx::bench::rdtsc_vm_safe();
        [[maybe_unused]] auto result = parse_field_loop(data);
        auto end = nfx::bench::rdtsc_vm_safe();
        stats.add(end - start);
    }

    stats.compute();

    std::cout << "Field loop (12 fields): P50=" << stats.p50()
              << " cycles, mean=" << stats.mean() << "\n";
    std::cout << "Average per field: " << (stats.mean() / 12) << " cycles\n";

    return 0;
}
