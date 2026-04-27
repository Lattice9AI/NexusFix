#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nexusfix/util/bit_utils.hpp"
#include "nexusfix/util/branchless.hpp"
#include "nexusfix/util/string_hash.hpp"
#include "nexusfix/util/format_utils.hpp"
#include "nexusfix/util/branch_hints.hpp"

// compiler.hpp redefines NFX_ASSUME/NFX_UNREACHABLE - undef branch_hints versions first
#undef NFX_ASSUME
#undef NFX_UNREACHABLE
#include "nexusfix/util/compiler.hpp"

#include <cstring>

using namespace nfx::util;

// ============================================================================
// Bit Utils - Byte Parsing
// ============================================================================

TEST_CASE("Byte parsing little-endian", "[utils][bit_utils][regression]") {
    std::byte data[] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };

    SECTION("parse_u16_le") {
        REQUIRE(parse_u16_le(data) == 0x0201);
    }

    SECTION("parse_u32_le") {
        REQUIRE(parse_u32_le(data) == 0x04030201u);
    }

    SECTION("parse_u64_le") {
        REQUIRE(parse_u64_le(data) == 0x0807060504030201ull);
    }
}

TEST_CASE("Byte parsing big-endian", "[utils][bit_utils][regression]") {
    std::byte data[] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };

    SECTION("parse_u16_be") {
        REQUIRE(parse_u16_be(data) == 0x0102);
    }

    SECTION("parse_u32_be") {
        REQUIRE(parse_u32_be(data) == 0x01020304u);
    }

    SECTION("parse_u64_be") {
        REQUIRE(parse_u64_be(data) == 0x0102030405060708ull);
    }
}

// ============================================================================
// Bit Utils - Byte Swap
// ============================================================================

TEST_CASE("Byte swap", "[utils][bit_utils][regression]") {
    REQUIRE(byteswap16(0x0102) == 0x0201);
    REQUIRE(byteswap32(0x01020304u) == 0x04030201u);
    REQUIRE(byteswap64(0x0102030405060708ull) == 0x0807060504030201ull);
}

// ============================================================================
// Bit Utils - Bit Manipulation
// ============================================================================

TEST_CASE("Bit manipulation", "[utils][bit_utils][regression]") {
    SECTION("countl_zero") {
        REQUIRE(countl_zero(uint32_t{1}) == 31);
        REQUIRE(countl_zero(uint32_t{0x80000000}) == 0);
        REQUIRE(countl_zero(uint32_t{0}) == 32);
    }

    SECTION("countr_zero") {
        REQUIRE(countr_zero(uint32_t{1}) == 0);
        REQUIRE(countr_zero(uint32_t{8}) == 3);
        REQUIRE(countr_zero(uint32_t{0}) == 32);
    }

    SECTION("popcount") {
        REQUIRE(popcount(uint32_t{0}) == 0);
        REQUIRE(popcount(uint32_t{1}) == 1);
        REQUIRE(popcount(uint32_t{0xFF}) == 8);
        REQUIRE(popcount(uint32_t{0xFFFFFFFF}) == 32);
    }

    SECTION("is_power_of_two") {
        REQUIRE(is_power_of_two(uint32_t{1}));
        REQUIRE(is_power_of_two(uint32_t{2}));
        REQUIRE(is_power_of_two(uint32_t{1024}));
        REQUIRE_FALSE(is_power_of_two(uint32_t{0}));
        REQUIRE_FALSE(is_power_of_two(uint32_t{3}));
    }

    SECTION("next_power_of_two") {
        REQUIRE(next_power_of_two(uint32_t{1}) == 1);
        REQUIRE(next_power_of_two(uint32_t{3}) == 4);
        REQUIRE(next_power_of_two(uint32_t{5}) == 8);
        REQUIRE(next_power_of_two(uint32_t{1024}) == 1024);
    }

    SECTION("prev_power_of_two") {
        REQUIRE(prev_power_of_two(uint32_t{1}) == 1);
        REQUIRE(prev_power_of_two(uint32_t{3}) == 2);
        REQUIRE(prev_power_of_two(uint32_t{7}) == 4);
        REQUIRE(prev_power_of_two(uint32_t{1024}) == 1024);
    }

    SECTION("bit_width") {
        REQUIRE(bit_width(uint32_t{0}) == 0);
        REQUIRE(bit_width(uint32_t{1}) == 1);
        REQUIRE(bit_width(uint32_t{255}) == 8);
        REQUIRE(bit_width(uint32_t{256}) == 9);
    }
}

