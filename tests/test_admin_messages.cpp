#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/messages/fix44/logon.hpp"
#include "nexusfix/messages/fix44/heartbeat.hpp"
#include "nexusfix/messages/common/trailer.hpp"

using namespace nfx;
using namespace nfx::fix44;

// ============================================================================
// Logon Tests
// ============================================================================

TEST_CASE("Logon MSG_TYPE", "[admin][logon][regression]") {
    REQUIRE(Logon::MSG_TYPE == 'A');
}

TEST_CASE("Logon build and parse roundtrip", "[admin][logon][regression]") {
    SECTION("Required fields only") {
        MessageAssembler asm_;
        auto raw = Logon::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(1)
            .sending_time("20231215-10:30:00")
            .encrypt_method(0)
            .heart_bt_int(30)
            .build(asm_);

        auto result = Logon::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.header.msg_type == 'A');
        REQUIRE(msg.header.sender_comp_id == "CLIENT");
        REQUIRE(msg.header.target_comp_id == "SERVER");
        REQUIRE(msg.header.msg_seq_num == 1);
        REQUIRE(msg.encrypt_method == 0);
        REQUIRE(msg.heart_bt_int == 30);
        REQUIRE_FALSE(msg.reset_seq_num_flag);
        REQUIRE(msg.username.empty());
        REQUIRE(msg.password.empty());
    }

    SECTION("With optional fields") {
        MessageAssembler asm_;
        auto raw = Logon::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(2)
            .sending_time("20231215-10:30:00")
            .encrypt_method(0)
            .heart_bt_int(60)
            .reset_seq_num_flag(true)
            .build(asm_);

        auto result = Logon::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.heart_bt_int == 60);
        REQUIRE(msg.reset_seq_num_flag);
    }
}

TEST_CASE("Logon parse errors", "[admin][logon][regression]") {
    SECTION("Wrong message type") {
        MessageAssembler asm_;
        auto raw = Heartbeat::Builder{}
            .sender_comp_id("C")
            .target_comp_id("S")
            .msg_seq_num(1)
            .sending_time("20231215")
            .build(asm_);

        auto result = Logon::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::InvalidMsgType);
    }

    SECTION("Missing EncryptMethod") {
        // Build a message with MsgType=A but no tag 98
        MessageAssembler asm_;
        auto raw = asm_.start()
            .field(tag::MsgType::value, 'A')
            .field(tag::SenderCompID::value, "C")
            .field(tag::TargetCompID::value, "S")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215")
            // Missing 98=EncryptMethod
            .field(108, static_cast<int64_t>(30))
            .finish();

        auto result = Logon::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
    }
}

// ============================================================================
// Logout Tests
// ============================================================================

TEST_CASE("Logout MSG_TYPE", "[admin][logout][regression]") {
    REQUIRE(Logout::MSG_TYPE == '5');
}

TEST_CASE("Logout build and parse", "[admin][logout][regression]") {
    SECTION("Without text") {
        MessageAssembler asm_;
        auto raw = Logout::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(10)
            .sending_time("20231215-10:35:00")
            .build(asm_);

        auto result = Logout::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '5');
        REQUIRE(result->header.msg_seq_num == 10);
        REQUIRE(result->text.empty());
    }

    SECTION("With text") {
        MessageAssembler asm_;
        auto raw = Logout::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(11)
            .sending_time("20231215-10:35:00")
            .text("Session ended normally")
            .build(asm_);

        auto result = Logout::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->text == "Session ended normally");
    }
}

// ============================================================================
// Heartbeat Tests
// ============================================================================

TEST_CASE("Heartbeat MSG_TYPE", "[admin][heartbeat][regression]") {
    REQUIRE(Heartbeat::MSG_TYPE == '0');
}

TEST_CASE("Heartbeat build and parse", "[admin][heartbeat][regression]") {
    SECTION("Without TestReqID") {
        MessageAssembler asm_;
        auto raw = Heartbeat::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(5)
            .sending_time("20231215-10:30:30")
            .build(asm_);

        auto result = Heartbeat::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '0');
        REQUIRE(result->header.msg_seq_num == 5);
        REQUIRE(result->test_req_id.empty());
    }

    SECTION("With TestReqID") {
        MessageAssembler asm_;
        auto raw = Heartbeat::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(6)
            .sending_time("20231215-10:31:00")
            .test_req_id("TR001")
            .build(asm_);

        auto result = Heartbeat::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->test_req_id == "TR001");
    }
}

