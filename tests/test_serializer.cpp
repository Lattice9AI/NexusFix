#include <catch2/catch_test_macros.hpp>

#include "nexusfix/serializer/constexpr_serializer.hpp"

#include <string>
#include <cstring>

using namespace nfx::serializer;

// ============================================================================
// IntToString Compile-time Tests
// ============================================================================

TEST_CASE("IntToString compile-time conversion", "[serializer][regression]") {
    SECTION("single digit") {
        constexpr IntToString<0> s0;
        REQUIRE(std::string_view(s0.c_str()) == "0");
        REQUIRE(s0.size() == 1);

        constexpr IntToString<9> s9;
        REQUIRE(std::string_view(s9.c_str()) == "9");
    }

    SECTION("multi digit") {
        constexpr IntToString<35> s35;
        REQUIRE(std::string_view(s35.c_str()) == "35");
        REQUIRE(s35.size() == 2);

        constexpr IntToString<108> s108;
        REQUIRE(std::string_view(s108.c_str()) == "108");
        REQUIRE(s108.size() == 3);

        constexpr IntToString<1128> s1128;
        REQUIRE(std::string_view(s1128.c_str()) == "1128");
    }

    SECTION("negative numbers") {
        constexpr IntToString<-1> sn1;
        REQUIRE(std::string_view(sn1.c_str()) == "-1");
        REQUIRE(sn1.size() == 2);

        constexpr IntToString<-42> sn42;
        REQUIRE(std::string_view(sn42.c_str()) == "-42");
    }
}

// ============================================================================
// TagString Compile-time Tests
// ============================================================================

TEST_CASE("TagString compile-time generation", "[serializer][regression]") {
    SECTION("common FIX tags") {
        REQUIRE(std::string_view(TAG_8.c_str()) == "8=");
        REQUIRE(TAG_8.size() == 2);

        REQUIRE(std::string_view(TAG_9.c_str()) == "9=");
        REQUIRE(std::string_view(TAG_10.c_str()) == "10=");
        REQUIRE(TAG_10.size() == 3);

        REQUIRE(std::string_view(TAG_35.c_str()) == "35=");
        REQUIRE(std::string_view(TAG_49.c_str()) == "49=");
        REQUIRE(std::string_view(TAG_56.c_str()) == "56=");
        REQUIRE(std::string_view(TAG_108.c_str()) == "108=");
        REQUIRE(TAG_108.size() == 4);
    }

    SECTION("custom tag") {
        constexpr TagString<553> tag_username;
        REQUIRE(std::string_view(tag_username.c_str()) == "553=");
    }
}

// ============================================================================
// FastIntSerializer Tests
// ============================================================================

TEST_CASE("FastIntSerializer", "[serializer][regression]") {
    char buf[16];

    SECTION("serialize single digit") {
        size_t len = FastIntSerializer<10>::serialize(buf, 0);
        REQUIRE(len == 1);
        REQUIRE(buf[0] == '0');
    }

    SECTION("serialize multi-digit") {
        size_t len = FastIntSerializer<10>::serialize(buf, 12345);
        REQUIRE(len == 5);
        REQUIRE(std::string_view(buf, len) == "12345");
    }

    SECTION("serialize_fixed with leading zeros") {
        FastIntSerializer<3>::serialize_fixed(buf, 7);
        REQUIRE(std::string_view(buf, 3) == "007");

        FastIntSerializer<3>::serialize_fixed(buf, 42);
        REQUIRE(std::string_view(buf, 3) == "042");

        FastIntSerializer<3>::serialize_fixed(buf, 255);
        REQUIRE(std::string_view(buf, 3) == "255");
    }
}

// ============================================================================
// FastMessageBuilder Tests
// ============================================================================

