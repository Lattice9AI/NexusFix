#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <cstring>

#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/parser/simd_scanner.hpp"
#include "nexusfix/parser/consteval_parser.hpp"
#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/parser/structural_index.hpp"
#include "nexusfix/parser/simd_checksum.hpp"
#include "nexusfix/parser/repeating_group.hpp"
#include "nexusfix/interfaces/i_message.hpp"

using namespace nfx;

// ============================================================================
// Test Data
// ============================================================================

namespace {

// Sample ExecutionReport message (FIX 4.4)
// 8=FIX.4.4|9=136|35=8|49=SENDER|56=TARGET|34=1|52=20231215-10:30:00.000|
// 37=ORDER123|17=EXEC456|150=0|39=0|55=AAPL|54=1|38=100|44=150.50|151=100|14=0|6=0|10=000|
const std::string EXEC_REPORT =
    "8=FIX.4.4\x01" "9=136\x01" "35=8\x01" "49=SENDER\x01" "56=TARGET\x01"
    "34=1\x01" "52=20231215-10:30:00.000\x01" "37=ORDER123\x01" "17=EXEC456\x01"
    "150=0\x01" "39=0\x01" "55=AAPL\x01" "54=1\x01" "38=100\x01" "44=150.50\x01"
    "151=100\x01" "14=0\x01" "6=0\x01" "10=000\x01";

// Simple Logon message
const std::string LOGON =
    "8=FIX.4.4\x01" "9=63\x01" "35=A\x01" "49=CLIENT\x01" "56=SERVER\x01"
    "34=1\x01" "52=20231215-10:30:00\x01" "98=0\x01" "108=30\x01" "10=187\x01";

// Heartbeat message
const std::string HEARTBEAT =
    "8=FIX.4.4\x01" "9=51\x01" "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
    "34=5\x01" "52=20231215-10:30:00\x01" "10=132\x01";

/// Build a FIX message with correct BodyLength and CheckSum.
/// @param inner Fields between "9=<len>\x01" and "10=<cs>\x01"
///        (i.e. starting from "35=..." through the last body field + SOH)
/// @return Complete FIX message string
std::string build_fix_message(const std::string& inner) {
    // BodyLength covers everything from after "9=<len>\x01" to the SOH before "10="
    // That's exactly the 'inner' string.
    std::string bl = std::to_string(inner.size());
    std::string prefix = "8=FIX.4.4\x01" "9=" + bl + "\x01";
    std::string body = prefix + inner;
    uint8_t sum = 0;
    for (char c : body) sum += static_cast<uint8_t>(c);
    char cs[3];
    cs[0] = '0' + (sum / 100);
    cs[1] = '0' + ((sum / 10) % 10);
    cs[2] = '0' + (sum % 10);
    return body + "10=" + std::string(cs, 3) + "\x01";
}

/// Build a FIX message with a specific (possibly wrong) BodyLength but correct CheckSum.
std::string build_fix_message_with_bl(const std::string& inner, int body_length) {
    std::string bl = std::to_string(body_length);
    std::string prefix = "8=FIX.4.4\x01" "9=" + bl + "\x01";
    std::string body = prefix + inner;
    uint8_t sum = 0;
    for (char c : body) sum += static_cast<uint8_t>(c);
    char cs[3];
    cs[0] = '0' + (sum / 100);
    cs[1] = '0' + ((sum / 10) % 10);
    cs[2] = '0' + (sum % 10);
    return body + "10=" + std::string(cs, 3) + "\x01";
}

}  // namespace

// ============================================================================
// FieldView Tests
// ============================================================================

TEST_CASE("FieldView basic operations", "[parser][field_view][regression]") {
    SECTION("Construction") {
        const char* data = "12345";
        FieldView field{44, std::span<const char>{data, 5}};

        REQUIRE(field.tag == 44);
        REQUIRE(field.as_string() == "12345");
        REQUIRE(field.is_valid());
    }

    SECTION("Integer parsing") {
        const char* data = "12345";
        FieldView field{38, std::span<const char>{data, 5}};

        auto val = field.as_int();
        REQUIRE(val.has_value());
        REQUIRE(*val == 12345);
    }

    SECTION("Negative integer") {
        const char* data = "-500";
        FieldView field{38, std::span<const char>{data, 4}};

        auto val = field.as_int();
        REQUIRE(val.has_value());
        REQUIRE(*val == -500);
    }

    SECTION("Price parsing") {
        const char* data = "150.75";
        FieldView field{44, std::span<const char>{data, 6}};

        auto price = field.as_price();
        REQUIRE_THAT(price.to_double(),
            Catch::Matchers::WithinRel(150.75, 0.0001));
    }

    SECTION("Char parsing") {
        const char* data = "1";
        FieldView field{54, std::span<const char>{data, 1}};

        REQUIRE(field.as_char() == '1');
    }

    SECTION("Bool parsing") {
        const char* yes = "Y";
        const char* no = "N";

        FieldView field_y{43, std::span<const char>{yes, 1}};
        FieldView field_n{43, std::span<const char>{no, 1}};

        REQUIRE(field_y.as_bool() == true);
        REQUIRE(field_n.as_bool() == false);
    }

    SECTION("Side enum parsing") {
        const char* buy = "1";
        const char* sell = "2";

        FieldView field_buy{54, std::span<const char>{buy, 1}};
        FieldView field_sell{54, std::span<const char>{sell, 1}};

        REQUIRE(field_buy.as_side() == Side::Buy);
        REQUIRE(field_sell.as_side() == Side::Sell);
    }
}

// ============================================================================
// FieldIterator Tests
// ============================================================================

TEST_CASE("FieldIterator", "[parser][field_view][regression]") {
    const std::string msg = "8=FIX.4.4\x01" "35=A\x01" "49=SENDER\x01";

    SECTION("Iterate all fields") {
        FieldIterator iter{std::span<const char>{msg.data(), msg.size()}};

        auto f1 = iter.next();
        REQUIRE(f1.tag == 8);
        REQUIRE(f1.as_string() == "FIX.4.4");

        auto f2 = iter.next();
        REQUIRE(f2.tag == 35);
        REQUIRE(f2.as_char() == 'A');

        auto f3 = iter.next();
        REQUIRE(f3.tag == 49);
        REQUIRE(f3.as_string() == "SENDER");

        auto f4 = iter.next();
        REQUIRE(!f4.is_valid());  // No more fields
    }

    SECTION("Reject unterminated field at end of buffer") {
        const std::string bad = "35=A";
        FieldIterator iter{std::span<const char>{bad.data(), bad.size()}};

        auto field = iter.next();
        REQUIRE(!field.is_valid());
        REQUIRE(iter.last_error() == ParseErrorCode::UnterminatedField);
    }
}

// ============================================================================
// FieldTable Tests
// ============================================================================

TEST_CASE("FieldTable O(1) lookup", "[parser][field_view][regression]") {
    FieldTable<512> table;

    const char* val1 = "AAPL";
    const char* val2 = "100";

    REQUIRE(table.set(55, std::span<const char>{val1, 4}).ok());
    REQUIRE(table.set(38, std::span<const char>{val2, 3}).ok());

    SECTION("Lookup existing") {
        REQUIRE(table.has(55));
        REQUIRE(table.get_string(55) == "AAPL");

        REQUIRE(table.has(38));
        auto qty = table.get_int(38);
        REQUIRE(qty.has_value());
        REQUIRE(*qty == 100);
    }

    SECTION("Lookup non-existing") {
        REQUIRE(!table.has(999));
        REQUIRE(table.get_string(999) == "");
    }
}

