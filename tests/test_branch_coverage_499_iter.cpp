// test_branch_coverage_499_iter.cpp
//
// TICKET_499 WI3 (batch 12): FieldIterator and runtime-parser malformed-input
// error arms. These sit directly on the untrusted-byte path: a tokenizer that
// hits a non-digit tag character, an overflowing tag number, a missing '=', or
// an unterminated field must reject cleanly. The happy-path parse tests never
// take these arms.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/messages/common/trailer.hpp"

using namespace nfx;

namespace {
std::span<const char> sp(std::string_view v) {
    return std::span<const char>{v.data(), v.size()};
}
}  // namespace

TEST_CASE("FieldIterator rejects malformed tags", "[parser][field_view][branch499][regression]") {
    SECTION("non-digit character in tag") {
        // 'x' where a tag digit is expected.
        FieldIterator it{sp("3x=D\x01")};
        auto f = it.next();
        REQUIRE_FALSE(f.is_valid());
        REQUIRE(it.last_error() == ParseErrorCode::InvalidTagNumber);
    }
    SECTION("tag number overflows int") {
        FieldIterator it{sp("99999999999=D\x01")};
        auto f = it.next();
        REQUIRE_FALSE(f.is_valid());
        REQUIRE(it.last_error() == ParseErrorCode::InvalidTagNumber);
    }
    SECTION("field without '=' is rejected") {
        // Tag digits then SOH with no '=': either InvalidFieldFormat (no '='
        // before SOH) or UnterminatedField, depending on where the scan stops.
        FieldIterator it{sp("35\x01")};
        auto f = it.next();
        REQUIRE_FALSE(f.is_valid());
        REQUIRE(it.last_error() != ParseErrorCode::None);
    }
    SECTION("well-formed field parses and has_next advances") {
        FieldIterator it{sp("35=D\x01" "49=SENDER\x01")};
        auto f1 = it.next();
        REQUIRE(f1.is_valid());
        REQUIRE(f1.tag == 35);
        REQUIRE(f1.as_char() == 'D');
        auto f2 = it.next();
        REQUIRE(f2.tag == 49);
        REQUIRE(f2.as_string() == "SENDER");
    }
}

TEST_CASE("IndexedParser rejects malformed fields", "[parser][runtime][branch499][regression]") {
    SECTION("non-digit tag character") {
        auto r = IndexedParser::parse(sp("8=FIX.4.4\x01" "3x=1\x01"));
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("empty tag (leading '=')") {
        auto r = IndexedParser::parse(sp("8=FIX.4.4\x01" "=value\x01"));
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("tag number overflow") {
        auto r = IndexedParser::parse(sp("8=FIX.4.4\x01" "99999999999=1\x01"));
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("well-formed message parses") {
        // Build with a valid BodyLength + CheckSum via MessageAssembler.
        MessageAssembler asm_;
        auto built = asm_.start(fix::FIX_4_4)
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "A")
            .field(tag::TargetCompID::value, "B")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260717-10:00:00")
            .finish();
        auto r = IndexedParser::parse(built);
        REQUIRE(r.has_value());
        REQUIRE(r->msg_type() == '0');
    }
}
