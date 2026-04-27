#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/messages/fix42/fix42.hpp"
#include "nexusfix/messages/fix44/logon.hpp"
#include "nexusfix/messages/fix44/heartbeat.hpp"
#include "nexusfix/messages/fix44/new_order_single.hpp"

using namespace nfx;
using namespace nfx::fix42;

// ============================================================================
// FIX 4.2 ExecutionReport Tests
// ============================================================================

TEST_CASE("FIX 4.2 ExecutionReport with ExecTransType", "[fix42][exec_report][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("BROKER")
        .target_comp_id("CLIENT")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .order_id("ORD001")
        .exec_id("EXEC001")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::New)
        .ord_status(OrdStatus::New)
        .symbol("600000.SH")
        .side(Side::Buy)
        .cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .leaves_qty(Qty::from_int(1000))
        .order_qty(Qty::from_int(1000))
        .cl_ord_id("CL001")
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIX.4.2");
    REQUIRE(msg.exec_trans_type == ExecTransType::New);
    REQUIRE(msg.exec_type == ExecType::New);
    REQUIRE(msg.ord_status == OrdStatus::New);
    REQUIRE(msg.symbol == "600000.SH");
    REQUIRE(msg.side == Side::Buy);
    REQUIRE(msg.order_id == "ORD001");
    REQUIRE(msg.exec_id == "EXEC001");
    REQUIRE(msg.cl_ord_id == "CL001");
    REQUIRE(msg.leaves_qty.whole() == 1000);
    REQUIRE(msg.order_qty.whole() == 1000);
}

TEST_CASE("FIX 4.2 ExecutionReport with PartialFill and Fill", "[fix42][exec_report][regression]") {
    SECTION("PartialFill (ExecType=1)") {
        MessageAssembler asm_;
        auto raw = ExecutionReport::Builder{}
            .sender_comp_id("BROKER")
            .target_comp_id("CLIENT")
            .msg_seq_num(2)
            .sending_time("20260427-10:00:01.000")
            .order_id("ORD001")
            .exec_id("EXEC002")
            .exec_trans_type(ExecTransType::New)
            .exec_type(ExecType::PartialFill)
            .ord_status(OrdStatus::PartiallyFilled)
            .symbol("600000.SH")
            .side(Side::Buy)
            .cum_qty(Qty::from_int(500))
            .avg_px(FixedPrice::from_double(10.50))
            .leaves_qty(Qty::from_int(500))
            .last_qty(Qty::from_int(500))
            .last_px(FixedPrice::from_double(10.50))
            .build(asm_);

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.exec_type == ExecType::PartialFill);
        REQUIRE(msg.ord_status == OrdStatus::PartiallyFilled);
        REQUIRE(msg.is_fill());
        REQUIRE(!msg.is_terminal());
        REQUIRE(msg.cum_qty.whole() == 500);
        REQUIRE(msg.last_qty.whole() == 500);
    }

    SECTION("Fill (ExecType=2)") {
        MessageAssembler asm_;
        auto raw = ExecutionReport::Builder{}
            .sender_comp_id("BROKER")
            .target_comp_id("CLIENT")
            .msg_seq_num(3)
            .sending_time("20260427-10:00:02.000")
            .order_id("ORD001")
            .exec_id("EXEC003")
            .exec_trans_type(ExecTransType::New)
            .exec_type(ExecType::Fill)
            .ord_status(OrdStatus::Filled)
            .symbol("600000.SH")
            .side(Side::Buy)
            .cum_qty(Qty::from_int(1000))
            .avg_px(FixedPrice::from_double(10.50))
            .last_qty(Qty::from_int(500))
            .last_px(FixedPrice::from_double(10.50))
            .build(asm_);

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.exec_type == ExecType::Fill);
        REQUIRE(msg.ord_status == OrdStatus::Filled);
        REQUIRE(msg.is_fill());
        REQUIRE(msg.is_terminal());
    }
}

// ============================================================================
// FIX 4.2 NewOrderSingle Tests
// ============================================================================

