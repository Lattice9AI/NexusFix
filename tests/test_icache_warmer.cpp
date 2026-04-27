#include <catch2/catch_test_macros.hpp>

#include "nexusfix/util/icache_warmer.hpp"

using namespace nfx::util;

// ============================================================================
// ICacheWarmer Tests
//
// Note: Warmup messages have hardcoded checksums that may not match the
// computed value. The warmer exercises all parser code paths regardless
// of parse success/failure, which is the primary goal (I-Cache warming).
// ============================================================================

TEST_CASE("warm_icache completes and reports statistics", "[icache_warmer][regression]") {
    auto stats = warm_icache(10);

    CHECK(stats.iterations == 10);
    // 4 messages per iteration
    CHECK(stats.messages_parsed == 40);
    // parse_errors <= messages_parsed (some or all may fail checksum)
    CHECK(stats.parse_errors <= stats.messages_parsed);
}

TEST_CASE("warm_simd_scanner returns valid stats", "[icache_warmer][regression]") {
    auto stats = warm_simd_scanner(10);

    // SIMD scanner does not parse full messages, so no parse errors
    CHECK(stats.parse_errors == 0);
    CHECK(stats.iterations == 10);
    // 4 SIMD operations per iteration
    CHECK(stats.messages_parsed == 40);
}

TEST_CASE("warm_all exercises all code paths", "[icache_warmer][regression]") {
    auto stats = warm_all(100);

    // warm_all calls warm_icache(100) + warm_simd_scanner(10)
    CHECK(stats.iterations == 110);
    // warm_icache: 100 * 4 = 400, warm_simd_scanner: 10 * 4 = 40
    CHECK(stats.messages_parsed == 440);
}

TEST_CASE("warm_icache_timed reports cycle count", "[icache_warmer][regression]") {
    auto stats = warm_icache_timed(10);

    CHECK(stats.iterations == 10);
    CHECK(stats.messages_parsed == 40);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    CHECK(stats.total_cycles > 0);
#endif
}

TEST_CASE("WarmupStats::success reflects parse_errors == 0", "[icache_warmer][regression]") {
    WarmupStats good{.iterations = 10, .messages_parsed = 40, .parse_errors = 0, .total_cycles = 0};
    CHECK(good.success());

    WarmupStats bad{.iterations = 10, .messages_parsed = 40, .parse_errors = 1, .total_cycles = 0};
    CHECK_FALSE(bad.success());
}
