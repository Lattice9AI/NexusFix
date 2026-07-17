// test_branch_coverage_499_consteval.cpp
//
// TICKET_499 WI3 (batch 7): the consteval header parser and checksum validator
// error ladders. parse_header and validate_checksum run on untrusted bytes and
// have per-field failure arms (bad BodyLength, bad MsgSeqNum, each missing
// required header field, buffer too short, missing checksum) that the happy-path
// header tests never take.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "nexusfix/parser/consteval_parser.hpp"

using namespace nfx;

namespace {
// Build a FIX message from tag=value pairs, joining with SOH (0x01). No
// checksum unless the caller adds a 10= pair.
std::string fix_msg(std::initializer_list<std::pair<int, std::string_view>> fields) {
    std::string out;
    for (auto& [tag, val] : fields) {
        out += std::to_string(tag);
        out += '=';
        out += val;
        out += '\x01';
    }
    return out;
}

std::span<const char> as_span(const std::string& s) {
    return std::span<const char>{s.data(), s.size()};
}
}  // namespace

TEST_CASE("parse_header buffer-too-short arm", "[parser][consteval][branch499][regression]") {
    std::string tiny = "8=FIX\x01";  // < MIN_MESSAGE_SIZE (20)
    auto r = parse_header(as_span(tiny));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error.code == ParseErrorCode::BufferTooShort);
}

TEST_CASE("parse_header valid header succeeds", "[parser][consteval][branch499][regression]") {
    auto msg = fix_msg({
        {8, "FIX.4.4"}, {9, "50"}, {35, "D"}, {49, "CLIENT"},
        {56, "SERVER"}, {34, "1"}, {52, "20260717-10:00:00"},
        {11, "CLORD1"},  // first non-header field -> body_start
    });
    auto r = parse_header(as_span(msg));
    REQUIRE(r.ok());
    REQUIRE(r.header.begin_string == "FIX.4.4");
    REQUIRE(r.header.msg_type == 'D');
    REQUIRE(r.body_start > 0);
}

TEST_CASE("parse_header field-level error arms", "[parser][consteval][branch499][regression]") {
    SECTION("non-numeric BodyLength -> InvalidBodyLength") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "notanumber"}, {35, "D"},
                            {49, "C"}, {56, "S"}, {34, "1"}, {52, "T"}});
        auto r = parse_header(as_span(msg));
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error.code == ParseErrorCode::InvalidBodyLength);
    }
    SECTION("non-numeric MsgSeqNum -> InvalidFieldFormat") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "50"}, {35, "D"},
                            {49, "C"}, {56, "S"}, {34, "xx"}, {52, "T"}});
        auto r = parse_header(as_span(msg));
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error.code == ParseErrorCode::InvalidFieldFormat);
    }
}

TEST_CASE("parse_header missing required-field arms", "[parser][consteval][branch499][regression]") {
    // Each leaves out exactly one required header field. Pad with a body field
    // so the buffer clears MIN_MESSAGE_SIZE and the loop reaches the checks.
    SECTION("missing BeginString") {
        auto msg = fix_msg({{9, "50"}, {35, "D"}, {49, "C"}, {56, "S"},
                            {34, "1"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::BeginString::value);
    }
    SECTION("missing BodyLength") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {35, "D"}, {49, "C"}, {56, "S"},
                            {34, "1"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::BodyLength::value);
    }
    SECTION("missing MsgType") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "50"}, {49, "C"}, {56, "S"},
                            {34, "1"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::MsgType::value);
    }
    SECTION("missing SenderCompID") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "50"}, {35, "D"}, {56, "S"},
                            {34, "1"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::SenderCompID::value);
    }
    SECTION("missing TargetCompID") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "50"}, {35, "D"}, {49, "C"},
                            {34, "1"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::TargetCompID::value);
    }
    SECTION("missing MsgSeqNum") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "50"}, {35, "D"}, {49, "C"},
                            {56, "S"}, {52, "20260717-10:00:00"}});
        auto r = parse_header(as_span(msg));
        REQUIRE(r.error.tag == tag::MsgSeqNum::value);
    }
}

TEST_CASE("validate_checksum arms", "[parser][consteval][branch499][regression]") {
    SECTION("too short") {
        std::string tiny = "10=00";
        auto err = validate_checksum(as_span(tiny));
        REQUIRE(err.code == ParseErrorCode::BufferTooShort);
    }
    SECTION("no checksum field present") {
        auto msg = fix_msg({{8, "FIX.4.4"}, {9, "5"}, {35, "0"}});
        auto err = validate_checksum(as_span(msg));
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
    }
    SECTION("well-formed checksum field is located") {
        // Body then a 10=NNN trailer; validate_checksum locates and parses it.
        std::string body = fix_msg({{8, "FIX.4.4"}, {9, "5"}, {35, "0"}});
        std::string msg = body + "10=123\x01";
        auto err = validate_checksum(as_span(msg));
        // May be None or InvalidChecksum depending on the digits; the point is
        // the search + parse arms run rather than MissingRequiredField.
        REQUIRE(err.code != ParseErrorCode::MissingRequiredField);
        REQUIRE(err.code != ParseErrorCode::BufferTooShort);
    }
}

TEST_CASE("extract_fields error arm on malformed input", "[parser][consteval][branch499][regression]") {
    // A field with a non-numeric tag makes FieldIterator::next return invalid,
    // so extract_fields records the error and stops.
    std::string bad = "8=FIX.4.4\x01" "xx=oops\x01";
    auto result = extract_fields<16>(as_span(bad));
    REQUIRE_FALSE(result.ok());
}
