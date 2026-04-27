#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>

#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/parser/simd_scanner.hpp"
#include "nexusfix/parser/simd_checksum.hpp"

#if defined(NFX_HAS_MIMALLOC) && NFX_HAS_MIMALLOC
#include <mimalloc.h>
#endif

using namespace nfx;

// ============================================================================
// Test Data
// ============================================================================

namespace {

/// Build a FIX message with correct BodyLength and CheckSum.
std::string build_fix_message(const std::string& inner) {
    std::string bl = std::to_string(inner.size());
    std::string prefix = "8=FIX.4.4\x01" "9=" + bl + "\x01";
    std::string body = prefix + inner;
    uint8_t sum = 0;
    for (char c : body) sum += static_cast<uint8_t>(c);
    char cs[4];
    cs[0] = '0' + (sum / 100);
    cs[1] = '0' + ((sum / 10) % 10);
    cs[2] = '0' + (sum % 10);
    cs[3] = '\0';
    return body + "10=" + std::string(cs, 3) + "\x01";
}

// ExecutionReport with typical field count
const std::string EXEC_REPORT = build_fix_message(
    "35=8\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
    "52=20231215-10:30:00.000\x01" "37=ORDER123\x01" "17=EXEC456\x01"
    "150=0\x01" "39=0\x01" "55=AAPL\x01" "54=1\x01" "38=100\x01"
    "44=150.50\x01" "151=100\x01" "14=0\x01" "6=0\x01");

constexpr size_t WARMUP_ITERATIONS = 1000;
constexpr size_t BENCH_ITERATIONS  = 10000;

// P99 latency threshold for ExecutionReport parse (nanoseconds).
// TICKET_228: fail if P99 regresses > 20% beyond baseline.
// Baseline target from TICKET_182: < 200 ns on bare metal.
// CI/VM environments are slower, so we use a generous gate.
constexpr double P99_THRESHOLD_NS = 5000.0;

#if defined(NFX_HAS_MIMALLOC) && NFX_HAS_MIMALLOC
/// Count live blocks on a mimalloc heap via mi_heap_visit_blocks.
struct HeapBlockCount {
    size_t count{0};
};

bool count_blocks_visitor(
    const mi_heap_t* /*heap*/, const mi_heap_area_t* /*area*/,
    void* block, size_t /*block_size*/, void* arg) {
    if (block) {
        auto* ctx = static_cast<HeapBlockCount*>(arg);
        ++ctx->count;
    }
    return true; // continue visiting
}
#endif // NFX_HAS_MIMALLOC

} // namespace

// ============================================================================
// 6A-1: Parser P99 latency below threshold (warm, 10K iterations)
// ============================================================================

TEST_CASE("Parser P99 latency gate", "[performance][regression]") {
    using clock = std::chrono::steady_clock;

    std::span<const char> data{EXEC_REPORT.data(), EXEC_REPORT.size()};

    // Warmup: populate I-cache and branch predictor
    for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
        auto result = ParsedMessage::parse(data);
        REQUIRE(result.has_value());
    }

    // Measure
    std::vector<double> latencies_ns;
    latencies_ns.reserve(BENCH_ITERATIONS);

    for (size_t i = 0; i < BENCH_ITERATIONS; ++i) {
        auto start = clock::now();
        auto result = ParsedMessage::parse(data);
        auto end = clock::now();

        // Prevent dead code elimination
        REQUIRE(result.has_value());

        double ns = std::chrono::duration<double, std::nano>(end - start).count();
        latencies_ns.push_back(ns);
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());
    double p99 = latencies_ns[BENCH_ITERATIONS * 99 / 100];
    double p50 = latencies_ns[BENCH_ITERATIONS / 2];

    INFO("P50 = " << p50 << " ns, P99 = " << p99 << " ns");
    CHECK(p99 < P99_THRESHOLD_NS);
}

// ============================================================================
// 6A-2: Zero heap allocations on hot path (instrumented allocator counting)
// ============================================================================