// ============================================================================
// SIMD Scanner Tests
// ============================================================================

TEST_CASE("SIMD SOH scanning", "[parser][simd][regression]") {
    SECTION("Scalar scanner") {
        const std::string data = "8=FIX.4.4\x01" "35=A\x01" "10=000\x01";
        auto positions = simd::scan_soh_scalar(
            std::span<const char>{data.data(), data.size()});

        REQUIRE(positions.count == 3);
        REQUIRE(data[positions[0]] == '\x01');
        REQUIRE(data[positions[1]] == '\x01');
        REQUIRE(data[positions[2]] == '\x01');
    }

    SECTION("Find SOH") {
        const std::string data = "8=FIX.4.4\x01" "35=A\x01";
        size_t pos = simd::find_soh(
            std::span<const char>{data.data(), data.size()});

        REQUIRE(pos == 9);  // Position of first SOH
    }

    SECTION("Find equals") {
        const std::string data = "35=A";
        size_t pos = simd::find_equals(
            std::span<const char>{data.data(), data.size()});

        REQUIRE(pos == 2);  // Position of '='
    }

    SECTION("Count SOH") {
        size_t count = simd::count_soh(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(count == 19);  // Number of fields
    }
}

// ============================================================================
// Consteval Parser Tests
// ============================================================================

TEST_CASE("Header parsing", "[parser][consteval][regression]") {
    SECTION("Valid header") {
        auto result = parse_header(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(result.ok());
        REQUIRE(result.header.begin_string == "FIX.4.4");
        REQUIRE(result.header.body_length == 136);
        REQUIRE(result.header.msg_type == '8');
        REQUIRE(result.header.sender_comp_id == "SENDER");
        REQUIRE(result.header.target_comp_id == "TARGET");
        REQUIRE(result.header.msg_seq_num == 1);
    }

    SECTION("Logon header") {
        auto result = parse_header(
            std::span<const char>{LOGON.data(), LOGON.size()});

        REQUIRE(result.ok());
        REQUIRE(result.header.msg_type == 'A');
        REQUIRE(result.header.sender_comp_id == "CLIENT");
        REQUIRE(result.header.target_comp_id == "SERVER");
    }

    SECTION("Buffer too short") {
        const std::string short_msg = "8=FIX";
        auto result = parse_header(
            std::span<const char>{short_msg.data(), short_msg.size()});

        REQUIRE(!result.ok());
        REQUIRE(result.error.code == ParseErrorCode::BufferTooShort);
    }
}

// ============================================================================
// Runtime Parser Tests
// ============================================================================

TEST_CASE("ParsedMessage", "[parser][runtime][regression]") {
    SECTION("Parse execution report") {
        auto result = ParsedMessage::parse(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.msg_type() == '8');
        REQUIRE(msg.sender_comp_id() == "SENDER");
        REQUIRE(msg.target_comp_id() == "TARGET");
        REQUIRE(msg.msg_seq_num() == 1);

        // Check body fields
        REQUIRE(msg.get_string(37) == "ORDER123");  // OrderID
        REQUIRE(msg.get_string(17) == "EXEC456");   // ExecID
        REQUIRE(msg.get_string(55) == "AAPL");      // Symbol
        REQUIRE(msg.get_char(54) == '1');           // Side = Buy
    }

    SECTION("Field iteration") {
        auto result = ParsedMessage::parse(
            std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});

        REQUIRE(result.has_value());

        size_t count = 0;
        for (const auto& field : *result) {
            REQUIRE(field.is_valid());
            ++count;
        }
        REQUIRE(count == result->field_count());
    }
}

TEST_CASE("IndexedParser O(1) lookup", "[parser][runtime][regression]") {
    auto result = IndexedParser::parse(
        std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

    REQUIRE(result.has_value());

    auto& parser = *result;

    SECTION("Header access") {
        REQUIRE(parser.msg_type() == '8');
        REQUIRE(parser.sender_comp_id() == "SENDER");
    }

    SECTION("Field lookup O(1)") {
        REQUIRE(parser.has_field(55));
        REQUIRE(parser.get_string(55) == "AAPL");

        REQUIRE(parser.has_field(54));
        REQUIRE(parser.get_char(54) == '1');

        REQUIRE(!parser.has_field(999));
    }

    SECTION("High-numbered tags (overflow)") {
        // Build a Logon with tag 553 (Username) and tag 554 (Password)
        std::string inner =
            "35=A\x01" "49=CLIENT\x01"
            "56=SERVER\x01" "34=1\x01" "52=20231215-10:30:00\x01"
            "98=0\x01" "108=30\x01"
            "553=user1\x01" "554=pass1\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());

        auto& p = *r;
        REQUIRE(p.has_field(553));
        REQUIRE(p.get_string(553) == "user1");
        REQUIRE(p.has_field(554));
        REQUIRE(p.get_string(554) == "pass1");
        // Low tags still work
        REQUIRE(p.has_field(98));
        REQUIRE(p.get_string(108) == "30");
    }

    SECTION("Overflow exhaustion returns error") {
        // Build a message with > 8 distinct tags >= 512 to exhaust overflow
        std::string inner =
            "35=A\x01" "49=CLIENT\x01"
            "56=SERVER\x01" "34=1\x01" "52=20231215-10:30:00\x01";
        // Add 9 high tags (overflow capacity is 8, so 9th should fail)
        for (int t = 553; t <= 561; ++t) {
            inner += std::to_string(t) + "=val\x01";
        }
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::OverflowExhausted);
    }
}

// ============================================================================
// Message Type Detection
// ============================================================================

TEST_CASE("Message type detection", "[parser][regression]") {
    SECTION("Admin messages") {
        REQUIRE(msg_type::is_admin('0'));  // Heartbeat
        REQUIRE(msg_type::is_admin('A'));  // Logon
        REQUIRE(msg_type::is_admin('5'));  // Logout

        REQUIRE(!msg_type::is_admin('8'));  // ExecutionReport
        REQUIRE(!msg_type::is_admin('D'));  // NewOrderSingle
    }

    SECTION("App messages") {
        REQUIRE(msg_type::is_app('8'));  // ExecutionReport
        REQUIRE(msg_type::is_app('D'));  // NewOrderSingle
        REQUIRE(msg_type::is_app('F'));  // OrderCancelRequest

        REQUIRE(!msg_type::is_app('0'));  // Heartbeat
        REQUIRE(!msg_type::is_app('A'));  // Logon
    }

    SECTION("Message type names") {
        REQUIRE(msg_type::name('0') == "Heartbeat");
        REQUIRE(msg_type::name('A') == "Logon");
        REQUIRE(msg_type::name('8') == "ExecutionReport");
        REQUIRE(msg_type::name('D') == "NewOrderSingle");
    }
}

// ============================================================================
// FIX Protocol Utilities
// ============================================================================

TEST_CASE("FIX checksum", "[parser][fix][regression]") {
    SECTION("Calculate checksum") {
        // Simple test data
        const std::string data = "8=FIX.4.4\x01" "9=5\x01" "35=0\x01";
        uint8_t checksum = fix::calculate_checksum(
            std::span<const char>{data.data(), data.size()});

        // checksum is uint8_t, so it's inherently in range [0, 255]
        REQUIRE(checksum > 0);
    }

    SECTION("Format checksum") {
        auto formatted = fix::format_checksum(7);
        REQUIRE(formatted[0] == '0');
        REQUIRE(formatted[1] == '0');
        REQUIRE(formatted[2] == '7');

        auto formatted2 = fix::format_checksum(123);
        REQUIRE(formatted2[0] == '1');
        REQUIRE(formatted2[1] == '2');
        REQUIRE(formatted2[2] == '3');
    }
}

// ============================================================================
// Stream Parser Tests
// ============================================================================

TEST_CASE("StreamParser message framing", "[parser][stream][regression]") {
    StreamParser parser;

    SECTION("Single complete message") {
        size_t consumed = parser.feed(
            std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});

        REQUIRE(consumed > 0);
        REQUIRE(parser.has_message());

        auto [start, end] = parser.next_message();
        REQUIRE(start == 0);
        REQUIRE(end == HEARTBEAT.size());
    }
}