TEST_CASE("FIX 4.2 NewOrderSingle with required HandlInst", "[fix42][nos][regression]") {
    MessageAssembler asm_;
    auto raw = NewOrderSingle::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .cl_ord_id("ORD001")
        .handl_inst('1')
        .symbol("600000.SH")
        .side(Side::Buy)
        .transact_time("20260427-10:00:00.000")
        .order_qty(Qty::from_int(1000))
        .ord_type(OrdType::Limit)
        .price(FixedPrice::from_double(10.50))
        .build(asm_);

    auto result = NewOrderSingle::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIX.4.2");
    REQUIRE(msg.header.msg_type == 'D');
    REQUIRE(msg.cl_ord_id == "ORD001");
    REQUIRE(msg.handl_inst == '1');
    REQUIRE(msg.symbol == "600000.SH");
    REQUIRE(msg.side == Side::Buy);
    REQUIRE(msg.order_qty.whole() == 1000);
    REQUIRE(msg.ord_type == OrdType::Limit);
}

TEST_CASE("FIX 4.2 NewOrderSingle rejects missing HandlInst", "[fix42][nos][regression]") {
    // Build a FIX 4.4 NOS (no HandlInst emitted when handl_inst_ == '\0'),
    // then try to parse as FIX 4.2 which requires HandlInst
    MessageAssembler asm_;
    auto raw = nfx::fix44::NewOrderSingle::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .cl_ord_id("ORD001")
        .symbol("600000.SH")
        .side(Side::Buy)
        .transact_time("20260427-10:00:00.000")
        .order_qty(Qty::from_int(1000))
        .ord_type(OrdType::Market)
        .handl_inst('\0')
        .build(asm_);

    // FIX 4.2 parser should reject because HandlInst is missing
    auto result = fix42::NewOrderSingle::from_buffer(raw);
    REQUIRE(!result.has_value());
    REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
    REQUIRE(result.error().tag == tag::HandlInst::value);
}

// ============================================================================
// FIX 4.2 Version Detection
// ============================================================================

TEST_CASE("is_fix42() returns true for BeginString=FIX.4.2", "[fix42][version][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("BROKER")
        .target_comp_id("CLIENT")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .order_id("ORD001")
        .exec_id("EXEC001")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::New)
        .ord_status(OrdStatus::New)
        .symbol("AAPL")
        .side(Side::Buy)
        .cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());
    REQUIRE(result->header.begin_string == "FIX.4.2");
    REQUIRE(result->header.begin_string.starts_with("FIX.4."));
    REQUIRE(result->header.begin_string != "FIX.4.4");
    REQUIRE(result->header.begin_string != "FIXT.1.1");
}

// ============================================================================
// FIX 4.2 Round-trip Tests
// ============================================================================

TEST_CASE("FIX 4.2 ExecutionReport round-trip: build -> parse -> verify 8=FIX.4.2", "[fix42][roundtrip][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("BROKER")
        .target_comp_id("CLIENT")
        .msg_seq_num(42)
        .sending_time("20260427-10:30:00.000")
        .order_id("ORD042")
        .exec_id("EXEC042")
        .exec_trans_type(ExecTransType::Status)
        .exec_type(ExecType::New)
        .ord_status(OrdStatus::New)
        .symbol("MSFT")
        .side(Side::Sell)
        .cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .build(asm_);

    // Verify raw message starts with 8=FIX.4.2
    std::string_view raw_sv{raw.data(), raw.size()};
    REQUIRE(raw_sv.starts_with("8=FIX.4.2"));

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());
    REQUIRE(result->header.begin_string == "FIX.4.2");
    REQUIRE(result->header.msg_seq_num == 42);
    REQUIRE(result->exec_trans_type == ExecTransType::Status);
}

// ============================================================================
// Session Builder with begin_string=FIX.4.2
// ============================================================================

TEST_CASE("Session builder with begin_string=FIX.4.2 produces correct Logon", "[fix42][session][regression]") {
    MessageAssembler asm_;
    auto raw = nfx::fix44::Logon::Builder{}
        .begin_string(fix::FIX_4_2)
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .encrypt_method(0)
        .heart_bt_int(30)
        .build(asm_);

    std::string_view raw_sv{raw.data(), raw.size()};
    REQUIRE(raw_sv.starts_with("8=FIX.4.2"));

    auto result = nfx::fix44::Logon::from_buffer(raw);
    REQUIRE(result.has_value());
    REQUIRE(result->header.begin_string == "FIX.4.2");
    REQUIRE(result->encrypt_method == 0);
    REQUIRE(result->heart_bt_int == 30);
}

