// test_branch_coverage_499_ts.cpp
//
// TICKET_499 WI3 (batch 13): timestamp range-validation arms and the fixed-point
// comparison operators. parse_timestamp runs detail::validate_ranges, which has
// a distinct bound check per component (month/day/hour/minute/second/millis);
// the happy-path timestamp tests only feed valid values.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/parser/timestamp_parser.hpp"
#include "nexusfix/types/field_types.hpp"

using namespace nfx;

TEST_CASE("parse_timestamp range-validation arms", "[parser][timestamp][branch499][regression]") {
    // Valid baseline parses.
    REQUIRE(parse_timestamp("20260717-10:30:00.123").has_value());

    // Each out-of-range component trips its own arm in validate_ranges.
    REQUIRE_FALSE(parse_timestamp("20261317-10:30:00.123").has_value());  // month 13
    REQUIRE_FALSE(parse_timestamp("20260732-10:30:00.123").has_value());  // day 32
    REQUIRE_FALSE(parse_timestamp("20260717-24:30:00.123").has_value());  // hour 24
    REQUIRE_FALSE(parse_timestamp("20260717-10:60:00.123").has_value());  // minute 60
    REQUIRE_FALSE(parse_timestamp("20260717-10:30:60.123").has_value());  // second 60
    // month 0 / day 0 lower-bound arms
    REQUIRE_FALSE(parse_timestamp("20260017-10:30:00.123").has_value());  // month 0
    REQUIRE_FALSE(parse_timestamp("20260700-10:30:00.123").has_value());  // day 0
}

TEST_CASE("parse_timestamp too-short input", "[parser][timestamp][branch499][regression]") {
    REQUIRE_FALSE(parse_timestamp("2026").has_value());
    REQUIRE_FALSE(parse_timestamp("").has_value());
}

TEST_CASE("FixedPrice comparison operators", "[types][field_types][branch499][regression]") {
    auto a = FixedPrice::from_double(100.0);
    auto b = FixedPrice::from_double(200.0);
    auto a2 = FixedPrice::from_double(100.0);

    REQUIRE(a < b);
    REQUIRE(b > a);
    REQUIRE(a <= a2);
    REQUIRE(a >= a2);
    REQUIRE(a == a2);
    REQUIRE(a != b);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("Qty comparison operators", "[types][field_types][branch499][regression]") {
    auto a = Qty::from_int(10);
    auto b = Qty::from_int(20);
    auto a2 = Qty::from_int(10);

    REQUIRE(a < b);
    REQUIRE(b > a);
    REQUIRE(a == a2);
    REQUIRE(a != b);
    REQUIRE(a <= a2);
    REQUIRE(b >= a);
}

TEST_CASE("FixedPrice/Qty from_string precision-clamp edge", "[types][field_types][branch499][regression]") {
    // Exactly DECIMAL_PLACES fractional digits, then one more that is dropped.
    // FixedPrice scale is 10^8 (8 places); the 9th fractional digit hits the
    // fractional_digits < DECIMAL_PLACES false arm.
    auto p = FixedPrice::from_string("1.123456789");
    REQUIRE(p.to_double() > 1.1234567);
    REQUIRE(p.to_double() < 1.1234568);

    // Qty scale is 10^4 (4 places); a 5th fractional digit is dropped.
    auto q = Qty::from_string("2.12345");
    REQUIRE(q.whole() == 2);
}