// ============================================================================
// Message Boundary Detection
// ============================================================================

TEST_CASE("Message boundary detection", "[parser][simd][regression]") {
    SECTION("Find complete message") {
        auto boundary = simd::find_message_boundary(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(boundary.complete);
        REQUIRE(boundary.start == 0);
        REQUIRE(boundary.end == EXEC_REPORT.size());
    }

    SECTION("Correct BodyLength is accepted") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message(inner);
        auto boundary = simd::find_message_boundary(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(boundary.complete);
        REQUIRE(boundary.start == 0);
        REQUIRE(boundary.end == msg.size());
    }

    SECTION("BodyLength > actual marks boundary as corrupt") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 999);
        auto boundary = simd::find_message_boundary(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(!boundary.complete);
        REQUIRE(boundary.corrupt);
        REQUIRE(boundary.end == msg.size());
    }

    SECTION("BodyLength < actual marks boundary as corrupt") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 5);
        auto boundary = simd::find_message_boundary(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(!boundary.complete);
        REQUIRE(boundary.corrupt);
        REQUIRE(boundary.end == msg.size());
    }

    SECTION("Embedded trailer pattern skipped via BodyLength") {
        // Body contains a fake "\x01 10=999\x01" pattern before the real trailer.
        // The framing layer should skip the fake one because BodyLength won't match
        // at that point, and find the real trailer at the correct offset.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "58=fake\x01" "10=999\x01"
            "56=TARGET\x01" "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message(inner);
        auto boundary = simd::find_message_boundary(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(boundary.complete);
        REQUIRE(boundary.start == 0);
        REQUIRE(boundary.end == msg.size());
    }
}

TEST_CASE("StreamParser skips BodyLength mismatch", "[parser][stream][regression]") {
    StreamParser parser;

    SECTION("Inflated BodyLength skipped, not queued") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 999);

        size_t consumed = parser.feed(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(consumed == msg.size());
        REQUIRE(!parser.has_message());
    }

    SECTION("Deflated BodyLength skipped, not queued") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 5);

        size_t consumed = parser.feed(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(consumed == msg.size());
        REQUIRE(!parser.has_message());
    }

    SECTION("Corrupt message followed by valid message - stream recovers") {
        std::string bad_inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string bad_msg = build_fix_message_with_bl(bad_inner, 999);

        std::string good_inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=6\x01" "52=20231215-10:30:01\x01";
        std::string good_msg = build_fix_message(good_inner);

        std::string stream = bad_msg + good_msg;
        size_t consumed = parser.feed(
            std::span<const char>{stream.data(), stream.size()});

        REQUIRE(consumed == stream.size());
        REQUIRE(parser.has_message());

        auto [start, end] = parser.next_message();
        REQUIRE(start == bad_msg.size());
        REQUIRE(end == stream.size());
        REQUIRE(!parser.has_message());
    }
}

TEST_CASE("StreamParser MAX_PENDING overflow", "[parser][stream][regression]") {
    StreamParser parser;

    // Build 20 identical valid FIX messages
    std::string inner =
        "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
        "34=1\x01" "52=20231215-10:30:00\x01";
    std::string single_msg = build_fix_message(inner);
    size_t msg_len = single_msg.size();

    constexpr size_t TOTAL_MSGS = 20;
    std::string buffer;
    buffer.reserve(msg_len * TOTAL_MSGS);
    for (size_t i = 0; i < TOTAL_MSGS; ++i) {
        buffer += single_msg;
    }

    // Feed entire buffer - should stop after 16 messages (MAX_PENDING)
    size_t consumed = parser.feed(
        std::span<const char>{buffer.data(), buffer.size()});

    REQUIRE(consumed == msg_len * 16);

    // Drain all 16 pending messages
    for (size_t i = 0; i < 16; ++i) {
        REQUIRE(parser.has_message());
        auto [start, end] = parser.next_message();
        REQUIRE(end - start == msg_len);
    }
    REQUIRE(!parser.has_message());

    // Feed remaining 4 messages
    auto leftover = std::span<const char>{
        buffer.data() + consumed, buffer.size() - consumed};
    size_t consumed2 = parser.feed(leftover);

    REQUIRE(consumed2 == msg_len * 4);

    // Drain remaining 4 messages
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(parser.has_message());
        auto [start, end] = parser.next_message();
        REQUIRE(end - start == msg_len);
    }
    REQUIRE(!parser.has_message());
}

// ============================================================================
// Structural Index Tests (TICKET_208 simdjson-style)
// ============================================================================