// ============================================================================
// Bit Utils - Bit Field Operations
// ============================================================================

TEST_CASE("Bit field operations", "[utils][bit_utils][regression]") {
    SECTION("extract_bits") {
        REQUIRE(extract_bits(uint32_t{0xFF00}, 8, 8) == 0xFF);
        REQUIRE(extract_bits(uint32_t{0b11010}, 1, 4) == 0b1101);
    }

    SECTION("insert_bits") {
        uint32_t val = 0;
        val = insert_bits(val, uint32_t{0xFF}, 8, 8);
        REQUIRE(val == 0xFF00);
    }

    SECTION("set/clear/toggle/test bit") {
        uint32_t val = 0;
        val = set_bit(val, 3);
        REQUIRE(val == 8);
        REQUIRE(test_bit(val, 3));
        REQUIRE_FALSE(test_bit(val, 2));

        val = toggle_bit(val, 3);
        REQUIRE(val == 0);

        val = set_bit(val, 5);
        val = clear_bit(val, 5);
        REQUIRE(val == 0);
    }
}

// ============================================================================
// Bit Utils - Alignment
// ============================================================================

TEST_CASE("Alignment utilities", "[utils][bit_utils][regression]") {
    SECTION("is_aligned") {
        REQUIRE(is_aligned(uint64_t{64}, uint64_t{64}));
        REQUIRE(is_aligned(uint64_t{128}, uint64_t{64}));
        REQUIRE_FALSE(is_aligned(uint64_t{65}, uint64_t{64}));
        REQUIRE(is_aligned(uint64_t{0}, uint64_t{64}));
    }

    SECTION("align_up") {
        REQUIRE(align_up(uint64_t{1}, uint64_t{64}) == 64);
        REQUIRE(align_up(uint64_t{64}, uint64_t{64}) == 64);
        REQUIRE(align_up(uint64_t{65}, uint64_t{64}) == 128);
    }

    SECTION("align_down") {
        REQUIRE(align_down(uint64_t{65}, uint64_t{64}) == 64);
        REQUIRE(align_down(uint64_t{127}, uint64_t{64}) == 64);
        REQUIRE(align_down(uint64_t{128}, uint64_t{64}) == 128);
    }
}

// ============================================================================
// Bit Utils - Fixed-Point Arithmetic
// ============================================================================

TEST_CASE("Fixed-point arithmetic", "[utils][bit_utils][regression]") {
    SECTION("to_fixed and from_fixed roundtrip") {
        double price = 123.456;
        int64_t fixed = to_fixed<8>(price);
        double back = from_fixed<8>(fixed);
        REQUIRE_THAT(back, Catch::Matchers::WithinRel(price, 1e-8));
    }

    SECTION("to_fixed with different decimals") {
        REQUIRE(to_fixed<2>(1.23) == 123);
        REQUIRE(to_fixed<4>(1.5) == 15000);
    }

    SECTION("negative values") {
        int64_t fixed = to_fixed<2>(-1.5);
        REQUIRE(fixed == -150);
        REQUIRE_THAT(from_fixed<2>(fixed), Catch::Matchers::WithinRel(-1.5, 1e-10));
    }
}

// ============================================================================
// Branchless - Min/Max
// ============================================================================

TEST_CASE("Branchless min/max signed", "[utils][branchless][regression]") {
    REQUIRE(branchless_min(3, 5) == 3);
    REQUIRE(branchless_min(-1, 1) == -1);
    REQUIRE(branchless_min(0, 0) == 0);

    REQUIRE(branchless_max(3, 5) == 5);
    REQUIRE(branchless_max(-1, 1) == 1);
    REQUIRE(branchless_max(0, 0) == 0);
}

TEST_CASE("Branchless min/max unsigned", "[utils][branchless][regression]") {
    REQUIRE(branchless_min(3u, 5u) == 3u);
    REQUIRE(branchless_min(0u, 10u) == 0u);

    REQUIRE(branchless_max(3u, 5u) == 5u);
    REQUIRE(branchless_max(0u, 10u) == 10u);
}