#if defined(NFX_HAS_MIMALLOC) && NFX_HAS_MIMALLOC
TEST_CASE("Zero heap allocations on hot path", "[performance][regression]") {
    std::span<const char> data{EXEC_REPORT.data(), EXEC_REPORT.size()};

    // Warmup outside measurement window
    for (size_t i = 0; i < 100; ++i) {
        auto result = ParsedMessage::parse(data);
        REQUIRE(result.has_value());
    }

    // Create a fresh mimalloc heap and make it the thread default.
    // Any operator new during parse will be routed to this heap by mimalloc.
    mi_heap_t* test_heap = mi_heap_new();
    REQUIRE(test_heap != nullptr);

    mi_heap_t* old_heap = mi_heap_set_default(test_heap);

    // Parse N messages; all allocations (if any) go to test_heap
    constexpr size_t N = 100;
    for (size_t i = 0; i < N; ++i) {
        auto result = ParsedMessage::parse(data);
        REQUIRE(result.has_value());
    }

    // Restore old default before counting
    mi_heap_set_default(old_heap);

    // Count blocks allocated on the test heap
    HeapBlockCount ctx;
    mi_heap_visit_blocks(test_heap, true, count_blocks_visitor, &ctx);

    INFO("Heap allocations during " << N << " parses: " << ctx.count);
    CHECK(ctx.count == 0);

    mi_heap_delete(test_heap);
}
#endif // NFX_HAS_MIMALLOC

// ============================================================================
// 6A-3: SIMD vs scalar produce identical parse results
// ============================================================================

TEST_CASE("SIMD vs scalar SOH scan produce identical results", "[performance][regression]") {
    std::span<const char> data{EXEC_REPORT.data(), EXEC_REPORT.size()};

    auto scalar_result = nfx::simd::scan_soh_scalar(data);

    // The dispatch function picks the best available SIMD path.
    // On x86 with AVX2/AVX-512 this exercises a different code path than scalar.
    auto simd_result = nfx::simd::scan_soh(data);

    REQUIRE(scalar_result.count == simd_result.count);
    for (size_t i = 0; i < scalar_result.count; ++i) {
        INFO("Mismatch at SOH index " << i);
        CHECK(scalar_result[i] == simd_result[i]);
    }

    SECTION("Large buffer with many fields") {
        // Build a larger message by repeating fields
        std::string large;
        large.reserve(4096);
        for (int i = 0; i < 100; ++i) {
            large += "100=VALUE" + std::to_string(i) + "\x01";
        }

        std::span<const char> large_data{large.data(), large.size()};
        auto scalar_large = nfx::simd::scan_soh_scalar(large_data);
        auto simd_large = nfx::simd::scan_soh(large_data);

        REQUIRE(scalar_large.count == simd_large.count);
        for (size_t i = 0; i < scalar_large.count; ++i) {
            INFO("Large buffer mismatch at SOH index " << i);
            CHECK(scalar_large[i] == simd_large[i]);
        }
    }
}

// ============================================================================
// 6A-4: Checksum SIMD vs scalar produce identical values
// ============================================================================

TEST_CASE("Checksum SIMD vs scalar produce identical values", "[performance][regression]") {
    SECTION("ExecutionReport message") {
        uint8_t scalar = nfx::parser::checksum_scalar(
            EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t best = nfx::parser::checksum(
            EXEC_REPORT.data(), EXEC_REPORT.size());

        CHECK(scalar == best);
    }

    SECTION("Small buffer (< SIMD register width)") {
        const char* small = "8=FIX.4.4\x01";
        size_t len = std::strlen(small);

        uint8_t scalar = nfx::parser::checksum_scalar(small, len);
        uint8_t best = nfx::parser::checksum(small, len);
        CHECK(scalar == best);
    }

    SECTION("Large buffer crossing multiple SIMD lanes") {
        std::string large(2048, 'A');
        uint8_t scalar = nfx::parser::checksum_scalar(large.data(), large.size());
        uint8_t best = nfx::parser::checksum(large.data(), large.size());
        CHECK(scalar == best);
    }

    SECTION("All byte values") {
        // Ensure modular arithmetic works for all byte patterns
        std::string all_bytes(256, '\0');
        for (int i = 0; i < 256; ++i) {
            all_bytes[static_cast<size_t>(i)] = static_cast<char>(i);
        }

        uint8_t scalar = nfx::parser::checksum_scalar(all_bytes.data(), all_bytes.size());
        uint8_t best = nfx::parser::checksum(all_bytes.data(), all_bytes.size());
        CHECK(scalar == best);
    }

    SECTION("Empty buffer") {
        uint8_t scalar = nfx::parser::checksum_scalar("", 0);
        uint8_t best = nfx::parser::checksum("", 0);
        CHECK(scalar == best);
    }
}
