#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/parser/timestamp_parser.hpp"
#include "nexusfix/parser/field_view.hpp"

using namespace nfx;

// ============================================================================
// Test Data
// ============================================================================

namespace {

constexpr std::string_view VALID_TS = "20240115-10:30:00.123";
constexpr std::string_view MIDNIGHT_TS = "20240101-00:00:00.000";
constexpr std::string_view MAX_TIME_TS = "20241231-23:59:59.999";
constexpr std::string_view YEAR_BOUNDARY_TS = "19700101-00:00:00.000";

}  // namespace

// ============================================================================
// Scalar Parser Tests
// ============================================================================

TEST_CASE("Scalar timestamp parsing", "[timestamp][scalar]") {
    SECTION("Valid timestamp") {
        auto result = parse_timestamp_scalar(VALID_TS);
        REQUIRE(result.has_value());

        auto& ts = *result;
        CHECK(ts.year == 2024);
        CHECK(ts.month == 1);
        CHECK(ts.day == 15);
        CHECK(ts.hour == 10);
        CHECK(ts.minute == 30);
        CHECK(ts.second == 0);
        CHECK(ts.millis == 123);
    }

    SECTION("Midnight") {
        auto result = parse_timestamp_scalar(MIDNIGHT_TS);
        REQUIRE(result.has_value());

        auto& ts = *result;
        CHECK(ts.year == 2024);
        CHECK(ts.month == 1);
        CHECK(ts.day == 1);
        CHECK(ts.hour == 0);
        CHECK(ts.minute == 0);
        CHECK(ts.second == 0);
        CHECK(ts.millis == 0);
    }

    SECTION("Max time values") {
        auto result = parse_timestamp_scalar(MAX_TIME_TS);
        REQUIRE(result.has_value());

        auto& ts = *result;
        CHECK(ts.year == 2024);
        CHECK(ts.month == 12);
        CHECK(ts.day == 31);
        CHECK(ts.hour == 23);
        CHECK(ts.minute == 59);
        CHECK(ts.second == 59);
        CHECK(ts.millis == 999);
    }

    SECTION("Unix epoch") {
        auto result = parse_timestamp_scalar(YEAR_BOUNDARY_TS);
        REQUIRE(result.has_value());
        CHECK(result->year == 1970);
        CHECK(result->month == 1);
        CHECK(result->day == 1);
    }

    SECTION("Empty input") {
        auto result = parse_timestamp_scalar("");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Too short") {
        auto result = parse_timestamp_scalar("20240115-10:30:00.12");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Wrong separator - dash") {
        auto result = parse_timestamp_scalar("20240115X10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Wrong separator - first colon") {
        auto result = parse_timestamp_scalar("20240115-10-30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Wrong separator - second colon") {
        auto result = parse_timestamp_scalar("20240115-10:30-00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Wrong separator - dot") {
        auto result = parse_timestamp_scalar("20240115-10:30:00:123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Non-digit in year") {
        auto result = parse_timestamp_scalar("202X0115-10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Non-digit in millis") {
        auto result = parse_timestamp_scalar("20240115-10:30:00.1X3");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid month 13") {
        auto result = parse_timestamp_scalar("20241301-10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid month 00") {
        auto result = parse_timestamp_scalar("20240001-10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid day 00") {
        auto result = parse_timestamp_scalar("20240100-10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid day 32") {
        auto result = parse_timestamp_scalar("20240132-10:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid hour 24") {
        auto result = parse_timestamp_scalar("20240115-24:30:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid minute 60") {
        auto result = parse_timestamp_scalar("20240115-10:60:00.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Invalid second 60") {
        auto result = parse_timestamp_scalar("20240115-10:30:60.123");
        CHECK_FALSE(result.has_value());
    }

    SECTION("Timestamp with extra data after is OK") {
        // In FIX messages, the timestamp field has data after it
        std::string ts_with_trailing = "20240115-10:30:00.123\x01" "37=ORDER123";
        auto result = parse_timestamp_scalar(ts_with_trailing);
        REQUIRE(result.has_value());
        CHECK(result->year == 2024);
        CHECK(result->millis == 123);
    }
}

// ============================================================================
// SIMD Parser Tests (if available)
// ============================================================================

#if NFX_SIMD_AVAILABLE

TEST_CASE("SIMD timestamp parsing", "[timestamp][simd]") {
    SECTION("Valid timestamp") {
        // Need >= 32 bytes for safe AVX2 load. Pad with FIX-like data.
        std::string padded = std::string(VALID_TS) + "\x01" "37=ORDER12345678";
        auto result = parse_timestamp_simd(padded);
        REQUIRE(result.has_value());

        auto& ts = *result;
        CHECK(ts.year == 2024);
        CHECK(ts.month == 1);
        CHECK(ts.day == 15);
        CHECK(ts.hour == 10);
        CHECK(ts.minute == 30);
        CHECK(ts.second == 0);
        CHECK(ts.millis == 123);
    }

    SECTION("Scalar and SIMD agree on valid timestamps") {
        const std::string_view timestamps[] = {
            "20240115-10:30:00.123",
            "20241231-23:59:59.999",
            "20240101-00:00:00.000",
            "19700101-00:00:00.000",
            "20260331-14:22:07.456",
        };

        for (auto ts_str : timestamps) {
            // Pad for safe SIMD load
            std::string padded = std::string(ts_str) + "\x01" "99=PADDING_DATA__";
            auto scalar = parse_timestamp_scalar(padded);
            auto simd = parse_timestamp_simd(padded);

            REQUIRE(scalar.has_value());
            REQUIRE(simd.has_value());
            CHECK(scalar->year == simd->year);
            CHECK(scalar->month == simd->month);
            CHECK(scalar->day == simd->day);
            CHECK(scalar->hour == simd->hour);
            CHECK(scalar->minute == simd->minute);
            CHECK(scalar->second == simd->second);
            CHECK(scalar->millis == simd->millis);
        }
    }

    SECTION("SIMD rejects invalid timestamps") {
        // Pad all for safe SIMD load
        std::string bad_sep = "20240115X10:30:00.123\x01" "99=PADDING_DATA__";
        CHECK_FALSE(parse_timestamp_simd(bad_sep).has_value());

        std::string bad_digit = "202X0115-10:30:00.123\x01" "99=PADDING_DATA__";
        CHECK_FALSE(parse_timestamp_simd(bad_digit).has_value());

        std::string bad_month = "20241301-10:30:00.123\x01" "99=PADDING_DATA__";
        CHECK_FALSE(parse_timestamp_simd(bad_month).has_value());
    }

    SECTION("Too short input") {
        auto result = parse_timestamp_simd("20240115-10:30:00.12");
        CHECK_FALSE(result.has_value());
    }
}

#endif  // NFX_SIMD_AVAILABLE

// ============================================================================
// Dispatch Function Tests
// ============================================================================

TEST_CASE("Timestamp dispatch function", "[timestamp]") {
    // Pad for safe SIMD load in case SIMD is active
    std::string padded = std::string(VALID_TS) + "\x01" "99=PADDING_DATA__";
    auto result = parse_timestamp(padded);
    REQUIRE(result.has_value());

    CHECK(result->year == 2024);
    CHECK(result->month == 1);
    CHECK(result->day == 15);
    CHECK(result->hour == 10);
    CHECK(result->minute == 30);
    CHECK(result->second == 0);
    CHECK(result->millis == 123);
}

// ============================================================================
// Epoch Conversion Tests
// ============================================================================

TEST_CASE("Timestamp to epoch conversion", "[timestamp][epoch]") {
    SECTION("Unix epoch") {
        ParsedTimestamp ts{1970, 1, 1, 0, 0, 0, 0};
        CHECK(to_epoch_ms(ts) == 0);
    }

    SECTION("One second after epoch") {
        ParsedTimestamp ts{1970, 1, 1, 0, 0, 1, 0};
        CHECK(to_epoch_ms(ts) == 1000);
    }

    SECTION("One day after epoch") {
        ParsedTimestamp ts{1970, 1, 2, 0, 0, 0, 0};
        CHECK(to_epoch_ms(ts) == 86400000);
    }

    SECTION("Known date: 2024-01-15 10:30:00.123") {
        ParsedTimestamp ts{2024, 1, 15, 10, 30, 0, 123};
        // 2024-01-15T10:30:00.123Z = 1705313400123 ms
        CHECK(to_epoch_ms(ts) == 1705314600123ULL);
    }

    SECTION("Y2K: 2000-01-01 00:00:00.000") {
        ParsedTimestamp ts{2000, 1, 1, 0, 0, 0, 0};
        // 2000-01-01T00:00:00Z = 946684800000 ms
        CHECK(to_epoch_ms(ts) == 946684800000ULL);
    }

    SECTION("Roundtrip: parse then convert") {
        std::string padded = std::string(VALID_TS) + "\x01" "99=PADDING_DATA__";
        auto result = parse_timestamp(padded);
        REQUIRE(result.has_value());
        CHECK(to_epoch_ms(*result) == 1705314600123ULL);
    }
}

// ============================================================================
// FieldView Integration Tests
// ============================================================================

TEST_CASE("FieldView as_timestamp", "[timestamp][field_view]") {
    // Simulate a field value containing a FIX timestamp with trailing data for safe SIMD
    std::string ts_data = std::string(VALID_TS);
    // In real FIX, the value span points into a larger buffer, so SIMD reads beyond are safe
    // For testing, pad the string
    std::string padded = ts_data + std::string(16, '\0');
    FieldView field{52, std::span<const char>{padded.data(), FIX_TIMESTAMP_LEN}};

    auto result = field.as_timestamp();
    REQUIRE(result.has_value());
    CHECK(result->year == 2024);
    CHECK(result->hour == 10);
    CHECK(result->millis == 123);
}

// ============================================================================
// Real FIX Message Timestamps
// ============================================================================

TEST_CASE("Real FIX message timestamp fields", "[timestamp]") {
    // Typical SendingTime values seen in production
    const std::string_view real_timestamps[] = {
        "20260331-09:30:00.001",
        "20260331-16:00:00.000",
        "20260102-08:00:00.500",
        "20251215-23:59:59.999",
    };

    for (auto ts_str : real_timestamps) {
        std::string padded = std::string(ts_str) + "\x01" "99=PADDING_DATA__";
        auto result = parse_timestamp(padded);
        REQUIRE(result.has_value());
        // All should convert to a positive epoch
        CHECK(to_epoch_ms(*result) > 0);
    }
}