// ============================================================================
// Branchless - Select, Clamp, Abs, Sign
// ============================================================================

TEST_CASE("Branchless select", "[utils][branchless][regression]") {
    REQUIRE(branchless_select(true, 10, 20) == 10);
    REQUIRE(branchless_select(false, 10, 20) == 20);
}

TEST_CASE("Branchless clamp", "[utils][branchless][regression]") {
    REQUIRE(branchless_clamp(5, 0, 10) == 5);
    REQUIRE(branchless_clamp(-5, 0, 10) == 0);
    REQUIRE(branchless_clamp(15, 0, 10) == 10);

    REQUIRE(branchless_clamp(5u, 2u, 8u) == 5u);
    REQUIRE(branchless_clamp(0u, 2u, 8u) == 2u);
    REQUIRE(branchless_clamp(10u, 2u, 8u) == 8u);
}

TEST_CASE("Branchless abs", "[utils][branchless][regression]") {
    REQUIRE(branchless_abs(5) == 5);
    REQUIRE(branchless_abs(-5) == 5);
    REQUIRE(branchless_abs(0) == 0);
}

TEST_CASE("Branchless sign functions", "[utils][branchless][regression]") {
    SECTION("branchless_sign") {
        REQUIRE(branchless_sign(10) == 1);
        REQUIRE(branchless_sign(-10) == -1);
        REQUIRE(branchless_sign(0) == 0);
    }

    SECTION("branchless_signum") {
        REQUIRE(branchless_signum(10) == 1);
        REQUIRE(branchless_signum(-10) == -1);
        REQUIRE(branchless_signum(0) == 1);
    }

    SECTION("branchless_same_sign") {
        REQUIRE(branchless_same_sign(5, 10));
        REQUIRE(branchless_same_sign(-5, -10));
        REQUIRE_FALSE(branchless_same_sign(5, -10));
    }
}

// ============================================================================
// Branchless - Comparisons
// ============================================================================

TEST_CASE("Branchless comparisons", "[utils][branchless][regression]") {
    REQUIRE(bool_to_mask32(true) == -1);
    REQUIRE(bool_to_mask32(false) == 0);

    REQUIRE(branchless_lt(3, 5) == 1);
    REQUIRE(branchless_lt(5, 3) == 0);
    REQUIRE(branchless_gt(5, 3) == 1);
    REQUIRE(branchless_eq(3, 3) == 1);
    REQUIRE(branchless_eq(3, 4) == 0);
}

// ============================================================================
// Branchless - Range Checks and ASCII
// ============================================================================

TEST_CASE("Branchless range and ASCII", "[utils][branchless][regression]") {
    SECTION("in_range") {
        REQUIRE(in_range(5, 0, 10));
        REQUIRE(in_range(0, 0, 10));
        REQUIRE(in_range(10, 0, 10));
        REQUIRE_FALSE(in_range(11, 0, 10));
    }

    SECTION("is_digit") {
        REQUIRE(is_digit('0'));
        REQUIRE(is_digit('9'));
        REQUIRE_FALSE(is_digit('A'));
        REQUIRE_FALSE(is_digit('/'));
    }

    SECTION("is_alpha") {
        REQUIRE(is_alpha('A'));
        REQUIRE(is_alpha('z'));
        REQUIRE_FALSE(is_alpha('0'));
        REQUIRE_FALSE(is_alpha(' '));
    }

    SECTION("is_alnum") {
        REQUIRE(is_alnum('A'));
        REQUIRE(is_alnum('5'));
        REQUIRE_FALSE(is_alnum('!'));
    }

    SECTION("to_upper / to_lower") {
        REQUIRE(to_upper('a') == 'A');
        REQUIRE(to_upper('A') == 'A');
        REQUIRE(to_upper('5') == '5');

        REQUIRE(to_lower('A') == 'a');
        REQUIRE(to_lower('a') == 'a');
        REQUIRE(to_lower('5') == '5');
    }

    SECTION("digit_to_int / int_to_digit") {
        REQUIRE(digit_to_int('0') == 0);
        REQUIRE(digit_to_int('9') == 9);
        REQUIRE(int_to_digit(0) == '0');
        REQUIRE(int_to_digit(9) == '9');
    }
}