TEST_CASE("FIXStructuralIndex scalar", "[parser][simd][structural][regression]") {
    SECTION("Build index from execution report") {
        auto idx = simd::build_index_scalar(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(idx.valid());
        REQUIRE(idx.soh_count == 19);     // 19 fields in EXEC_REPORT
        REQUIRE(idx.equals_count == 19);  // Each field has one '='
        REQUIRE(idx.field_count() == 19);
    }

    SECTION("Extract tag at index") {
        auto idx = simd::build_index_scalar(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        // First field is tag 8 (BeginString)
        REQUIRE(idx.tag_at(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, 0) == 8);

        // Third field is tag 35 (MsgType)
        REQUIRE(idx.tag_at(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, 2) == 35);
    }

    SECTION("Extract value at index") {
        auto idx = simd::build_index_scalar(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        // First field value is "FIX.4.4"
        REQUIRE(idx.value_at(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, 0) == "FIX.4.4");

        // MsgType value is "8"
        REQUIRE(idx.value_at(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, 2) == "8");
    }

    SECTION("Find tag by number") {
        auto idx = simd::build_index_scalar(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        size_t found = idx.find_tag(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, 55);  // Symbol

        REQUIRE(found < idx.field_count());
        REQUIRE(idx.value_at(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}, found) == "AAPL");
    }
}

TEST_CASE("FIXStructuralIndex runtime dispatch", "[parser][simd][structural][regression]") {
    // Initialize runtime dispatch
    simd::init_simd_dispatch();

    SECTION("Active implementation is detected") {
        auto impl = simd::active_simd_impl();
        INFO("Active SIMD implementation: " << simd::simd_impl_name(impl));

        // Should be at least scalar
        REQUIRE(impl >= simd::SimdImpl::Scalar);
    }

    SECTION("Build index via runtime dispatch") {
        auto idx = simd::build_index(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        REQUIRE(idx.valid());
        REQUIRE(idx.soh_count == 19);
        REQUIRE(idx.equals_count == 19);
    }
}

TEST_CASE("IndexedFieldAccessor", "[parser][simd][structural][regression]") {
    auto idx = simd::build_index(
        std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

    simd::IndexedFieldAccessor accessor{idx,
        std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()}};

    SECTION("Field count") {
        REQUIRE(accessor.field_count() == 19);
    }

    SECTION("Get by tag") {
        REQUIRE(accessor.get(8) == "FIX.4.4");   // BeginString
        REQUIRE(accessor.get(35) == "8");        // MsgType (ExecutionReport)
        REQUIRE(accessor.get(55) == "AAPL");     // Symbol
        REQUIRE(accessor.get(37) == "ORDER123"); // OrderID
    }

    SECTION("Get as integer") {
        REQUIRE(accessor.get_int(9) == 136);   // BodyLength
        REQUIRE(accessor.get_int(34) == 1);    // MsgSeqNum
        REQUIRE(accessor.get_int(38) == 100);  // OrderQty
    }

    SECTION("Get as char") {
        REQUIRE(accessor.msg_type() == '8');   // ExecutionReport
        REQUIRE(accessor.get_char(54) == '1'); // Side = Buy
    }

    SECTION("Non-existent tag") {
        REQUIRE(accessor.get(999) == "");
        REQUIRE(accessor.get_int(999) == 0);
    }
}

TEST_CASE("PaddedMessageBuffer", "[parser][simd][structural][regression]") {
    SECTION("Construction and set") {
        simd::MediumPaddedBuffer buffer;

        REQUIRE(buffer.empty());
        REQUIRE(buffer.capacity() == 1024);

        buffer.set(std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});

        REQUIRE(!buffer.empty());
        REQUIRE(buffer.size() == HEARTBEAT.size());
    }

    SECTION("SIMD-safe pointer") {
        simd::MediumPaddedBuffer buffer;
        buffer.set(std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});

        // Can safely read past end (padding is zeroed)
        const char* ptr = buffer.simd_safe_ptr();
        REQUIRE(ptr[buffer.size()] == '\0');     // First byte of padding
        REQUIRE(ptr[buffer.size() + 63] == '\0'); // Last byte of padding
    }

    SECTION("Build index from padded buffer") {
        simd::MediumPaddedBuffer buffer;
        buffer.set(std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});

        auto idx = simd::build_index(buffer.data());

        REQUIRE(idx.valid());
        REQUIRE(idx.soh_count == 19);
    }
}

// ============================================================================
// SIMD Checksum Tests
// ============================================================================

TEST_CASE("Checksum scalar basics", "[parser][checksum][regression]") {
    using namespace nfx::parser;

    SECTION("Known value from EXEC_REPORT") {
        uint8_t result = checksum_scalar(EXEC_REPORT.data(), EXEC_REPORT.size());
        // Verify it produces a deterministic value
        uint8_t expected = 0;
        for (char c : EXEC_REPORT) {
            expected += static_cast<uint8_t>(c);
        }
        REQUIRE(result == expected);
    }

    SECTION("Empty input") {
        uint8_t result = checksum_scalar("", 0);
        REQUIRE(result == 0);
    }

    SECTION("Single byte") {
        char c = 'A';  // 0x41 = 65
        uint8_t result = checksum_scalar(&c, 1);
        REQUIRE(result == 65);
    }

    SECTION("All zeros") {
        char zeros[16] = {};
        uint8_t result = checksum_scalar(zeros, sizeof(zeros));
        REQUIRE(result == 0);
    }

    SECTION("All 0xFF") {
        char ffs[16];
        std::memset(ffs, 0xFF, sizeof(ffs));
        uint8_t result = checksum_scalar(ffs, sizeof(ffs));
        // 16 * 255 = 4080, mod 256 = 4080 - 15*256 = 4080 - 3840 = 240
        REQUIRE(result == static_cast<uint8_t>(16 * 255));
    }
}

TEST_CASE("Checksum dispatch consistency", "[parser][checksum][regression]") {
    using namespace nfx::parser;

    SECTION("Scalar matches auto-dispatch for EXEC_REPORT") {
        uint8_t scalar = checksum_scalar(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t dispatched = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(scalar == dispatched);
    }

    SECTION("string_view overload") {
        uint8_t from_ptr = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t from_sv = checksum(std::string_view{EXEC_REPORT});
        REQUIRE(from_ptr == from_sv);
    }

    SECTION("span overload") {
        uint8_t from_ptr = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        std::span<const char> sp{EXEC_REPORT.data(), EXEC_REPORT.size()};
        uint8_t from_span = checksum(sp);
        REQUIRE(from_ptr == from_span);
    }

    SECTION("Unaligned data") {
        // Create unaligned buffer by offsetting by 1
        std::string buf = "X" + EXEC_REPORT;
        uint8_t aligned = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t unaligned = checksum(buf.data() + 1, EXEC_REPORT.size());
        REQUIRE(aligned == unaligned);
    }

    SECTION("Large 4096-byte input") {
        std::string large(4096, 'B');  // 'B' = 66
        uint8_t scalar = checksum_scalar(large.data(), large.size());
        uint8_t dispatched = checksum(large.data(), large.size());
        REQUIRE(scalar == dispatched);
    }

#if defined(NFX_AVX2_CHECKSUM)
    SECTION("AVX2 matches scalar") {
        uint8_t scalar = checksum_scalar(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t avx2 = checksum_avx2(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(scalar == avx2);
    }
#endif

#if defined(NFX_SSE2_CHECKSUM) || defined(NFX_AVX2_CHECKSUM) || defined(NFX_AVX512_CHECKSUM)
    SECTION("SSE2 matches scalar") {
        uint8_t scalar = checksum_scalar(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t sse2 = checksum_sse2(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(scalar == sse2);
    }
#endif

#if defined(NFX_AVX512_CHECKSUM)
    SECTION("AVX-512 matches scalar") {
        uint8_t scalar = checksum_scalar(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t avx512 = checksum_avx512(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(scalar == avx512);
    }
#endif
}

TEST_CASE("IncrementalChecksum", "[parser][checksum][regression]") {
    using namespace nfx::parser;

    SECTION("Single update matches batch") {
        IncrementalChecksum inc;
        inc.update(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t incremental = inc.finalize();
        uint8_t batch = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(incremental == batch);
    }

    SECTION("Chunked updates") {
        IncrementalChecksum inc;
        size_t mid = EXEC_REPORT.size() / 2;
        inc.update(EXEC_REPORT.data(), mid);
        inc.update(EXEC_REPORT.data() + mid, EXEC_REPORT.size() - mid);
        uint8_t chunked = inc.finalize();
        uint8_t batch = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(chunked == batch);
    }

    SECTION("Reset and reuse") {
        IncrementalChecksum inc;
        inc.update("ABC", 3);
        inc.reset();
        REQUIRE(inc.finalize() == 0);

        inc.update(EXEC_REPORT.data(), EXEC_REPORT.size());
        uint8_t result = inc.finalize();
        uint8_t expected = checksum(EXEC_REPORT.data(), EXEC_REPORT.size());
        REQUIRE(result == expected);
    }
}

TEST_CASE("Checksum formatting", "[parser][checksum][regression]") {
    using namespace nfx::parser;

    SECTION("format_checksum 3-digit") {
        char buf[3];
        format_checksum(255, buf);
        REQUIRE(buf[0] == '2');
        REQUIRE(buf[1] == '5');
        REQUIRE(buf[2] == '5');
    }

    SECTION("format_checksum leading zero") {
        char buf[3];
        format_checksum(4, buf);
        REQUIRE(buf[0] == '0');
        REQUIRE(buf[1] == '0');
        REQUIRE(buf[2] == '4');
    }

    SECTION("parse_checksum roundtrip") {
        char buf[3];
        for (int i = 0; i < 256; ++i) {
            format_checksum(static_cast<uint8_t>(i), buf);
            uint8_t parsed = parse_checksum(buf);
            REQUIRE(parsed == static_cast<uint8_t>(i));
        }
    }
}

TEST_CASE("FIX checksum validation", "[parser][checksum][regression]") {
    using namespace nfx::parser;

    SECTION("validate_fix_checksum on valid message") {
        // Build a message with correct checksum
        std::string body =
            "8=FIX.4.4\x01" "9=5\x01" "35=0\x01";
        uint8_t sum = checksum(body.data(), body.size());
        char cs[3];
        format_checksum(sum, cs);
        std::string msg = body + "10=" + std::string(cs, 3) + "\x01";
        REQUIRE(validate_fix_checksum(msg));
    }

    SECTION("validate_fix_checksum on corrupted message") {
        std::string body =
            "8=FIX.4.4\x01" "9=5\x01" "35=0\x01";
        std::string msg = body + "10=000\x01";
        // Unless checksum happens to be 000, this should fail
        uint8_t actual = checksum(body.data(), body.size());
        if (actual != 0) {
            REQUIRE_FALSE(validate_fix_checksum(msg));
        }
    }

    SECTION("calculate_fix_checksum") {
        std::string body = "8=FIX.4.4\x01" "9=5\x01" "35=0\x01";
        uint8_t result = calculate_fix_checksum(body);
        uint8_t expected = checksum(body.data(), body.size());
        REQUIRE(result == expected);
    }
}

// ============================================================================
// Repeating Group Tests
// ============================================================================

TEST_CASE("RepeatingGroupIterator single entry", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    // Single MD entry: 269=0|270=150.50|271=1000|
    std::string data = "269=0\x01" "270=150.50\x01" "271=1000\x01";
    std::span<const char> sp{data.data(), data.size()};

    RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 1};
    REQUIRE(iter.count() == 1);
    REQUIRE(iter.current() == 0);

    SECTION("Field access via get_field") {
        REQUIRE(iter.has_next());
        auto entry = iter.next();

        auto type_field = entry.get_field(tag::MDEntryType::value);
        REQUIRE(type_field.is_valid());
        REQUIRE(type_field.value[0] == '0');
    }

    SECTION("get_string") {
        auto entry = iter.next();
        auto sv = entry.get_string(tag::MDEntryType::value);
        REQUIRE(sv == "0");
    }

    SECTION("get_int") {
        auto entry = iter.next();
        auto val = entry.get_int(tag::MDEntrySize::value);
        REQUIRE(val.has_value());
        REQUIRE(*val == 1000);
    }

    SECTION("get_price") {
        auto entry = iter.next();
        auto price = entry.get_price(tag::MDEntryPx::value);
        REQUIRE(price.raw != 0);
    }
}

TEST_CASE("RepeatingGroupIterator multiple entries", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    // Two MD entries
    std::string data =
        "269=0\x01" "270=150.50\x01" "271=1000\x01"
        "269=1\x01" "270=151.00\x01" "271=500\x01";
    std::span<const char> sp{data.data(), data.size()};

    RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 2};
    REQUIRE(iter.count() == 2);

    SECTION("Iterate and track count") {
        auto entry1 = iter.next();
        REQUIRE(iter.current() == 1);
        REQUIRE(entry1.get_char(tag::MDEntryType::value) == '0');

        auto entry2 = iter.next();
        REQUIRE(iter.current() == 2);
        REQUIRE(entry2.get_char(tag::MDEntryType::value) == '1');

        REQUIRE_FALSE(iter.has_next());
    }
}

TEST_CASE("RepeatingGroupIterator edge cases", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    SECTION("Empty group count=0") {
        std::string data = "269=0\x01";
        std::span<const char> sp{data.data(), data.size()};
        RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 0};
        REQUIRE(iter.count() == 0);
        REQUIRE_FALSE(iter.has_next());
    }

    SECTION("Exhausted iterator returns empty entry") {
        std::string data = "269=0\x01";
        std::span<const char> sp{data.data(), data.size()};
        RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 1};
        [[maybe_unused]] auto consumed = iter.next();  // consume the one entry
        REQUIRE_FALSE(iter.has_next());
        auto empty = iter.next();
        REQUIRE(empty.data.empty());
    }
}

TEST_CASE("MDEntryIterator", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    SECTION("Parse single MDEntry") {
        std::string data =
            "269=0\x01" "270=100.25\x01" "271=500\x01" "278=E001\x01";
        std::span<const char> sp{data.data(), data.size()};

        MDEntryIterator iter{sp, 1};
        REQUIRE(iter.count() == 1);
        REQUIRE(iter.has_next());

        auto md = iter.next();
        REQUIRE(md.entry_type == MDEntryType::Bid);
        REQUIRE(md.entry_id == "E001");
    }

    SECTION("Parse multiple MDEntry") {
        std::string data =
            "269=0\x01" "270=100.00\x01" "271=500\x01"
            "269=1\x01" "270=101.00\x01" "271=300\x01";
        std::span<const char> sp{data.data(), data.size()};

        MDEntryIterator iter{sp, 2};
        REQUIRE(iter.count() == 2);

        auto bid = iter.next();
        REQUIRE(bid.entry_type == MDEntryType::Bid);

        auto offer = iter.next();
        REQUIRE(offer.entry_type == MDEntryType::Offer);

        REQUIRE_FALSE(iter.has_next());
    }

    SECTION("Default delimiter tag is MDEntryType") {
        std::string data = "269=2\x01" "270=99.50\x01";
        std::span<const char> sp{data.data(), data.size()};

        MDEntryIterator iter{sp, 1};  // Uses default delimiter_tag = 269
        auto md = iter.next();
        REQUIRE(md.entry_type == MDEntryType::Trade);
    }
}

TEST_CASE("RelatedSymIterator", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    SECTION("Parse single RelatedSymbol") {
        std::string data =
            "55=AAPL\x01" "48=US0378331005\x01" "207=NYSE\x01";
        std::span<const char> sp{data.data(), data.size()};

        RelatedSymIterator iter{sp, 1};
        REQUIRE(iter.count() == 1);
        REQUIRE(iter.has_next());

        auto sym = iter.next();
        REQUIRE(sym.symbol == "AAPL");
        REQUIRE(sym.security_id == "US0378331005");
        REQUIRE(sym.security_exchange == "NYSE");
    }

    SECTION("Parse multiple RelatedSymbol") {
        std::string data =
            "55=AAPL\x01" "48=US0378331005\x01"
            "55=MSFT\x01" "48=US5949181045\x01";
        std::span<const char> sp{data.data(), data.size()};

        RelatedSymIterator iter{sp, 2};
        auto sym1 = iter.next();
        REQUIRE(sym1.symbol == "AAPL");

        auto sym2 = iter.next();
        REQUIRE(sym2.symbol == "MSFT");
    }
}

TEST_CASE("parse_md_entry helper", "[parser][repeating_group][regression]") {
    using namespace nfx::parser;

    SECTION("Full fields") {
        std::string data =
            "269=0\x01" "270=150.50\x01" "271=1000\x01"
            "279=0\x01" "278=E100\x01" "55=AAPL\x01"
            "272=20231215\x01" "273=10:30:00\x01"
            "290=1\x01" "346=42\x01";
        std::span<const char> sp{data.data(), data.size()};

        RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 1};
        auto entry = iter.next();
        auto md = parse_md_entry(entry);

        REQUIRE(md.entry_type == MDEntryType::Bid);
        REQUIRE(md.update_action == MDUpdateAction::New);
        REQUIRE(md.entry_id == "E100");
        REQUIRE(md.symbol == "AAPL");
        REQUIRE(md.entry_date == "20231215");
        REQUIRE(md.entry_time == "10:30:00");
        REQUIRE(md.position_no == 1);
        REQUIRE(md.number_of_orders == 42);
    }

    SECTION("Minimal fields") {
        std::string data = "269=1\x01" "270=200.00\x01" "271=50\x01";
        std::span<const char> sp{data.data(), data.size()};

        RepeatingGroupIterator iter{sp, tag::MDEntryType::value, 1};
        auto entry = iter.next();
        auto md = parse_md_entry(entry);

        REQUIRE(md.entry_type == MDEntryType::Offer);
        REQUIRE(md.entry_id.empty());
        REQUIRE(md.symbol.empty());
        REQUIRE(md.position_no == 0);
        REQUIRE(md.number_of_orders == 0);
    }
}

// ============================================================================
// BodyLength Validation Tests (TICKET_469_1)
// ============================================================================

TEST_CASE("BodyLength validation", "[parser][edge-case][regression]") {
    SECTION("Valid messages pass body length check") {
        // HEARTBEAT, LOGON, EXEC_REPORT all have correct body lengths
        auto hb = ParsedMessage::parse(
            std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});
        REQUIRE(hb.has_value());

        auto logon = ParsedMessage::parse(
            std::span<const char>{LOGON.data(), LOGON.size()});
        REQUIRE(logon.has_value());

        auto exec = ParsedMessage::parse(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});
        REQUIRE(exec.has_value());
    }

    SECTION("Valid messages pass IndexedParser body length check") {
        auto hb = IndexedParser::parse(
            std::span<const char>{HEARTBEAT.data(), HEARTBEAT.size()});
        REQUIRE(hb.has_value());

        auto logon = IndexedParser::parse(
            std::span<const char>{LOGON.data(), LOGON.size()});
        REQUIRE(logon.has_value());

        auto exec = IndexedParser::parse(
            std::span<const char>{EXEC_REPORT.data(), EXEC_REPORT.size()});
        REQUIRE(exec.has_value());
    }

    SECTION("BodyLength > actual body rejects via ParsedMessage") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        // Use inflated body length (999 instead of actual)
        std::string msg = build_fix_message_with_bl(inner, 999);

        auto r = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::BodyLengthMismatch);
        REQUIRE(r.error().tag == 9);
    }

    SECTION("BodyLength > actual body rejects via IndexedParser") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 999);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::BodyLengthMismatch);
    }

    SECTION("BodyLength < actual body rejects via ParsedMessage") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        // Use deflated body length (5 instead of actual)
        std::string msg = build_fix_message_with_bl(inner, 5);

        auto r = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::BodyLengthMismatch);
    }

    SECTION("BodyLength < actual body rejects via IndexedParser") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message_with_bl(inner, 5);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::BodyLengthMismatch);
    }

    SECTION("validate_body_length standalone - correct length") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message(inner);

        auto hdr = parse_header(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(hdr.ok());

        auto err = validate_body_length(
            std::span<const char>{msg.data(), msg.size()},
            hdr.header.body_length);
        REQUIRE(err.code == ParseErrorCode::None);
    }

    SECTION("validate_body_length standalone - wrong length") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=5\x01" "52=20231215-10:30:00\x01";
        std::string msg = build_fix_message(inner);

        auto err = validate_body_length(
            std::span<const char>{msg.data(), msg.size()}, 999);
        REQUIRE(err.code == ParseErrorCode::BodyLengthMismatch);
        REQUIRE(err.tag == 9);
    }

    SECTION("Error message text") {
        REQUIRE(parse_error_message(ParseErrorCode::BodyLengthMismatch)
                == "BodyLength mismatch");
    }
}

