// test_branch_coverage_499_parser.cpp
//
// TICKET_499 WI3 (batch 4): FieldView accessor branches and a couple of small
// SBE/parse helpers. FieldView is the zero-copy field accessor sitting on the
// untrusted-parse path; every as_* method has empty / malformed / overflow arms
// that the happy-path parser tests never exercise.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/sbe/types/composite_types.hpp"

using namespace nfx;

namespace {
FieldView fv(int tag, std::string_view v) {
    return FieldView{tag, std::span<const char>{v.data(), v.size()}};
}
}  // namespace

TEST_CASE("FieldView as_char / as_bool branches", "[parser][field_view][branch499][regression]") {
    REQUIRE(fv(1, "").as_char() == '\0');       // empty arm
    REQUIRE(fv(1, "D").as_char() == 'D');        // non-empty arm
    REQUIRE(fv(1, "Y").as_bool());               // Y true
    REQUIRE_FALSE(fv(1, "N").as_bool());         // non-Y false
    REQUIRE_FALSE(fv(1, "").as_bool());          // empty false arm
}

TEST_CASE("FieldView as_int branches", "[parser][field_view][branch499][regression]") {
    REQUIRE_FALSE(fv(1, "").as_int().has_value());         // empty
    REQUIRE(fv(1, "42").as_int() == 42);                    // plain
    REQUIRE(fv(1, "-42").as_int() == -42);                  // negative arm
    REQUIRE_FALSE(fv(1, "4x2").as_int().has_value());      // non-digit
    // overflow: 19 nines exceeds int64 headroom guard
    REQUIRE_FALSE(fv(1, "99999999999999999999").as_int().has_value());
}

TEST_CASE("FieldView as_uint branches", "[parser][field_view][branch499][regression]") {
    REQUIRE_FALSE(fv(1, "").as_uint().has_value());        // empty
    REQUIRE(fv(1, "100").as_uint() == 100u);                // plain
    REQUIRE_FALSE(fv(1, "1a").as_uint().has_value());      // non-digit
    // overflow: 20 nines exceeds uint64 headroom guard
    REQUIRE_FALSE(fv(1, "99999999999999999999").as_uint().has_value());
}

TEST_CASE("FieldView as_side branches", "[parser][field_view][branch499][regression]") {
    REQUIRE_FALSE(fv(54, "").as_side().has_value());       // empty
    REQUIRE(fv(54, "1").as_side() == Side::Buy);            // in-range digit
    REQUIRE_FALSE(fv(54, "A").as_side().has_value());      // out-of-range char
}

TEST_CASE("FieldView enum accessors empty vs present", "[parser][field_view][branch499][regression]") {
    // Each of these has an empty -> nullopt arm plus a present -> value arm.
    REQUIRE_FALSE(fv(40, "").as_ord_type().has_value());
    REQUIRE(fv(40, "2").as_ord_type().has_value());        // Limit

    REQUIRE_FALSE(fv(39, "").as_ord_status().has_value());
    REQUIRE(fv(39, "0").as_ord_status().has_value());      // New

    REQUIRE_FALSE(fv(150, "").as_exec_type().has_value());
    REQUIRE(fv(150, "0").as_exec_type().has_value());

    REQUIRE_FALSE(fv(59, "").as_time_in_force().has_value());
    REQUIRE(fv(59, "1").as_time_in_force().has_value());   // GoodTillCancel
}

TEST_CASE("FieldView price/qty and validity helpers", "[parser][field_view][branch499][regression]") {
    REQUIRE(fv(44, "150.5").as_price().to_double() == 150.5);
    REQUIRE(fv(38, "100").as_qty().whole() == 100);

    REQUIRE(fv(35, "D").is_valid());        // tag > 0
    REQUIRE_FALSE(fv(0, "D").is_valid());   // tag 0 arm
    REQUIRE(fv(35, "").is_empty());
    REQUIRE_FALSE(fv(35, "D").is_empty());
    REQUIRE(fv(35, "ABC").size() == 3);
}

TEST_CASE("SBE FixedString::is_null both arms", "[sbe][sbe_types][branch499][regression]") {
    using nfx::sbe::FixedString8;
    // All padding-space buffer -> null (loop completes, returns true).
    char pad[8];
    for (char& c : pad) { c = ' '; }
    REQUIRE(FixedString8::is_null(pad));

    // A non-padding, non-null byte -> early return false.
    char data[8] = {'A', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    REQUIRE_FALSE(FixedString8::is_null(data));
}