TEST_CASE("FastMessageBuilder field types", "[serializer][regression]") {
    FastMessageBuilder<512> builder;

    SECTION("string field") {
        builder.field<35>(std::string_view{"D"});
        auto data = builder.view();
        std::string msg(data.data(), data.size());
        // Should contain "35=D\x01"
        REQUIRE(msg.find("35=D") != std::string::npos);
        REQUIRE(msg.back() == SOH);
    }

    SECTION("integer field") {
        builder.field<34>(uint32_t{42});
        auto data = builder.view();
        std::string msg(data.data(), data.size());
        REQUIRE(msg.find("34=42") != std::string::npos);
    }

    SECTION("char field") {
        builder.field<35>('A');
        auto data = builder.view();
        std::string msg(data.data(), data.size());
        REQUIRE(msg.find("35=A") != std::string::npos);
    }

    SECTION("bool field") {
        builder.field<141>(true);
        auto data = builder.view();
        std::string msg(data.data(), data.size());
        REQUIRE(msg.find("141=Y") != std::string::npos);

        FastMessageBuilder<512> builder2;
        builder2.field<141>(false);
        auto data2 = builder2.view();
        std::string msg2(data2.data(), data2.size());
        REQUIRE(msg2.find("141=N") != std::string::npos);
    }

    SECTION("named helpers") {
        builder.begin_string(std::string_view{"FIX.4.4"});
        builder.msg_type('A');
        builder.sender_comp_id(std::string_view{"SENDER"});
        builder.target_comp_id(std::string_view{"TARGET"});
        builder.msg_seq_num(1);
        builder.sending_time(std::string_view{"20240101-12:00:00.000"});

        auto data = builder.view();
        std::string msg(data.data(), data.size());
        REQUIRE(msg.find("8=FIX.4.4") != std::string::npos);
        REQUIRE(msg.find("35=A") != std::string::npos);
        REQUIRE(msg.find("49=SENDER") != std::string::npos);
        REQUIRE(msg.find("56=TARGET") != std::string::npos);
        REQUIRE(msg.find("34=1") != std::string::npos);
    }

    SECTION("reset clears builder") {
        builder.field<35>("D");
        REQUIRE(builder.size() > 0);
        builder.reset();
        REQUIRE(builder.size() == 0);
    }
}

TEST_CASE("FastMessageBuilder body length and checksum", "[serializer][regression]") {
    FastMessageBuilder<512> builder;

    builder.begin_string(std::string_view{"FIX.4.4"});
    size_t bl_pos = builder.body_length_placeholder();
    builder.mark_body_start();
    builder.msg_type('0');
    builder.sender_comp_id(std::string_view{"S"});
    builder.target_comp_id(std::string_view{"T"});
    builder.msg_seq_num(1);
    builder.sending_time(std::string_view{"20240101-00:00:00.000"});

    size_t body_len = builder.size() - builder.body_start();
    builder.update_body_length(bl_pos, body_len);
    builder.finalize_checksum();

    std::string msg(builder.view().data(), builder.view().size());

    // Message should contain checksum tag
    REQUIRE(msg.find("10=") != std::string::npos);

    // Find "10=" position
    auto cksum_pos = msg.find("10=");
    REQUIRE(cksum_pos != std::string::npos);

    uint32_t sum = 0;
    for (size_t i = 0; i < cksum_pos; ++i) {
        sum += static_cast<uint8_t>(msg[i]);
    }
    uint8_t expected_cksum = static_cast<uint8_t>(sum % 256);

    // Parse the checksum value from the message
    auto cksum_str = msg.substr(cksum_pos + 3, 3);
    int parsed_cksum = (cksum_str[0] - '0') * 100 + (cksum_str[1] - '0') * 10 + (cksum_str[2] - '0');
    REQUIRE(parsed_cksum == expected_cksum);
}

// ============================================================================
// LogonBuilder Tests
// ============================================================================

TEST_CASE("LogonBuilder", "[serializer][regression]") {
    LogonBuilder<512> builder;
    builder.encrypt_method(0);
    builder.heartbt_int(30);
    builder.reset_seq_num_flag(true);
    builder.username(std::string_view{"user1"});
    builder.password(std::string_view{"pass1"});

    std::string msg(builder.view().data(), builder.view().size());
    REQUIRE(msg.find("98=0") != std::string::npos);
    REQUIRE(msg.find("108=30") != std::string::npos);
    REQUIRE(msg.find("141=Y") != std::string::npos);
    REQUIRE(msg.find("553=user1") != std::string::npos);
    REQUIRE(msg.find("554=pass1") != std::string::npos);
}

// ============================================================================
// HeartbeatBuilder Tests
// ============================================================================

TEST_CASE("HeartbeatBuilder", "[serializer][regression]") {
    HeartbeatBuilder<256> builder;
    builder.test_req_id(std::string_view{"TEST001"});

    std::string msg(builder.view().data(), builder.view().size());
    REQUIRE(msg.find("112=TEST001") != std::string::npos);
}

// ============================================================================
// TestRequestBuilder Tests
// ============================================================================

TEST_CASE("TestRequestBuilder", "[serializer][regression]") {
    TestRequestBuilder<256> builder;
    builder.test_req_id(std::string_view{"REQ_001"});

    std::string msg(builder.view().data(), builder.view().size());
    REQUIRE(msg.find("112=REQ_001") != std::string::npos);
}

// ============================================================================
// MessageFactory Tests
// ============================================================================