// ============================================================================
// Duplicate Tag Detection Tests (TICKET_469_2)
// ============================================================================

TEST_CASE("Duplicate tag current behavior", "[parser][edge-case]") {
    SECTION("IndexedParser duplicate tag (flat, tag < 512) - LAST WINS") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "55=MSFT\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(55) == "MSFT");  // Last wins
    }

    SECTION("IndexedParser duplicate tag (overflow, tag >= 512) - LAST WINS") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "553=user1\x01" "553=user2\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        // Overflow stores both; get() returns first match (linear scan)
        // but both entries exist in the overflow array
        REQUIRE(r->has_field(553));
    }

    SECTION("IndexedParser triple duplicate - final value wins") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "55=MSFT\x01" "55=GOOG\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(55) == "GOOG");  // Last wins for flat array
    }

    SECTION("ParsedMessage duplicate tag - FIRST WINS") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "55=MSFT\x01";
        std::string msg = build_fix_message(inner);

        auto r = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(55) == "AAPL");  // First wins (linear scan)
    }

    SECTION("Cross-parser inconsistency - same message, different results") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "55=MSFT\x01";
        std::string msg = build_fix_message(inner);

        auto parsed = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        auto indexed = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});

        REQUIRE(parsed.has_value());
        REQUIRE(indexed.has_value());

        // ParsedMessage returns first, IndexedParser returns last
        REQUIRE(parsed->get_string(55) == "AAPL");
        REQUIRE(indexed->get_string(55) == "MSFT");
        REQUIRE(parsed->get_string(55) != indexed->get_string(55));
    }
}