// ============================================================================
// Branchless - Integer Parsing
// ============================================================================

TEST_CASE("Branchless integer parsing", "[utils][branchless][regression]") {
    SECTION("parse_digit") {
        REQUIRE(parse_digit('0') == 0);
        REQUIRE(parse_digit('5') == 5);
        REQUIRE(parse_digit('9') == 9);
        REQUIRE(parse_digit('A') == -1);
        REQUIRE(parse_digit('/') == -1);
    }

    SECTION("parse_2digits") {
        REQUIRE(parse_2digits("00") == 0);
        REQUIRE(parse_2digits("42") == 42);
        REQUIRE(parse_2digits("99") == 99);
        REQUIRE(parse_2digits("A0") == -1);
        REQUIRE(parse_2digits("0A") == -1);
    }

    SECTION("parse_4digits") {
        REQUIRE(parse_4digits("0000") == 0);
        REQUIRE(parse_4digits("1234") == 1234);
        REQUIRE(parse_4digits("9999") == 9999);
        REQUIRE(parse_4digits("12A4") == -1);
    }
}

// ============================================================================
// Branchless - Conditional Arithmetic
// ============================================================================

TEST_CASE("Branchless conditional arithmetic", "[utils][branchless][regression]") {
    REQUIRE(conditional_add(10, 5, true) == 15);
    REQUIRE(conditional_add(10, 5, false) == 10);

    REQUIRE(conditional_sub(10, 3, true) == 7);
    REQUIRE(conditional_sub(10, 3, false) == 10);

    REQUIRE(conditional_inc(10, true) == 11);
    REQUIRE(conditional_inc(10, false) == 10);

    REQUIRE(conditional_dec(10, true) == 9);
    REQUIRE(conditional_dec(10, false) == 10);
}

// ============================================================================
// String Hash - FNV-1a
// ============================================================================

TEST_CASE("FNV-1a hash compile-time", "[utils][string_hash][regression]") {
    SECTION("32-bit hash") {
        constexpr auto h1 = fnv1a_hash32("hello");
        constexpr auto h2 = fnv1a_hash32("world");
        STATIC_REQUIRE(h1 != h2);

        // Empty string should hash to offset basis
        constexpr auto empty = fnv1a_hash32("");
        STATIC_REQUIRE(empty == 2166136261u);
    }

    SECTION("64-bit hash") {
        constexpr auto h1 = fnv1a_hash64("hello");
        constexpr auto h2 = fnv1a_hash64("world");
        STATIC_REQUIRE(h1 != h2);

        constexpr auto empty = fnv1a_hash64("");
        STATIC_REQUIRE(empty == 14695981039346656037ull);
    }

    SECTION("default hash is 64-bit") {
        constexpr auto h64 = fnv1a_hash64("test");
        constexpr auto hdef = fnv1a_hash("test");
        STATIC_REQUIRE(h64 == hdef);
    }
}

TEST_CASE("FNV-1a hash runtime matches compile-time", "[utils][string_hash][regression]") {
    SECTION("32-bit") {
        constexpr auto ct = fnv1a_hash32("FIX.4.4");
        auto rt = fnv1a_hash32_runtime("FIX.4.4");
        REQUIRE(ct == rt);
    }

    SECTION("64-bit") {
        constexpr auto ct = fnv1a_hash64("FIX.4.4");
        auto rt = fnv1a_hash64_runtime("FIX.4.4");
        REQUIRE(ct == rt);
    }
}

TEST_CASE("String hash user-defined literals", "[utils][string_hash][regression]") {
    using namespace nfx::util::literals;

    constexpr auto h1 = "D"_hash;
    constexpr auto h2 = "8"_hash;
    STATIC_REQUIRE(h1 != h2);

    constexpr auto h32 = "D"_hash32;
    REQUIRE(h32 != 0);
}

// ============================================================================
// String Hash - Pre-computed FIX Hashes
// ============================================================================

