#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/messages/fix50/fix50.hpp"
#include "nexusfix/messages/fix44/logon.hpp"

using namespace nfx;
using namespace nfx::fix50;

// ============================================================================
// FIX 5.0 NewOrderSingle Tests
// ============================================================================

TEST_CASE("FIX 5.0 NewOrderSingle MSG_TYPE and BEGIN_STRING", "[fix50][nos][regression]") {
    REQUIRE(NewOrderSingle::MSG_TYPE == 'D');
    REQUIRE(NewOrderSingle::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIX 5.0 NewOrderSingle build and parse roundtrip", "[fix50][nos][regression]") {
    SECTION("Limit order with required fields") {
        MessageAssembler asm_;
        auto raw = NewOrderSingle::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("EXCHANGE")
            .msg_seq_num(1)
            .sending_time("20260122-14:30:00.000")
            .cl_ord_id("ORD001")
            .symbol("AAPL")
            .side(Side::Buy)
            .transact_time("20260122-14:30:00.000")
            .order_qty(Qty::from_int(100))
            .ord_type(OrdType::Limit)
            .price(FixedPrice::from_double(150.50))
            .build(asm_);

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.header.begin_string == "FIXT.1.1");
        REQUIRE(msg.header.msg_type == 'D');
        REQUIRE(msg.header.sender_comp_id == "CLIENT");
        REQUIRE(msg.header.target_comp_id == "EXCHANGE");
        REQUIRE(msg.cl_ord_id == "ORD001");
        REQUIRE(msg.symbol == "AAPL");
        REQUIRE(msg.side == Side::Buy);
        REQUIRE(msg.order_qty.whole() == 100);
        REQUIRE(msg.ord_type == OrdType::Limit);
    }

    SECTION("Market order") {
        MessageAssembler asm_;
        auto raw = NewOrderSingle::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("EXCHANGE")
            .msg_seq_num(2)
            .sending_time("20260122-14:30:01.000")
            .cl_ord_id("ORD002")
            .symbol("MSFT")
            .side(Side::Sell)
            .transact_time("20260122-14:30:01.000")
            .order_qty(Qty::from_int(50))
            .ord_type(OrdType::Market)
            .build(asm_);

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.side == Side::Sell);
        REQUIRE(msg.ord_type == OrdType::Market);
        REQUIRE(msg.is_market());
        REQUIRE_FALSE(msg.is_limit());
        REQUIRE_FALSE(msg.is_stop());
    }
}

TEST_CASE("FIX 5.0 NewOrderSingle with ApplVerID", "[fix50][nos][regression]") {
    MessageAssembler asm_;
    auto raw = NewOrderSingle::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260122-14:30:00.000")
        .cl_ord_id("ORD003")
        .symbol("GOOG")
        .side(Side::Buy)
        .transact_time("20260122-14:30:00.000")
        .order_qty(Qty::from_int(10))
        .ord_type(OrdType::Market)
        .use_fix50()
        .build(asm_);

    auto result = NewOrderSingle::from_buffer(raw);
    REQUIRE(result.has_value());
    REQUIRE(result->appl_ver_id == appl_ver_id::FIX_5_0);
}

TEST_CASE("FIX 5.0 NewOrderSingle convenience methods", "[fix50][nos][regression]") {
    NewOrderSingle msg;

    SECTION("is_limit") {
        msg.ord_type = OrdType::Limit;
        REQUIRE(msg.is_limit());
        REQUIRE_FALSE(msg.is_market());
        REQUIRE_FALSE(msg.is_stop());
    }

    SECTION("is_market") {
        msg.ord_type = OrdType::Market;
        REQUIRE(msg.is_market());
        REQUIRE_FALSE(msg.is_limit());
    }

    SECTION("is_stop") {
        msg.ord_type = OrdType::Stop;
        REQUIRE(msg.is_stop());
        REQUIRE_FALSE(msg.is_limit());
    }

    SECTION("is_stop_limit") {
        msg.ord_type = OrdType::StopLimit;
        REQUIRE(msg.is_stop());
        REQUIRE(msg.is_limit());
    }

    SECTION("notional") {
        msg.price = FixedPrice::from_double(100.0);
        msg.order_qty = Qty::from_int(50);
        auto notional = msg.notional();
        REQUIRE(notional.to_double() > 4999.0);
        REQUIRE(notional.to_double() < 5001.0);
    }
}

// ============================================================================
// FIX 5.0 ExecutionReport Tests
// ============================================================================