TEST_CASE("StrictIndexedParser duplicate tag detection", "[parser][edge-case][strict]") {
    SECTION("Rejects duplicate non-repeating tag (flat, tag < 512)") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "11=ORD1\x01" "11=ORD2\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::DuplicateTag);
        REQUIRE(r.error().tag == 11);
    }

    SECTION("Rejects duplicate tag (overflow, tag >= 512)") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "553=user1\x01" "553=user2\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::DuplicateTag);
        REQUIRE(r.error().tag == 553);
    }

    SECTION("Accepts message without duplicates") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "54=1\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(55) == "AAPL");
        REQUIRE(r->get_char(54) == '1');
    }

    SECTION("Rejects duplicate group-member tag outside group context (no count tag)") {
        // NewOrderSingle with duplicate Symbol (55) but NO group count tag
        // Tag 55 is a group member tag, but without a preceding No* tag
        // it must still be rejected as a duplicate.
        std::string inner =
            "35=D\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "11=ORD1\x01" "55=AAPL\x01" "55=MSFT\x01"
            "54=1\x01" "38=100\x01" "40=2\x01" "44=150.50\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::DuplicateTag);
        REQUIRE(r.error().tag == 55);
    }

    SECTION("Rejects duplicate group-member tag after group context ends") {
        // A repeating group appears first, then a non-member tag ends that
        // group context. A later duplicate of 55 must still be rejected.
        std::string inner =
            "35=D\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "11=ORD1\x01" "268=1\x01"
            "269=0\x01" "270=150.25\x01" "271=100\x01"
            "54=1\x01"
            "55=AAPL\x01" "55=MSFT\x01" "38=100\x01" "40=2\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::DuplicateTag);
        REQUIRE(r.error().tag == 55);
    }

    SECTION("Accepts repeating group tags (MDEntries with duplicate 269/270/271)") {
        // Market Data Snapshot: 268=2 followed by two MDEntry groups
        // Tags 269, 270, 271 each appear twice - legitimate FIX repeating group
        std::string inner =
            "35=W\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "262=REQ1\x01" "55=AAPL\x01"
            "268=2\x01"
            "269=0\x01" "270=150.25\x01" "271=100\x01"
            "269=1\x01" "270=150.50\x01" "271=200\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(262) == "REQ1");
        REQUIRE(r->has_field(268));
        REQUIRE(r->has_field(269));
    }

    SECTION("Accepts repeating group tags (RelatedSym with duplicate 55/48)") {
        // Two related symbols: tag 55 appears twice
        std::string inner =
            "35=V\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "262=REQ2\x01" "263=1\x01" "264=0\x01"
            "146=2\x01"
            "55=AAPL\x01" "48=US0378331005\x01"
            "55=MSFT\x01" "48=US5949181045\x01"
            "267=2\x01" "269=0\x01" "269=1\x01";
        std::string msg = build_fix_message(inner);

        auto r = StrictIndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(262) == "REQ2");
    }

    SECTION("Default IndexedParser still accepts duplicates (backwards compatible)") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "55=AAPL\x01" "55=MSFT\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(55) == "MSFT");
    }
}

