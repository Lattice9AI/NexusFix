#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/messages/fixt11/fixt11.hpp"
#include "nexusfix/messages/fix44/logon.hpp"

using namespace nfx;
using namespace nfx::fixt11;

// ============================================================================
// FIXT 1.1 Logon Tests
// ============================================================================

TEST_CASE("FIXT 1.1 Logon MSG_TYPE and BEGIN_STRING", "[fixt11][logon][regression]") {
    REQUIRE(Logon::MSG_TYPE == 'A');
    REQUIRE(Logon::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIXT 1.1 Logon build and parse roundtrip", "[fixt11][logon][regression]") {
    SECTION("Required fields with DefaultApplVerID") {
        MessageAssembler asm_;
        auto raw = Logon::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(1)
            .sending_time("20260122-14:30:00.000")
            .encrypt_method(0)
            .heart_bt_int(30)
            .use_fix50()
            .build(asm_);

        auto result = Logon::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.header.begin_string == "FIXT.1.1");
        REQUIRE(msg.header.msg_type == 'A');
        REQUIRE(msg.header.sender_comp_id == "CLIENT");
        REQUIRE(msg.header.target_comp_id == "SERVER");
        REQUIRE(msg.encrypt_method == 0);
        REQUIRE(msg.heart_bt_int == 30);
        REQUIRE(msg.default_appl_ver_id == appl_ver_id::FIX_5_0);
        REQUIRE_FALSE(msg.reset_seq_num_flag);
    }

    SECTION("With credentials and ResetSeqNumFlag") {
        MessageAssembler asm_;
        auto raw = Logon::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(1)
            .sending_time("20260122-14:30:00.000")
            .encrypt_method(0)
            .heart_bt_int(60)
            .use_fix50_sp2()
            .reset_seq_num_flag(true)
            .username("trader1")
            .password("secret")
            .build(asm_);

        auto result = Logon::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.heart_bt_int == 60);
        REQUIRE(msg.default_appl_ver_id == appl_ver_id::FIX_5_0_SP2);
        REQUIRE(msg.reset_seq_num_flag);
        REQUIRE(msg.username == "trader1");
        REQUIRE(msg.password == "secret");
    }
}

TEST_CASE("FIXT 1.1 Logon appl_ver_string", "[fixt11][logon][regression]") {
    Logon msg;
    msg.default_appl_ver_id = appl_ver_id::FIX_5_0;
    REQUIRE_FALSE(msg.appl_ver_string().empty());
}

TEST_CASE("FIXT 1.1 Logon rejects non-FIXT BeginString", "[fixt11][logon][regression]") {
    // Build a FIX 4.4 Logon and try to parse as FIXT 1.1 Logon
    MessageAssembler asm_;
    auto raw = nfx::fix44::Logon::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("SERVER")
        .msg_seq_num(1)
        .sending_time("20260122-14:30:00.000")
        .encrypt_method(0)
        .heart_bt_int(30)
        .build(asm_);

    auto result = Logon::from_buffer(raw);
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// FIXT 1.1 Logout Tests
// ============================================================================

TEST_CASE("FIXT 1.1 Logout MSG_TYPE", "[fixt11][logout][regression]") {
    REQUIRE(Logout::MSG_TYPE == '5');
    REQUIRE(Logout::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIXT 1.1 Logout build and parse roundtrip", "[fixt11][logout][regression]") {
    SECTION("Without text") {
        MessageAssembler asm_;
        auto raw = Logout::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(5)
            .sending_time("20260122-14:31:00.000")
            .build(asm_);

        auto result = Logout::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.begin_string == "FIXT.1.1");
        REQUIRE(result->text.empty());
    }

    SECTION("With text") {
        MessageAssembler asm_;
        auto raw = Logout::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(5)
            .sending_time("20260122-14:31:00.000")
            .text("Session ended normally")
            .build(asm_);

        auto result = Logout::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->text == "Session ended normally");
    }
}

// ============================================================================
// FIXT 1.1 Heartbeat Tests
// ============================================================================

TEST_CASE("FIXT 1.1 Heartbeat MSG_TYPE", "[fixt11][heartbeat][regression]") {
    REQUIRE(Heartbeat::MSG_TYPE == '0');
    REQUIRE(Heartbeat::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIXT 1.1 Heartbeat build and parse roundtrip", "[fixt11][heartbeat][regression]") {
    SECTION("Without TestReqID") {
        MessageAssembler asm_;
        auto raw = Heartbeat::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(10)
            .sending_time("20260122-14:30:30.000")
            .build(asm_);

        auto result = Heartbeat::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.begin_string == "FIXT.1.1");
        REQUIRE(result->test_req_id.empty());
    }

    SECTION("With TestReqID") {
        MessageAssembler asm_;
        auto raw = Heartbeat::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(10)
            .sending_time("20260122-14:30:30.000")
            .test_req_id("REQ123")
            .build(asm_);

        auto result = Heartbeat::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->test_req_id == "REQ123");
    }
}