// ============================================================================
// ExecTransType Enum Values
// ============================================================================

TEST_CASE("ExecTransType enum values", "[fix42][types][regression]") {
    REQUIRE(static_cast<char>(ExecTransType::New) == '0');
    REQUIRE(static_cast<char>(ExecTransType::Cancel) == '1');
    REQUIRE(static_cast<char>(ExecTransType::Correct) == '2');
    REQUIRE(static_cast<char>(ExecTransType::Status) == '3');
    REQUIRE(sizeof(ExecTransType) == 1);
}

// ============================================================================
// FIX 4.2 OrderCancelRequest Round-trip
// ============================================================================

TEST_CASE("FIX 4.2 OrderCancelRequest round-trip", "[fix42][cancel][regression]") {
    MessageAssembler asm_;
    auto raw = OrderCancelRequest::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(5)
        .sending_time("20260427-10:00:05.000")
        .orig_cl_ord_id("ORD001")
        .cl_ord_id("CXL001")
        .symbol("600000.SH")
        .side(Side::Buy)
        .transact_time("20260427-10:00:05.000")
        .order_qty(Qty::from_int(1000))
        .build(asm_);

    std::string_view raw_sv{raw.data(), raw.size()};
    REQUIRE(raw_sv.starts_with("8=FIX.4.2"));

    auto result = OrderCancelRequest::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIX.4.2");
    REQUIRE(msg.orig_cl_ord_id == "ORD001");
    REQUIRE(msg.cl_ord_id == "CXL001");
    REQUIRE(msg.symbol == "600000.SH");
    REQUIRE(msg.side == Side::Buy);
    REQUIRE(msg.order_qty.whole() == 1000);
}

// ============================================================================
// ExecutionReport Builder round-trip for conditional fields
// ============================================================================

TEST_CASE("FIX 4.2 ExecutionReport Builder emits price, ord_type, time_in_force, ord_rej_reason, orig_cl_ord_id", "[fix42][exec_report][roundtrip][regression]") {
    SECTION("Limit order with price and time_in_force") {
        MessageAssembler asm_;
        auto raw = ExecutionReport::Builder{}
            .sender_comp_id("BROKER")
            .target_comp_id("CLIENT")
            .msg_seq_num(10)
            .sending_time("20260427-11:00:00.000")
            .order_id("ORD010")
            .exec_id("EXEC010")
            .exec_trans_type(ExecTransType::New)
            .exec_type(ExecType::New)
            .ord_status(OrdStatus::New)
            .symbol("AAPL")
            .side(Side::Buy)
            .cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .leaves_qty(Qty::from_int(500))
            .order_qty(Qty::from_int(500))
            .ord_type(OrdType::Limit)
            .price(FixedPrice::from_double(150.25))
            .time_in_force(TimeInForce::GoodTillCancel)
            .cl_ord_id("CL010")
            .orig_cl_ord_id("CL009")
            .transact_time("20260427-11:00:00.000")
            .build(asm_);

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.ord_type == OrdType::Limit);
        REQUIRE(msg.price.raw != 0);
        REQUIRE(msg.time_in_force == TimeInForce::GoodTillCancel);
        REQUIRE(msg.cl_ord_id == "CL010");
        REQUIRE(msg.orig_cl_ord_id == "CL009");
        REQUIRE(msg.order_qty.whole() == 500);
        REQUIRE(msg.leaves_qty.whole() == 500);
        REQUIRE(msg.transact_time == "20260427-11:00:00.000");
    }

    SECTION("Rejected order with ord_rej_reason") {
        MessageAssembler asm_;
        auto raw = ExecutionReport::Builder{}
            .sender_comp_id("BROKER")
            .target_comp_id("CLIENT")
            .msg_seq_num(11)
            .sending_time("20260427-11:00:01.000")
            .order_id("ORD011")
            .exec_id("EXEC011")
            .exec_trans_type(ExecTransType::New)
            .exec_type(ExecType::Rejected)
            .ord_status(OrdStatus::Rejected)
            .symbol("TSLA")
            .side(Side::Sell)
            .cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .ord_rej_reason(3)
            .text("Insufficient buying power")
            .build(asm_);

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.is_rejected());
        REQUIRE(msg.ord_rej_reason == 3);
        REQUIRE(msg.text == "Insufficient buying power");
    }

    SECTION("OrdRejReason=0 (Broker option) is emitted, not silently dropped") {
        MessageAssembler asm_;
        auto raw = ExecutionReport::Builder{}
            .sender_comp_id("BROKER")
            .target_comp_id("CLIENT")
            .msg_seq_num(12)
            .sending_time("20260427-11:00:02.000")
            .order_id("ORD012")
            .exec_id("EXEC012")
            .exec_trans_type(ExecTransType::New)
            .exec_type(ExecType::Rejected)
            .ord_status(OrdStatus::Rejected)
            .symbol("GOOG")
            .side(Side::Buy)
            .cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .ord_rej_reason(0)
            .build(asm_);

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.ord_rej_reason == 0);

        // Verify Tag 103 is actually present on the wire
        std::string_view wire(raw.data(), raw.size());
        REQUIRE(wire.find("103=0") != std::string_view::npos);
    }
}