TEST_CASE("MessageFactory complete messages", "[serializer][regression]") {
    MessageFactory<4096> factory("FIX.4.4", "SENDER", "TARGET");

    SECTION("build_heartbeat") {
        auto msg = factory.build_heartbeat(1, "20240101-00:00:00.000");
        std::string_view sv(msg.data(), msg.size());

        REQUIRE(sv.find("8=FIX.4.4") != std::string_view::npos);
        REQUIRE(sv.find("35=0") != std::string_view::npos);
        REQUIRE(sv.find("49=SENDER") != std::string_view::npos);
        REQUIRE(sv.find("56=TARGET") != std::string_view::npos);
        REQUIRE(sv.find("34=1") != std::string_view::npos);
        REQUIRE(sv.find("10=") != std::string_view::npos);
    }

    SECTION("build_heartbeat with test_req_id") {
        auto msg = factory.build_heartbeat(2, "20240101-00:00:00.000", "TESTREQ1");
        std::string_view sv(msg.data(), msg.size());
        REQUIRE(sv.find("112=TESTREQ1") != std::string_view::npos);
    }

    SECTION("build_logon") {
        auto msg = factory.build_logon(1, "20240101-00:00:00.000", 30, 0, true);
        std::string_view sv(msg.data(), msg.size());

        REQUIRE(sv.find("35=A") != std::string_view::npos);
        REQUIRE(sv.find("98=0") != std::string_view::npos);
        REQUIRE(sv.find("108=30") != std::string_view::npos);
        REQUIRE(sv.find("141=Y") != std::string_view::npos);
        REQUIRE(sv.find("10=") != std::string_view::npos);
    }

    SECTION("build_logon without reset") {
        auto msg = factory.build_logon(1, "20240101-00:00:00.000", 30);
        std::string_view sv(msg.data(), msg.size());
        REQUIRE(sv.find("141=") == std::string_view::npos);
    }

    SECTION("build_test_request") {
        auto msg = factory.build_test_request(3, "20240101-00:00:00.000", "TR_001");
        std::string_view sv(msg.data(), msg.size());

        REQUIRE(sv.find("35=1") != std::string_view::npos);
        REQUIRE(sv.find("112=TR_001") != std::string_view::npos);
    }

    SECTION("build_logout") {
        auto msg = factory.build_logout(5, "20240101-00:00:00.000", "Normal shutdown");
        std::string_view sv(msg.data(), msg.size());

        REQUIRE(sv.find("35=5") != std::string_view::npos);
        REQUIRE(sv.find("58=Normal shutdown") != std::string_view::npos);
    }

    SECTION("build_logout without text") {
        auto msg = factory.build_logout(5, "20240101-00:00:00.000");
        std::string_view sv(msg.data(), msg.size());
        REQUIRE(sv.find("58=") == std::string_view::npos);
    }
}

// ============================================================================
// FastMessageBuilder Overflow / Truncation Tests (TICKET_471_1)
// ============================================================================