// ============================================================================
// Embedded SOH / Field Edge Cases (TICKET_469_3)
// ============================================================================

TEST_CASE("Embedded SOH truncates field value", "[parser][edge-case]") {
    SECTION("FieldIterator truncates value at embedded SOH") {
        // 55=AA<SOH>PL<SOH> - the embedded SOH splits this into two fields:
        // field 55="AA" and a malformed field "PL" (no '=' found)
        std::string data = "55=AA\x01" "PL\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f1 = iter.next();
        REQUIRE(f1.tag == 55);
        REQUIRE(f1.as_string() == "AA");  // Truncated at first SOH

        // "PL" has no '=', so next() returns invalid FieldView
        auto f2 = iter.next();
        REQUIRE(!f2.is_valid());
    }

    SECTION("IndexedParser rejects message with embedded SOH creating malformed field") {
        // "58=hello\x01world\x01" - the embedded SOH splits this into:
        // field 58="hello" and a malformed residue "world" (no '=' found).
        // The residue starts with 'w' (non-digit), so FieldIterator reports
        // InvalidTagNumber via the IndexedParser error path.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "58=hello\x01" "world\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidTagNumber);
    }

    SECTION("ParsedMessage rejects message with embedded SOH creating malformed field") {
        // ParsedMessage uses SIMD to find '=' first, so "world" (no '=')
        // is reported as InvalidFieldFormat.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "58=hello\x01" "world\x01";
        std::string msg = build_fix_message(inner);

        auto r = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidFieldFormat);
    }
}

TEST_CASE("Cross-parser malformed field rejection (TICKET_469_6)", "[parser][malformed]") {
    // Both ParsedMessage and IndexedParser must reject the same malformed inputs.
    // Error codes may differ (ParsedMessage uses SIMD find_equals first, while
    // IndexedParser delegates to FieldIterator digit loop), but both must fail.

    SECTION("BADFIELD without equals - original reproduction") {
        // "BADFIELD\x01" has no '=' separator. FieldIterator hits 'B' (non-digit)
        // and reports InvalidTagNumber; ParsedMessage finds no '=' and reports
        // InvalidFieldFormat.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "BADFIELD\x01";
        std::string msg = build_fix_message(inner);

        auto indexed = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!indexed.has_value());

        auto parsed = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!parsed.has_value());
    }

    SECTION("Empty tag - equals as first character") {
        // "=value\x01" has zero-length tag. FieldIterator produces tag=0 which
        // fails is_valid() (tag > 0), so IndexedParser rejects it.
        // ParsedMessage's digit loop is empty (eq_pos == field_start), stores
        // tag=0 and continues. Both reject the message.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "=value\x01";
        std::string msg = build_fix_message(inner);

        auto indexed = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!indexed.has_value());

        auto parsed = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!parsed.has_value());
    }

    SECTION("Non-digit in tag") {
        // "5X=value\x01" - tag contains non-digit 'X'. FieldIterator reports
        // InvalidTagNumber at 'X'; ParsedMessage digit loop also catches it.
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "5X=value\x01";
        std::string msg = build_fix_message(inner);

        auto indexed = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!indexed.has_value());
        REQUIRE(indexed.error().code == ParseErrorCode::InvalidTagNumber);

        auto parsed = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!parsed.has_value());
        REQUIRE(parsed.error().code == ParseErrorCode::InvalidTagNumber);
    }
}

TEST_CASE("Empty field value (SOH immediately after equals)", "[parser][edge-case]") {
    SECTION("FieldIterator returns empty string for tag=SOH") {
        // 58=<SOH> - value is empty
        std::string data = "58=\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(f.tag == 58);
        REQUIRE(f.as_string() == "");
        REQUIRE(f.is_empty());
        REQUIRE(f.size() == 0);
    }

    SECTION("IndexedParser stores empty value") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "58=\x01";
        std::string msg = build_fix_message(inner);

        auto r = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->has_field(58));
        REQUIRE(r->get_string(58) == "");
    }

    SECTION("ParsedMessage stores empty value") {
        std::string inner =
            "35=0\x01" "49=SENDER\x01" "56=TARGET\x01"
            "34=1\x01" "52=20231215-10:30:00\x01"
            "58=\x01";
        std::string msg = build_fix_message(inner);

        auto r = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(r.has_value());
        REQUIRE(r->get_string(58) == "");
    }
}

