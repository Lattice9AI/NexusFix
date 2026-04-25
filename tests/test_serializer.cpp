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