TEST_CASE("FastMessageBuilder overflow sets truncated flag", "[serializer][truncation][regression]") {
    SECTION("default constructed is not truncated") {
        FastMessageBuilder<64> builder;
        REQUIRE_FALSE(builder.truncated());
    }

    SECTION("normal usage does not truncate") {
        FastMessageBuilder<512> builder;
        builder.field<35>('A');
        builder.field<49>(std::string_view{"SENDER"});
        REQUIRE_FALSE(builder.truncated());
    }

    SECTION("write_raw truncation sets flag") {
        // Use a tiny buffer. "35=D\x01" = 5 bytes.
        FastMessageBuilder<4> builder;
        builder.field<35>(std::string_view{"D"});
        // "35=" is 3 bytes, "D" is 1 byte (fits exactly 4), SOH overflows
        REQUIRE(builder.truncated());
    }

    SECTION("write_soh truncation sets flag") {
        // "8=" is 2 bytes, "X" is 1 byte = 3 bytes, then SOH needs byte 4
        FastMessageBuilder<3> builder;
        builder.field<8>(std::string_view{"X"});
        // write_raw("8=", 2) ok, write_raw("X", 1) ok, write_soh() overflows
        REQUIRE(builder.truncated());
        REQUIRE(builder.size() == 3);
    }

    SECTION("char field overflow sets flag") {
        // "35=" is 3 bytes, char 'A' needs 1, SOH needs 1 = 5 total
        FastMessageBuilder<4> builder;
        builder.field<35>('A');
        REQUIRE(builder.truncated());
    }

    SECTION("exactly at MaxSize does not truncate") {
        // "8=X\x01" = 4 bytes exactly
        FastMessageBuilder<4> builder;
        builder.field<8>(std::string_view{"X"});
        REQUIRE(builder.size() == 4);
        REQUIRE_FALSE(builder.truncated());
    }

    SECTION("one byte over MaxSize sets truncation") {
        // "8=XY\x01" = 5 bytes, buffer is 4
        FastMessageBuilder<4> builder;
        builder.field<8>(std::string_view{"XY"});
        REQUIRE(builder.truncated());
        REQUIRE(builder.size() == 4);
    }

    SECTION("size is capped at MaxSize") {
        FastMessageBuilder<8> builder;
        // Write far more than 8 bytes
        std::string big(100, 'Z');
        builder.field<8>(big);
        REQUIRE(builder.size() <= 8);
        REQUIRE(builder.truncated());
    }

    SECTION("reset clears truncated flag") {
        FastMessageBuilder<4> builder;
        builder.field<8>(std::string_view{"XY"});
        REQUIRE(builder.truncated());

        builder.reset();
        REQUIRE_FALSE(builder.truncated());
        REQUIRE(builder.size() == 0);
    }

    SECTION("truncated flag persists across subsequent writes") {
        FastMessageBuilder<4> builder;
        builder.field<8>(std::string_view{"XY"});
        REQUIRE(builder.truncated());

        // Another write still truncated (buffer full)
        builder.field<9>(std::string_view{"1"});
        REQUIRE(builder.truncated());
    }

    SECTION("recovery after reset") {
        FastMessageBuilder<64> builder;

        // First: trigger truncation with tiny effective usage
        // Reuse same builder but with MaxSize=64, fill it up
        std::string big(100, 'A');
        builder.field<58>(big);
        REQUIRE(builder.truncated());

        // Reset and build a normal message
        builder.reset();
        REQUIRE_FALSE(builder.truncated());
        builder.field<35>('0');
        REQUIRE_FALSE(builder.truncated());
    }
}

TEST_CASE("FastMessageBuilder finalize_checksum bounds safety", "[serializer][truncation][regression]") {
    SECTION("finalize_checksum on nearly full buffer sets truncated") {
        // finalize_checksum writes "10=" (3 bytes) + 3 digit chars + SOH = 7 bytes
        // Fill buffer so there is not enough room for checksum
        FastMessageBuilder<16> builder;
        // "8=ABCDEFGHIJK\x01" = 2 + 11 + 1 = 14 bytes, leaving 2 for checksum (need 7)
        builder.field<8>(std::string_view{"ABCDEFGHIJK"});
        REQUIRE_FALSE(builder.truncated());
        REQUIRE(builder.size() == 14);

        builder.finalize_checksum();
        REQUIRE(builder.truncated());
        // Size should be capped at MaxSize
        REQUIRE(builder.size() <= 16);
    }

    SECTION("finalize_checksum with enough room does not truncate") {
        FastMessageBuilder<512> builder;
        builder.field<35>('0');
        REQUIRE_FALSE(builder.truncated());

        builder.finalize_checksum();
        REQUIRE_FALSE(builder.truncated());
    }
}

TEST_CASE("FastMessageBuilder update_body_length bounds safety", "[serializer][truncation][regression]") {
    SECTION("update_body_length skips write when placeholder was truncated") {
        // Buffer of 8 bytes. "9=" (2) + "000000" (6) + SOH (1) = 9 bytes needed.
        // body_length_placeholder will truncate the placeholder.
        FastMessageBuilder<8> builder;
        size_t bl_pos = builder.body_length_placeholder();
        REQUIRE(builder.truncated());

        // Capture buffer content before update
        auto before = builder.view();
        std::string snap_before(before.data(), before.size());

        // update_body_length must not write out of bounds
        // bl_pos points into a partially written region; pos+6 > MaxSize
        builder.update_body_length(bl_pos, 42);

        // If bl_pos + 6 > MaxSize, the write is skipped entirely
        if (bl_pos + 6 > 8) {
            auto after = builder.view();
            std::string snap_after(after.data(), after.size());
            REQUIRE(snap_before == snap_after);
        }
    }

    SECTION("update_body_length works when placeholder fits") {
        FastMessageBuilder<512> builder;
        builder.begin_string(std::string_view{"FIX.4.4"});
        size_t bl_pos = builder.body_length_placeholder();
        REQUIRE_FALSE(builder.truncated());

        builder.update_body_length(bl_pos, 123);
        auto sv = builder.view();
        std::string msg(sv.data(), sv.size());
        // Should contain "000123" at the placeholder position
        REQUIRE(msg.find("000123") != std::string::npos);
    }
}