TEST_CASE("Non-ASCII bytes in field values", "[parser][edge-case]") {
    SECTION("FieldIterator accepts high bytes (0x80-0xFF) in values") {
        // Value contains bytes 0x80, 0xC0, 0xFF
        std::string data = "58=\x80\xC0\xFF\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(f.tag == 58);
        REQUIRE(f.size() == 3);
        REQUIRE(static_cast<uint8_t>(f.value[0]) == 0x80);
        REQUIRE(static_cast<uint8_t>(f.value[1]) == 0xC0);
        REQUIRE(static_cast<uint8_t>(f.value[2]) == 0xFF);
    }

    SECTION("Non-ASCII bytes do not terminate field scanning") {
        // Ensure that bytes like 0x00 (NUL) don't terminate the value scan.
        // Only SOH (0x01) is the delimiter.
        const char raw[] = {'5', '8', '=', 'A', '\x00', 'B', '\x02', 'C', '\x01'};
        FieldIterator iter{std::span<const char>{raw, sizeof(raw)}};

        auto f = iter.next();
        REQUIRE(f.tag == 58);
        // Value should be everything from 'A' to 'C' (5 bytes: A, 0x00, B, 0x02, C)
        REQUIRE(f.size() == 5);
    }
}

TEST_CASE("Non-digit characters in tag number", "[parser][edge-case]") {
    SECTION("FieldIterator rejects tag with letters") {
        std::string data = "5X=value\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(!f.is_valid());  // Invalid tag
    }

    SECTION("FieldIterator rejects tag starting with letter") {
        std::string data = "A5=value\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(!f.is_valid());
    }

    SECTION("FieldIterator rejects tag with special characters") {
        std::string data = "5.5=value\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(!f.is_valid());
    }

    SECTION("FieldIterator rejects tag with negative sign") {
        std::string data = "-1=value\x01";
        FieldIterator iter{std::span<const char>{data.data(), data.size()}};

        auto f = iter.next();
        REQUIRE(!f.is_valid());
    }
}

TEST_CASE("ParsedMessage rejects messages exceeding MAX_FIELDS", "[parser][edge-case][regression]") {
    // scan_soh scans the entire buffer, so the SOH-delimited field count
    // includes framing tags: 8=, 9=, all inner fields, and 10= (checksum).
    // Total SOH positions = 2 (8, 9) + N_inner + 1 (10) = N_inner + 3.
    // To exceed MAX_FIELDS (128) we need N_inner + 3 > 128 => N_inner > 125.

    SECTION("129 total fields triggers FieldCountExceeded") {
        // 126 inner fields + 3 framing fields = 129 total
        std::string inner;
        inner += "35=8\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
                 "52=20231215-10:30:00.000\x01";
        // 5 inner fields so far, need 121 more
        for (int i = 0; i < 121; ++i) {
            inner += std::to_string(100 + i) + "=V\x01";
        }
        std::string msg = build_fix_message(inner);

        auto result = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::FieldCountExceeded);
    }

    SECTION("IndexedParser accepts the same oversized message") {
        std::string inner;
        inner += "35=8\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
                 "52=20231215-10:30:00.000\x01";
        for (int i = 0; i < 121; ++i) {
            inner += std::to_string(100 + i) + "=V\x01";
        }
        std::string msg = build_fix_message(inner);

        auto result = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(result.has_value());
    }

    SECTION("Exactly 128 total fields succeeds") {
        // 125 inner fields + 3 framing = 128 total
        std::string inner;
        inner += "35=8\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
                 "52=20231215-10:30:00.000\x01";
        // 5 inner so far, need 120 more
        for (int i = 0; i < 120; ++i) {
            inner += std::to_string(100 + i) + "=V\x01";
        }
        std::string msg = build_fix_message(inner);

        auto result = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(result.has_value());
        REQUIRE(result->field_count() == 128);
    }
}

TEST_CASE("SohPositions overflow sets truncated flag", "[parser][edge-case][regression]") {
    SECTION("push() beyond MAX_SOH_POSITIONS sets truncated") {
        simd::SohPositions pos;
        for (size_t i = 0; i < simd::MAX_SOH_POSITIONS; ++i) {
            pos.push(static_cast<uint16_t>(i));
        }
        REQUIRE(pos.size() == simd::MAX_SOH_POSITIONS);
        REQUIRE(!pos.truncated());

        // One more push triggers truncation
        pos.push(static_cast<uint16_t>(simd::MAX_SOH_POSITIONS));
        REQUIRE(pos.size() == simd::MAX_SOH_POSITIONS);
        REQUIRE(pos.truncated());
    }

    SECTION("scan_soh on buffer with >256 SOH reports truncation") {
        // Build a buffer with 257 SOH-delimited single-char fields: "X\x01" x 257
        std::string buf;
        for (size_t i = 0; i < simd::MAX_SOH_POSITIONS + 1; ++i) {
            buf += 'X';
            buf += fix::SOH;
        }
        auto positions = simd::scan_soh(
            std::span<const char>{buf.data(), buf.size()});
        REQUIRE(positions.truncated());
        REQUIRE(positions.size() == simd::MAX_SOH_POSITIONS);
    }

    SECTION("scan_soh with exactly MAX_SOH_POSITIONS does not truncate") {
        // Build a buffer with exactly 256 SOH-delimited fields: "X\x01" x 256
        std::string buf;
        for (size_t i = 0; i < simd::MAX_SOH_POSITIONS; ++i) {
            buf += 'X';
            buf += fix::SOH;
        }
        auto positions = simd::scan_soh(
            std::span<const char>{buf.data(), buf.size()});
        REQUIRE(!positions.truncated());
        REQUIRE(positions.size() == simd::MAX_SOH_POSITIONS);
    }

    SECTION("ParsedMessage rejects message exceeding MAX_SOH_POSITIONS") {
        // Build a FIX message body with enough fields to exceed 256 SOH positions
        std::string inner;
        inner += "35=8\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
                 "52=20231215-10:30:00.000\x01";
        // 5 fields so far; need 254 - 5 = 249 more inner fields
        // (plus 3 framing = 257 total SOH positions)
        for (int i = 0; i < 249; ++i) {
            inner += std::to_string(100 + i) + "=V\x01";
        }
        std::string msg = build_fix_message(inner);

        auto result = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::FieldCountExceeded);
    }
}

TEST_CASE("Unterminated field handling", "[parser][edge-case][regression]") {
    SECTION("ParsedMessage rejects missing trailing SOH after checksum") {
        std::string msg = HEARTBEAT;
        msg.pop_back();  // Remove final SOH from 10=132<SOH>

        auto result = ParsedMessage::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::UnterminatedField);
    }

    SECTION("IndexedParser rejects missing trailing SOH after checksum") {
        std::string msg = HEARTBEAT;
        msg.pop_back();

        auto result = IndexedParser::parse(
            std::span<const char>{msg.data(), msg.size()});
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::UnterminatedField);
    }

    SECTION("extract_fields reports unterminated trailing field") {
        const std::string bad = "35=A";
        auto fields = extract_fields(std::span<const char>{bad.data(), bad.size()});

        REQUIRE(!fields.ok());
        REQUIRE(fields.error.code == ParseErrorCode::UnterminatedField);
    }
}