TEST_CASE("Pre-computed FIX message type hashes", "[utils][string_hash][regression]") {
    using namespace nfx::util::fix_msg_type_hash;

    // Verify runtime dispatch matches
    REQUIRE(fnv1a_hash_runtime("0") == HEARTBEAT);
    REQUIRE(fnv1a_hash_runtime("1") == TEST_REQUEST);
    REQUIRE(fnv1a_hash_runtime("2") == RESEND_REQUEST);
    REQUIRE(fnv1a_hash_runtime("3") == REJECT);
    REQUIRE(fnv1a_hash_runtime("A") == LOGON);
    REQUIRE(fnv1a_hash_runtime("5") == LOGOUT);
    REQUIRE(fnv1a_hash_runtime("D") == NEW_ORDER_SINGLE);
    REQUIRE(fnv1a_hash_runtime("8") == EXECUTION_REPORT);
    REQUIRE(fnv1a_hash_runtime("F") == ORDER_CANCEL_REQUEST);
    REQUIRE(fnv1a_hash_runtime("V") == MARKET_DATA_REQUEST);
}

TEST_CASE("Pre-computed FIX version hashes", "[utils][string_hash][regression]") {
    using namespace nfx::util::fix_version_hash;

    REQUIRE(fnv1a_hash_runtime("FIX.4.0") == FIX_4_0);
    REQUIRE(fnv1a_hash_runtime("FIX.4.4") == FIX_4_4);
    REQUIRE(fnv1a_hash_runtime("FIXT.1.1") == FIXT_1_1);
}

// ============================================================================
// String Hash - Hash Comparison
// ============================================================================

TEST_CASE("Hash-based string comparison", "[utils][string_hash][regression]") {
    SECTION("hash_equals") {
        constexpr auto target = fnv1a_hash("D");
        REQUIRE(hash_equals<target>("D"));
        REQUIRE_FALSE(hash_equals<target>("8"));
    }

    SECTION("hash_compare") {
        REQUIRE(hash_compare("hello", "hello"));
        REQUIRE_FALSE(hash_compare("hello", "world"));
        REQUIRE_FALSE(hash_compare("hi", "hello"));  // Different lengths
    }
}

// ============================================================================
// Format Utils
// ============================================================================

TEST_CASE("format basic", "[utils][format][regression]") {
    REQUIRE(format("{} + {} = {}", 1, 2, 3) == "1 + 2 = 3");
    REQUIRE(format("hello {}", "world") == "hello world");
}

TEST_CASE("format_to_buffer", "[utils][format][regression]") {
    char buf[64];
    size_t written = format_to_buffer(buf, sizeof(buf), "tag={}", 35);
    REQUIRE(std::string_view(buf, written) == "tag=35");
}

TEST_CASE("format_session_id", "[utils][format][regression]") {
    REQUIRE(format_session_id("SENDER", "TARGET") == "SENDER:TARGET");
}

TEST_CASE("format_seq_gap", "[utils][format][regression]") {
    auto s = format_seq_gap(5, 10);
    REQUIRE(s == "Sequence gap: expected 5, received 10");
}

TEST_CASE("format_bytes", "[utils][format][regression]") {
    REQUIRE(format_bytes(500) == "500 bytes");
    REQUIRE(format_bytes(2048).find("KB") != std::string::npos);
    REQUIRE(format_bytes(2 * 1024 * 1024).find("MB") != std::string::npos);
    REQUIRE(format_bytes(2ull * 1024 * 1024 * 1024).find("GB") != std::string::npos);
}