// ============================================================================
// TestRequest Tests
// ============================================================================

TEST_CASE("TestRequest MSG_TYPE", "[admin][testrequest][regression]") {
    REQUIRE(TestRequest::MSG_TYPE == '1');
}

TEST_CASE("TestRequest build and parse", "[admin][testrequest][regression]") {
    SECTION("With TestReqID") {
        MessageAssembler asm_;
        auto raw = TestRequest::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(7)
            .sending_time("20231215-10:32:00")
            .test_req_id("TEST123")
            .build(asm_);

        auto result = TestRequest::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '1');
        REQUIRE(result->test_req_id == "TEST123");
    }

    SECTION("Missing TestReqID fails") {
        MessageAssembler asm_;
        auto raw = TestRequest::Builder{}
            .sender_comp_id("S")
            .target_comp_id("C")
            .msg_seq_num(1)
            .sending_time("20231215")
            // test_req_id left empty
            .build(asm_);

        auto result = TestRequest::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
    }
}

// ============================================================================
// ResendRequest Tests
// ============================================================================

TEST_CASE("ResendRequest MSG_TYPE", "[admin][resendrequest][regression]") {
    REQUIRE(ResendRequest::MSG_TYPE == '2');
}

TEST_CASE("ResendRequest build and parse", "[admin][resendrequest][regression]") {
    SECTION("Normal range") {
        MessageAssembler asm_;
        auto raw = ResendRequest::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(3)
            .sending_time("20231215-10:33:00")
            .begin_seq_no(5)
            .end_seq_no(10)
            .build(asm_);

        auto result = ResendRequest::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '2');
        REQUIRE(result->begin_seq_no == 5);
        REQUIRE(result->end_seq_no == 10);
    }

    SECTION("Infinity end_seq=0") {
        MessageAssembler asm_;
        auto raw = ResendRequest::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("SERVER")
            .msg_seq_num(4)
            .sending_time("20231215-10:33:00")
            .begin_seq_no(1)
            .end_seq_no(0)
            .build(asm_);

        auto result = ResendRequest::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->begin_seq_no == 1);
        REQUIRE(result->end_seq_no == 0);
    }
}

// ============================================================================
// SequenceReset Tests
// ============================================================================

TEST_CASE("SequenceReset MSG_TYPE", "[admin][sequencereset][regression]") {
    REQUIRE(SequenceReset::MSG_TYPE == '4');
}

TEST_CASE("SequenceReset build and parse", "[admin][sequencereset][regression]") {
    SECTION("Without GapFillFlag") {
        MessageAssembler asm_;
        auto raw = SequenceReset::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(1)
            .sending_time("20231215-10:34:00")
            .new_seq_no(100)
            .build(asm_);

        auto result = SequenceReset::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '4');
        REQUIRE(result->new_seq_no == 100);
        REQUIRE_FALSE(result->gap_fill_flag);
    }

    SECTION("With GapFillFlag") {
        MessageAssembler asm_;
        auto raw = SequenceReset::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(5)
            .sending_time("20231215-10:34:00")
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
// Reject Tests
// ============================================================================

TEST_CASE("Reject MSG_TYPE", "[admin][reject][regression]") {
    REQUIRE(Reject::MSG_TYPE == '3');
}

TEST_CASE("Reject build and parse", "[admin][reject][regression]") {
    SECTION("Required fields only") {
        MessageAssembler asm_;
        auto raw = Reject::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(8)
            .sending_time("20231215-10:35:00")
            .ref_seq_num(5)
            .build(asm_);

        auto result = Reject::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->header.msg_type == '3');
        REQUIRE(result->ref_seq_num == 5);
        REQUIRE(result->ref_tag_id == 0);
        REQUIRE(result->session_reject_reason == 0);
        REQUIRE(result->text.empty());
    }

    SECTION("All optional fields") {
        MessageAssembler asm_;
        auto raw = Reject::Builder{}
            .sender_comp_id("SERVER")
            .target_comp_id("CLIENT")
            .msg_seq_num(9)
            .sending_time("20231215-10:35:00")
            .ref_seq_num(5)
            .ref_tag_id(55)
            .session_reject_reason(1)
            .text("Required tag missing")
            .build(asm_);

        auto result = Reject::from_buffer(raw);
        REQUIRE(result.has_value());
        REQUIRE(result->ref_seq_num == 5);
        REQUIRE(result->ref_tag_id == 55);
        REQUIRE(result->session_reject_reason == 1);
        REQUIRE(result->text == "Required tag missing");
    }
}