// ============================================================================
// FIXT 1.1 TestRequest Tests
// ============================================================================

TEST_CASE("FIXT 1.1 TestRequest MSG_TYPE", "[fixt11][testrequest][regression]") {
    REQUIRE(TestRequest::MSG_TYPE == '1');
}

TEST_CASE("FIXT 1.1 TestRequest build and parse roundtrip", "[fixt11][testrequest][regression]") {
    MessageAssembler asm_;
    auto raw = TestRequest::Builder{}
        .sender_comp_id("SERVER")
        .target_comp_id("CLIENT")
        .msg_seq_num(8)
        .sending_time("20260122-14:30:45.000")
        .test_req_id("TREQ001")
        .build(asm_);

    auto result = TestRequest::from_buffer(raw);
    REQUIRE(result.has_value());
    REQUIRE(result->header.begin_string == "FIXT.1.1");
    REQUIRE(result->test_req_id == "TREQ001");
}

// ============================================================================
// FIXT 1.1 ResendRequest Tests
// ============================================================================

TEST_CASE("FIXT 1.1 ResendRequest MSG_TYPE", "[fixt11][resend][regression]") {
    REQUIRE(ResendRequest::MSG_TYPE == '2');
}

TEST_CASE("FIXT 1.1 ResendRequest build and parse roundtrip", "[fixt11][resend][regression]") {
    SECTION("Specific range") {
        MessageAssembler asm_;
        auto raw = ResendRequest::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(3)
            .sending_time("20260122-14:31:00.000")
            .begin_seq_no(5)
            .end_seq_no(10)
            .build(asm_);

        auto result = ResendRequest::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->begin_seq_no == 5);
        REQUIRE(result->end_seq_no == 10);
    }

    SECTION("Open-ended (end_seq_no = 0)") {
        MessageAssembler asm_;
        auto raw = ResendRequest::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(3)
            .sending_time("20260122-14:31:00.000")
            .begin_seq_no(5)
            .end_seq_no(0)
            .build(asm_);

        auto result = ResendRequest::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->begin_seq_no == 5);
        REQUIRE(result->end_seq_no == 0);
    }
}

// ============================================================================
// FIXT 1.1 SequenceReset Tests
// ============================================================================

TEST_CASE("FIXT 1.1 SequenceReset MSG_TYPE", "[fixt11][seqreset][regression]") {
    REQUIRE(SequenceReset::MSG_TYPE == '4');
}

TEST_CASE("FIXT 1.1 SequenceReset build and parse roundtrip", "[fixt11][seqreset][regression]") {
    SECTION("Reset mode") {
        MessageAssembler asm_;
        auto raw = SequenceReset::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(1)
            .sending_time("20260122-14:31:05.000")
            .new_seq_no(100)
            .build(asm_);

        auto result = SequenceReset::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->new_seq_no == 100);
        REQUIRE_FALSE(result->gap_fill_flag);
    }

    SECTION("GapFill mode") {
        MessageAssembler asm_;
        auto raw = SequenceReset::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(5)
            .sending_time("20260122-14:31:05.000")
            .new_seq_no(10)
            .gap_fill_flag(true)
            .build(asm_);

        auto result = SequenceReset::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->new_seq_no == 10);
        REQUIRE(result->gap_fill_flag);
    }
}

// ============================================================================
// FIXT 1.1 Reject Tests
// ============================================================================

TEST_CASE("FIXT 1.1 Reject MSG_TYPE", "[fixt11][reject][regression]") {
    REQUIRE(Reject::MSG_TYPE == '3');
}

TEST_CASE("FIXT 1.1 Reject build and parse roundtrip", "[fixt11][reject][regression]") {
    MessageAssembler asm_;
    auto raw = Reject::Builder{}
        .sender_comp_id("SERVER")
        .target_comp_id("CLIENT")
        .msg_seq_num(4)
        .sending_time("20260122-14:31:10.000")
        .ref_seq_num(3)
        .ref_tag_id(55)
        .session_reject_reason(1)
        .text("Required tag missing")
        .build(asm_);

    auto result = Reject::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.ref_seq_num == 3);
    REQUIRE(msg.ref_tag_id == 55);
    REQUIRE(msg.session_reject_reason == 1);
    REQUIRE(msg.text == "Required tag missing");
}