TEST_CASE("format_latency_ns", "[utils][format][regression]") {
    REQUIRE(format_latency_ns(500) == "500 ns");
    REQUIRE(format_latency_ns(5000).find("us") != std::string::npos);
    REQUIRE(format_latency_ns(5'000'000).find("ms") != std::string::npos);
    REQUIRE(format_latency_ns(5'000'000'000).find(" s") != std::string::npos);
}

TEST_CASE("format_throughput", "[utils][format][regression]") {
    REQUIRE(format_throughput(500).find("msg/s") != std::string::npos);
    REQUIRE(format_throughput(5000).find("K msg/s") != std::string::npos);
    REQUIRE(format_throughput(5'000'000).find("M msg/s") != std::string::npos);
}

TEST_CASE("format_tag_value", "[utils][format][regression]") {
    REQUIRE(format_tag_value(35, "D") == "35=D");
}

TEST_CASE("format_address", "[utils][format][regression]") {
    REQUIRE(format_address("127.0.0.1", 9876) == "127.0.0.1:9876");
}

TEST_CASE("format_parse_error", "[utils][format][regression]") {
    auto err = format_parse_error("invalid tag", 35, 10);
    REQUIRE(err.find("tag=35") != std::string::npos);
    REQUIRE(err.find("offset=10") != std::string::npos);

    auto err2 = format_parse_error("bad data", 0, 5);
    REQUIRE(err2.find("tag=") == std::string::npos);
    REQUIRE(err2.find("offset=5") != std::string::npos);
}

TEST_CASE("format_transport_error", "[utils][format][regression]") {
    auto err = format_transport_error("connection refused", 111);
    REQUIRE(err.find("errno=111") != std::string::npos);

    auto err2 = format_transport_error("timeout", 0);
    REQUIRE(err2.find("errno") == std::string::npos);
}

TEST_CASE("format_hex", "[utils][format][regression]") {
    std::byte data[] = {std::byte{0xAB}, std::byte{0xCD}, std::byte{0xEF}};
    auto hex = format_hex(std::span<const std::byte>(data, 3));
    REQUIRE(hex == "AB CD EF");

    SECTION("truncation with max_bytes") {
        auto truncated = format_hex(std::span<const std::byte>(data, 3), 2);
        REQUIRE(truncated.find("AB") != std::string::npos);
        REQUIRE(truncated.find("more") != std::string::npos);
    }
}

// ============================================================================
// Branch Hints - Compilation Smoke Tests (TICKET_479 Phase 7C)
// ============================================================================

TEST_CASE("NFX_LIKELY and NFX_UNLIKELY preserve boolean semantics",
          "[branch_hints][regression]") {
    REQUIRE(NFX_LIKELY(true));
    REQUIRE_FALSE(NFX_LIKELY(false));
    REQUIRE_FALSE(NFX_UNLIKELY(false));
    REQUIRE(NFX_UNLIKELY(true));

    SECTION("integer expressions coerced correctly") {
        int x = 42;
        REQUIRE(NFX_LIKELY(x));
        REQUIRE_FALSE(NFX_LIKELY(0));
        REQUIRE_FALSE(NFX_UNLIKELY(0));
        REQUIRE(NFX_UNLIKELY(x));
    }
}

TEST_CASE("NFX_ASSUME and NFX_UNREACHABLE compile",
          "[branch_hints][compiler][regression]") {
    // NFX_ASSUME: compiler may use the hint for optimization
    NFX_ASSUME(true);
    NFX_ASSUME(1 + 1 == 2);

    // NFX_UNREACHABLE: not invoked, just verify compilation in dead code
    int val = 1;
    if (val == 1) {
        REQUIRE(val == 1);
    } else {
        NFX_UNREACHABLE();
    }
}

TEST_CASE("NFX_HOT, NFX_COLD, NFX_FORCE_INLINE, NFX_NOINLINE compile",
          "[branch_hints][regression]") {
    struct Local {
        NFX_HOT static int hot_func(int x) { return x + 1; }
        NFX_COLD static int cold_func(int x) { return x - 1; }
        NFX_FORCE_INLINE static int inlined_func(int x) { return x * 2; }
        NFX_NOINLINE static int noinlined_func(int x) { return x / 2; }
    };

    REQUIRE(Local::hot_func(5) == 6);
    REQUIRE(Local::cold_func(5) == 4);
    REQUIRE(Local::inlined_func(3) == 6);
    REQUIRE(Local::noinlined_func(10) == 5);
}

TEST_CASE("NFX_PURE and NFX_CONST function attributes compile",
          "[branch_hints][regression]") {
    struct Local {
        NFX_PURE static int pure_func(const int* p) { return *p + 1; }
        NFX_CONST static int const_func(int x) { return x * 2; }
    };

    int v = 10;
    REQUIRE(Local::pure_func(&v) == 11);
    REQUIRE(Local::const_func(5) == 10);
}

TEST_CASE("NFX_PREFETCH_READ and NFX_PREFETCH_WRITE do not crash",
          "[branch_hints][regression]") {
    alignas(64) int buffer[16] = {};
    NFX_PREFETCH_READ(&buffer[0]);
    NFX_PREFETCH_WRITE(&buffer[8]);
    NFX_PREFETCH_READ_NTA(&buffer[4]);

    // Verify no side effects on data
    REQUIRE(buffer[0] == 0);
    REQUIRE(buffer[8] == 0);
}

TEST_CASE("NFX_ASSUME_ALIGNED compiles on aligned buffer",
          "[branch_hints][regression]") {
    alignas(64) char buf[128] = {};
    const void* aligned = NFX_ASSUME_ALIGNED(buf, 64);
    REQUIRE(aligned == static_cast<const void*>(buf));
}

TEST_CASE("NFX_CHECK_NULL, NFX_CHECK_ERROR, NFX_CHECK_SUCCESS macros",
          "[branch_hints][regression]") {
    SECTION("NFX_CHECK_NULL on nullptr enters branch") {
        int* p = nullptr;
        bool entered = false;
        NFX_CHECK_NULL(p) { entered = true; }
        REQUIRE(entered);
    }

    SECTION("NFX_CHECK_NULL on valid pointer skips branch") {
        int val = 42;
        int* p = &val;
        bool entered = false;
        NFX_CHECK_NULL(p) { entered = true; }
        REQUIRE_FALSE(entered);
    }

    SECTION("NFX_CHECK_ERROR on true enters branch") {
        bool entered = false;
        NFX_CHECK_ERROR(true) { entered = true; }
        REQUIRE(entered);
    }

    SECTION("NFX_CHECK_SUCCESS on true enters branch") {
        bool entered = false;
        NFX_CHECK_SUCCESS(true) { entered = true; }
        REQUIRE(entered);
    }
}

TEST_CASE("NFX_ASSERT compiles and passes on true condition",
          "[branch_hints][regression]") {
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4127)  // conditional expression is constant
#endif
    NFX_ASSERT(true);
    NFX_ASSERT(1 + 1 == 2);
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

    int x = 42;
    NFX_ASSERT(x == 42);
    NFX_ASSERT(x > 0);
}

TEST_CASE("NFX_LOOP_VECTORIZE, NFX_LOOP_INDEPENDENT, NFX_LOOP_UNROLL compile",
          "[branch_hints][regression]") {
    alignas(64) int data[64] = {};
    for (int i = 0; i < 64; ++i) data[i] = i;

    SECTION("NFX_LOOP_VECTORIZE on summation loop") {
        int sum = 0;
        NFX_LOOP_VECTORIZE
        for (int i = 0; i < 64; ++i) {
            sum += data[i];
        }
        REQUIRE(sum == 63 * 64 / 2);
    }

    SECTION("NFX_LOOP_INDEPENDENT on independent iterations") {
        int out[64] = {};
        NFX_LOOP_INDEPENDENT
        for (int i = 0; i < 64; ++i) {
            out[i] = data[i] * 2;
        }
        REQUIRE(out[0] == 0);
        REQUIRE(out[63] == 126);
    }

    SECTION("NFX_LOOP_UNROLL on small loop") {
        int sum = 0;
        NFX_LOOP_UNROLL(4)
        for (int i = 0; i < 16; ++i) {
            sum += data[i];
        }
        REQUIRE(sum == 15 * 16 / 2);
    }
}

TEST_CASE("NFX_RESTRICT compiles on pointer parameter",
          "[branch_hints][regression]") {
    struct Local {
        static void copy(int* NFX_RESTRICT dst, const int* NFX_RESTRICT src, int n) {
            for (int i = 0; i < n; ++i) {
                dst[i] = src[i];
            }
        }
    };

    int src[4] = {10, 20, 30, 40};
    int dst[4] = {};
    Local::copy(dst, src, 4);
    REQUIRE(dst[0] == 10);
    REQUIRE(dst[3] == 40);
}

// ============================================================================
// compiler.hpp - NFX_COMPILER_APPLE_CLANG (TICKET_479 Phase 7C)
// ============================================================================

TEST_CASE("NFX_COMPILER_APPLE_CLANG is defined", "[compiler][regression]") {
    // On Linux with GCC/Clang (non-Apple), must be 0
    // On Apple Clang, must be 1
    // Either way, macro must be defined and evaluate to 0 or 1
    [[maybe_unused]] constexpr int val = NFX_COMPILER_APPLE_CLANG;
    REQUIRE((val == 0 || val == 1));
#if !defined(__apple_build_version__)
    REQUIRE(NFX_COMPILER_APPLE_CLANG == 0);
#endif
}
