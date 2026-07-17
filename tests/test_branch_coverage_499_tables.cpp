// test_branch_coverage_499_tables.cpp
//
// TICKET_499 WI3 (batch 11): FieldTable overflow / duplicate / out-of-range
// arms, the tag repeating-group classifiers, and the remaining checksum::validate
// arms. All pure logic in already-instantiated headers, so they raise the
// covered count without inflating the branch denominator.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/types/tag.hpp"

using namespace nfx;

namespace {
std::span<const char> sp(std::string_view v) {
    return std::span<const char>{v.data(), v.size()};
}
}  // namespace

TEST_CASE("FieldTable overflow and out-of-range arms", "[parser][field_view][branch499][regression]") {
    // Small MaxTag so a large tag lands in the overflow area.
    FieldTable<8, 4> table;

    // In-range tag: stored in the flat array.
    REQUIRE(table.set(3, sp("D")).code == ParseErrorCode::None);
    REQUIRE(table.get(3).as_char() == 'D');
    REQUIRE(table.has(3));

    // Tag >= MaxTag: stored in overflow, retrieved by linear scan.
    REQUIRE(table.set(100, sp("OVF")).code == ParseErrorCode::None);
    REQUIRE(table.get(100).as_string() == "OVF");
    REQUIRE(table.has(100));

    // Missing overflow tag: scan completes, returns invalid.
    REQUIRE_FALSE(table.has(101));
    REQUIRE_FALSE(table.get(101).is_valid());

    // tag <= 0 is not a real field, treated as a no-op success.
    REQUIRE(table.set(0, sp("x")).code == ParseErrorCode::None);
    REQUIRE(table.set(-5, sp("x")).code == ParseErrorCode::None);
}

TEST_CASE("FieldTable overflow exhaustion", "[parser][field_view][branch499][regression]") {
    FieldTable<8, 2> table;  // only 2 overflow slots
    REQUIRE(table.set(100, sp("a")).code == ParseErrorCode::None);
    REQUIRE(table.set(101, sp("b")).code == ParseErrorCode::None);
    // Third overflow tag: no slot left -> OverflowExhausted.
    REQUIRE(table.set(102, sp("c")).code == ParseErrorCode::OverflowExhausted);
}

TEST_CASE("FieldTable strict-mode duplicate detection", "[parser][field_view][branch499][regression]") {
    FieldTable<8, 4, /*StrictMode=*/true> table;
    REQUIRE(table.set(3, sp("first")).code == ParseErrorCode::None);
    // Duplicate in the flat array.
    REQUIRE(table.set(3, sp("again")).code == ParseErrorCode::DuplicateTag);

    REQUIRE(table.set(100, sp("ovf")).code == ParseErrorCode::None);
    // Duplicate in the overflow area.
    REQUIRE(table.set(100, sp("ovf2")).code == ParseErrorCode::DuplicateTag);

    // allow_dup bypasses the check (repeating-group semantics).
    REQUIRE(table.set(3, sp("dup-ok"), /*allow_dup=*/true).code == ParseErrorCode::None);
}

TEST_CASE("tag repeating-group classifiers", "[types][tag][branch499][regression]") {
    using namespace nfx::tag;
    REQUIRE(is_group_count_tag(146));   // NoRelatedSym
    REQUIRE(is_group_count_tag(267));   // NoMDEntryTypes
    REQUIRE_FALSE(is_group_count_tag(35));

    // Per-group membership: each switch case plus the default.
    REQUIRE(is_repeating_group_member_tag(146, 55));    // Symbol under NoRelatedSym
    REQUIRE_FALSE(is_repeating_group_member_tag(146, 99));
    REQUIRE(is_repeating_group_member_tag(267, 269));   // MDEntryType
    REQUIRE(is_repeating_group_member_tag(268, 270));   // MDEntryPx under NoMDEntries
    REQUIRE_FALSE(is_repeating_group_member_tag(999, 55));  // default arm
}

TEST_CASE("checksum::validate SOH-search and trailer arms", "[messages][trailer][branch499][regression]") {
    SECTION("checksum field located via SOH search") {
        std::string body = "8=FIX.4.4\x01" "35=0\x01" "49=A\x01" "56=B\x01";
        uint8_t sum = checksum::calculate(std::span<const char>{body.data(), body.size()});
        auto fmt = checksum::format(sum);
        std::string msg = body + "10=" + std::string(fmt.data(), 3) + "\x01";
        // A valid, correctly-checksummed message exercises the SOH-before-10=
        // handling and the digit-parse loop to completion.
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code == ParseErrorCode::None);
    }
    SECTION("checksum value position runs off the end") {
        // "10=" appears but there aren't 3 digits after it before end.
        std::string msg = "8=FIX\x01" "0000\x01" "10=1\x01";
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code != ParseErrorCode::None);
    }
}
