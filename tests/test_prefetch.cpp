#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "nexusfix/util/prefetch.hpp"

using namespace nfx::util;

// ============================================================================
// Prefetch Function Smoke Tests (verify no crash)
// ============================================================================

TEST_CASE("prefetch_read on valid pointer does not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[256]{};
    prefetch_read(buffer);
    prefetch_read(buffer + CACHE_LINE_SIZE);
    CHECK(true);
}

TEST_CASE("prefetch_write on valid pointer does not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[256]{};
    prefetch_write(buffer);
    prefetch_write(buffer + CACHE_LINE_SIZE);
    CHECK(true);
}

TEST_CASE("prefetch_read_nta and prefetch_write_nta do not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[256]{};
    prefetch_read_nta(buffer);
    prefetch_write_nta(buffer);
    CHECK(true);
}

TEST_CASE("prefetch with configurable locality does not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[256]{};
    prefetch<PrefetchLocality::None>(buffer);
    prefetch<PrefetchLocality::Low>(buffer);
    prefetch<PrefetchLocality::Medium>(buffer);
    prefetch<PrefetchLocality::High>(buffer);
    prefetch<PrefetchLocality::High>(buffer, true);  // for_write
    CHECK(true);
}

TEST_CASE("prefetch_range on allocated buffer does not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[CACHE_LINE_SIZE * 16]{};
    prefetch_range<8>(buffer);
    prefetch_range<16>(buffer);
    CHECK(true);
}

TEST_CASE("prefetch_ahead and prefetch_ahead_lines do not crash", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buffer[1024]{};
    prefetch_ahead(buffer, 128);
    prefetch_ahead_lines(buffer, 4);
    CHECK(true);
}

TEST_CASE("prefetch_for_iteration within bounds does not crash", "[prefetch][regression]") {
    std::vector<int> data(1024, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        prefetch_for_iteration(data.data(), i, data.size());
    }
    CHECK(true);
}

TEST_CASE("prefetch_elements_ahead does not crash", "[prefetch][regression]") {
    std::vector<int> data(1024, 0);
    prefetch_elements_ahead(data.data());
    CHECK(true);
}

TEST_CASE("prefetch_fence does not crash", "[prefetch][regression]") {
    prefetch_fence();
    CHECK(true);
}

// ============================================================================
// Cache Line Utility Tests
// ============================================================================

TEST_CASE("align_to_cache_line rounds up correctly", "[prefetch][regression]") {
    CHECK(align_to_cache_line(0) == 0);
    CHECK(align_to_cache_line(1) == CACHE_LINE_SIZE);
    CHECK(align_to_cache_line(63) == CACHE_LINE_SIZE);
    CHECK(align_to_cache_line(64) == CACHE_LINE_SIZE);
    CHECK(align_to_cache_line(65) == 2 * CACHE_LINE_SIZE);
    CHECK(align_to_cache_line(128) == 128);
    CHECK(align_to_cache_line(129) == 3 * CACHE_LINE_SIZE);
}

TEST_CASE("is_cache_aligned on aligned and unaligned pointers", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char aligned_buf[128]{};
    CHECK(is_cache_aligned(aligned_buf));

    // Offset by 1 byte should not be aligned
    CHECK_FALSE(is_cache_aligned(aligned_buf + 1));
    CHECK_FALSE(is_cache_aligned(aligned_buf + 32));

    // Next cache line should be aligned
    CHECK(is_cache_aligned(aligned_buf + CACHE_LINE_SIZE));
}

TEST_CASE("cache_line_offset returns correct offset", "[prefetch][regression]") {
    alignas(CACHE_LINE_SIZE) char buf[128]{};

    CHECK(cache_line_offset(buf) == 0);
    CHECK(cache_line_offset(buf + 1) == 1);
    CHECK(cache_line_offset(buf + 32) == 32);
    CHECK(cache_line_offset(buf + 63) == 63);
    CHECK(cache_line_offset(buf + CACHE_LINE_SIZE) == 0);
}

// ============================================================================
// Constant Validation
// ============================================================================

TEST_CASE("Cache constants are reasonable", "[prefetch][regression]") {
    CHECK(CACHE_LINE_SIZE == 64);
    CHECK(L1_CACHE_SIZE == 32 * 1024);
    CHECK(L2_CACHE_SIZE == 256 * 1024);
    CHECK(DEFAULT_PREFETCH_DISTANCE == 8);
}