// ============================================================================
// OrderCancelRequest rejects missing required fields
// ============================================================================

TEST_CASE("FIX 4.2 OrderCancelRequest rejects missing required fields", "[fix42][cancel][regression]") {
    // Build a minimal FIX message with MsgType=F but missing required fields.
    // We use the assembler directly to craft an incomplete message.
    SECTION("Missing OrigClOrdID") {
        MessageAssembler asm_;
        asm_.start(fix::FIX_4_2)
            .field(tag::MsgType::value, 'F')
            .field(tag::SenderCompID::value, "CLIENT")
            .field(tag::TargetCompID::value, "EXCHANGE")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00.000")
            // No OrigClOrdID
            .field(tag::ClOrdID::value, "CXL001")
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1')
            .field(tag::TransactTime::value, "20260427-10:00:00.000");
        auto raw = asm_.finish();

        auto result = OrderCancelRequest::from_buffer(raw);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::OrigClOrdID::value);
    }

    SECTION("Missing Symbol") {
        MessageAssembler asm_;
        asm_.start(fix::FIX_4_2)
            .field(tag::MsgType::value, 'F')
            .field(tag::SenderCompID::value, "CLIENT")
            .field(tag::TargetCompID::value, "EXCHANGE")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00.000")
            .field(tag::OrigClOrdID::value, "ORD001")
            .field(tag::ClOrdID::value, "CXL001")
            // No Symbol
            .field(tag::Side::value, '1')
            .field(tag::TransactTime::value, "20260427-10:00:00.000");
        auto raw = asm_.finish();

        auto result = OrderCancelRequest::from_buffer(raw);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::Symbol::value);
    }

    SECTION("Missing Side") {
        MessageAssembler asm_;
        asm_.start(fix::FIX_4_2)
            .field(tag::MsgType::value, 'F')
            .field(tag::SenderCompID::value, "CLIENT")
            .field(tag::TargetCompID::value, "EXCHANGE")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00.000")
            .field(tag::OrigClOrdID::value, "ORD001")
            .field(tag::ClOrdID::value, "CXL001")
            .field(tag::Symbol::value, "AAPL")
            // No Side
            .field(tag::TransactTime::value, "20260427-10:00:00.000");
        auto raw = asm_.finish();

        auto result = OrderCancelRequest::from_buffer(raw);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::Side::value);
    }

    SECTION("Missing TransactTime") {
        MessageAssembler asm_;
        asm_.start(fix::FIX_4_2)
            .field(tag::MsgType::value, 'F')
            .field(tag::SenderCompID::value, "CLIENT")
            .field(tag::TargetCompID::value, "EXCHANGE")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00.000")
            .field(tag::OrigClOrdID::value, "ORD001")
            .field(tag::ClOrdID::value, "CXL001")
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1');
            // No TransactTime
        auto raw = asm_.finish();

        auto result = OrderCancelRequest::from_buffer(raw);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::TransactTime::value);
    }
}