TEST_CASE("FIX 5.0 ExecutionReport MSG_TYPE", "[fix50][execrpt][regression]") {
    REQUIRE(ExecutionReport::MSG_TYPE == '8');
    REQUIRE(ExecutionReport::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIX 5.0 ExecutionReport build and parse roundtrip", "[fix50][execrpt][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("EXCHANGE")
        .target_comp_id("CLIENT")
        .msg_seq_num(1)
        .sending_time("20260122-14:30:00.100")
        .order_id("EX001")
        .exec_id("EXEC001")
        .exec_type(ExecType::New)
        .ord_status(OrdStatus::New)
        .symbol("AAPL")
        .side(Side::Buy)
        .leaves_qty(Qty::from_int(100))
        .cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .cl_ord_id("ORD001")
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIXT.1.1");
    REQUIRE(msg.order_id == "EX001");
    REQUIRE(msg.exec_id == "EXEC001");
    REQUIRE(msg.exec_type == ExecType::New);
    REQUIRE(msg.ord_status == OrdStatus::New);
    REQUIRE(msg.symbol == "AAPL");
    REQUIRE(msg.side == Side::Buy);
    REQUIRE(msg.cl_ord_id == "ORD001");
}

TEST_CASE("FIX 5.0 ExecutionReport fill roundtrip", "[fix50][execrpt][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("EXCHANGE")
        .target_comp_id("CLIENT")
        .msg_seq_num(2)
        .sending_time("20260122-14:30:00.200")
        .order_id("EX001")
        .exec_id("EXEC002")
        .exec_type(ExecType::Fill)
        .ord_status(OrdStatus::Filled)
        .symbol("AAPL")
        .side(Side::Buy)
        .leaves_qty(Qty::from_int(0))
        .cum_qty(Qty::from_int(100))
        .avg_px(FixedPrice::from_double(150.50))
        .last_px(FixedPrice::from_double(150.50))
        .last_qty(Qty::from_int(100))
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.is_fill());
    REQUIRE(msg.is_terminal());
    REQUIRE_FALSE(msg.is_rejected());
    REQUIRE(msg.last_qty.whole() == 100);
}

TEST_CASE("FIX 5.0 ExecutionReport convenience methods", "[fix50][execrpt][regression]") {
    ExecutionReport msg;

    SECTION("is_fill for Fill") {
        msg.exec_type = ExecType::Fill;
        REQUIRE(msg.is_fill());
    }

    SECTION("is_fill for PartialFill") {
        msg.exec_type = ExecType::PartialFill;
        REQUIRE(msg.is_fill());
    }

    SECTION("is_fill for Trade") {
        msg.exec_type = ExecType::Trade;
        REQUIRE(msg.is_fill());
    }

    SECTION("is_rejected") {
        msg.exec_type = ExecType::Rejected;
        REQUIRE(msg.is_rejected());
        REQUIRE_FALSE(msg.is_fill());
    }

    SECTION("fill_value") {
        msg.last_px = FixedPrice::from_double(100.0);
        msg.last_qty = Qty::from_int(50);
        auto fv = msg.fill_value();
        REQUIRE(fv.to_double() > 4999.0);
        REQUIRE(fv.to_double() < 5001.0);
    }
}

TEST_CASE("FIX 5.0 ExecutionReport missing required fields", "[fix50][execrpt][regression]") {
    // Build a Logon message and try to parse as ExecutionReport
    MessageAssembler asm_;
    auto raw = nfx::fix44::Logon::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("SERVER")
        .msg_seq_num(1)
        .sending_time("20260122-14:30:00.000")
        .encrypt_method(0)
        .heart_bt_int(30)
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// FIX 5.0 OrderCancelRequest Tests
// ============================================================================

TEST_CASE("FIX 5.0 OrderCancelRequest build and parse roundtrip", "[fix50][cancel][regression]") {
    MessageAssembler asm_;
    auto raw = OrderCancelRequest::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(3)
        .sending_time("20260122-14:31:00.000")
        .orig_cl_ord_id("ORD001")
        .cl_ord_id("CXLORD001")
        .symbol("AAPL")
        .side(Side::Buy)
        .transact_time("20260122-14:31:00.000")
        .order_qty(Qty::from_int(100))
        .build(asm_);

    auto result = OrderCancelRequest::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIXT.1.1");
    REQUIRE(msg.orig_cl_ord_id == "ORD001");
    REQUIRE(msg.cl_ord_id == "CXLORD001");
    REQUIRE(msg.symbol == "AAPL");
    REQUIRE(msg.side == Side::Buy);
}

// ============================================================================
// FIX 5.0 OrderCancelReject Tests
// ============================================================================

TEST_CASE("FIX 5.0 OrderCancelReject MSG_TYPE", "[fix50][reject][regression]") {
    REQUIRE(OrderCancelReject::MSG_TYPE == '9');
    REQUIRE(OrderCancelReject::BEGIN_STRING == "FIXT.1.1");
}

TEST_CASE("FIX 5.0 appl_ver_string", "[fix50][regression]") {
    ExecutionReport msg;
    msg.appl_ver_id = appl_ver_id::FIX_5_0;
    REQUIRE_FALSE(msg.appl_ver_string().empty());
}
