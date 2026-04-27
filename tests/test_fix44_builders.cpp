#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

#include "nexusfix/messages/fix44/execution_report.hpp"
#include "nexusfix/messages/fix44/new_order_single.hpp"
#include "nexusfix/messages/fix44/market_data.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/parser/runtime_parser.hpp"

using namespace nfx;
using namespace nfx::fix44;

// ============================================================================
// FIX 4.4 ExecutionReport Builder (TICKET_479 Phase 5C)
// ============================================================================

TEST_CASE("ExecutionReport Builder sets all required fields",
          "[messages][fix44][builder][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("EXCHANGE")
        .target_comp_id("CLIENT")
        .msg_seq_num(42)
        .sending_time("20260427-09:30:00.123")
        .order_id("ORD789")
        .exec_id("EX456")
        .exec_type(ExecType::Fill)
        .ord_status(OrdStatus::Filled)
        .symbol("MSFT")
        .side(Side::Sell)
        .leaves_qty(Qty::from_int(0))
        .cum_qty(Qty::from_int(200))
        .avg_px(FixedPrice::from_double(425.75))
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.header.begin_string == "FIX.4.4");
    REQUIRE(msg.header.msg_type == '8');
    REQUIRE(msg.header.sender_comp_id == "EXCHANGE");
    REQUIRE(msg.header.target_comp_id == "CLIENT");
    REQUIRE(msg.header.msg_seq_num == 42);
    REQUIRE(msg.order_id == "ORD789");
    REQUIRE(msg.exec_id == "EX456");
    REQUIRE(msg.exec_type == ExecType::Fill);
    REQUIRE(msg.ord_status == OrdStatus::Filled);
    REQUIRE(msg.symbol == "MSFT");
    REQUIRE(msg.side == Side::Sell);
    REQUIRE(msg.leaves_qty.whole() == 0);
    REQUIRE(msg.cum_qty.whole() == 200);
    REQUIRE(msg.is_fill());
    REQUIRE(msg.is_terminal());
}

TEST_CASE("ExecutionReport Builder rejects missing required fields on parse",
          "[messages][fix44][builder][regression]") {
    SECTION("Missing OrderID") {
        // Manually build a message missing OrderID (tag 37)
        MessageAssembler asm_;
        asm_.start()
            .field(tag::MsgType::value, '8')
            .field(tag::SenderCompID::value, "S")
            .field(tag::TargetCompID::value, "T")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00")
            // No OrderID (tag 37)
            .field(tag::ExecID::value, "EX1")
            .field(tag::ExecType::value, '0')
            .field(tag::OrdStatus::value, '0')
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1')
            .field(tag::LeavesQty::value, static_cast<int64_t>(100))
            .field(tag::CumQty::value, static_cast<int64_t>(0))
            .field(tag::AvgPx::value, FixedPrice::from_double(0.0));
        auto raw = asm_.finish();

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::OrderID::value);
    }

    SECTION("Missing ExecType") {
        MessageAssembler asm_;
        asm_.start()
            .field(tag::MsgType::value, '8')
            .field(tag::SenderCompID::value, "S")
            .field(tag::TargetCompID::value, "T")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00")
            .field(tag::OrderID::value, "ORD1")
            .field(tag::ExecID::value, "EX1")
            // No ExecType (tag 150)
            .field(tag::OrdStatus::value, '0')
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1')
            .field(tag::LeavesQty::value, static_cast<int64_t>(100))
            .field(tag::CumQty::value, static_cast<int64_t>(0))
            .field(tag::AvgPx::value, FixedPrice::from_double(0.0));
        auto raw = asm_.finish();

        auto result = ExecutionReport::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::ExecType::value);
    }
}

TEST_CASE("ExecutionReport Builder round-trip with optional fields",
          "[messages][fix44][builder][regression]") {
    MessageAssembler asm_;
    auto raw = ExecutionReport::Builder{}
        .sender_comp_id("BROKER")
        .target_comp_id("FUND")
        .msg_seq_num(7)
        .sending_time("20260427-14:00:00.000")
        .order_id("ORD100")
        .exec_id("EX200")
        .exec_type(ExecType::PartialFill)
        .ord_status(OrdStatus::PartiallyFilled)
        .symbol("TSLA")
        .side(Side::Buy)
        .leaves_qty(Qty::from_int(50))
        .cum_qty(Qty::from_int(50))
        .avg_px(FixedPrice::from_double(180.25))
        .cl_ord_id("CL100")
        .order_qty(Qty::from_int(100))
        .last_px(FixedPrice::from_double(180.25))
        .last_qty(Qty::from_int(50))
        .text("Partial fill on NYSE")
        .transact_time("20260427-14:00:00.001")
        .account("ACCT001")
        .build(asm_);

    auto result = ExecutionReport::from_buffer(raw);
    REQUIRE(result.has_value());

    auto& msg = *result;
    REQUIRE(msg.cl_ord_id == "CL100");
    REQUIRE(msg.order_qty.whole() == 100);
    REQUIRE(msg.last_qty.whole() == 50);
    REQUIRE(msg.text == "Partial fill on NYSE");
    REQUIRE(msg.transact_time == "20260427-14:00:00.001");
    REQUIRE(msg.account == "ACCT001");
    REQUIRE(msg.is_fill());
    REQUIRE_FALSE(msg.is_terminal());
}

// ============================================================================
// FIX 4.4 NewOrderSingle Builder (TICKET_479 Phase 5C)
// ============================================================================

TEST_CASE("NewOrderSingle Builder required field validation on parse",
          "[messages][fix44][builder][regression]") {
    SECTION("Missing ClOrdID") {
        MessageAssembler asm_;
        asm_.start()
            .field(tag::MsgType::value, 'D')
            .field(tag::SenderCompID::value, "S")
            .field(tag::TargetCompID::value, "T")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00")
            // No ClOrdID (tag 11)
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1')
            .field(tag::TransactTime::value, "20260427-10:00:00")
            .field(tag::OrderQty::value, static_cast<int64_t>(100))
            .field(tag::OrdType::value, '2')
            .field(tag::Price::value, FixedPrice::from_double(150.0));
        auto raw = asm_.finish();

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::ClOrdID::value);
    }

    SECTION("Limit order missing Price") {
        MessageAssembler asm_;
        asm_.start()
            .field(tag::MsgType::value, 'D')
            .field(tag::SenderCompID::value, "S")
            .field(tag::TargetCompID::value, "T")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20260427-10:00:00")
            .field(tag::ClOrdID::value, "CL001")
            .field(tag::Symbol::value, "AAPL")
            .field(tag::Side::value, '1')
            .field(tag::TransactTime::value, "20260427-10:00:00")
            .field(tag::OrderQty::value, static_cast<int64_t>(100))
            .field(tag::OrdType::value, '2')  // Limit
            .field(tag::TimeInForce::value, '0');
            // No Price (tag 44) - required for Limit orders
        auto raw = asm_.finish();

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ParseErrorCode::MissingRequiredField);
        REQUIRE(result.error().tag == tag::Price::value);
    }
}

TEST_CASE("NewOrderSingle Builder round-trip",
          "[messages][fix44][builder][regression]") {
    SECTION("Limit order") {
        MessageAssembler asm_;
        auto raw = NewOrderSingle::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("BROKER")
            .msg_seq_num(1)
            .sending_time("20260427-09:30:00.000")
            .cl_ord_id("CL001")
            .symbol("AAPL")
            .side(Side::Buy)
            .transact_time("20260427-09:30:00.000")
            .order_qty(Qty::from_int(100))
            .ord_type(OrdType::Limit)
            .price(FixedPrice::from_double(150.50))
            .time_in_force(TimeInForce::Day)
            .account("ACCT1")
            .build(asm_);

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.header.begin_string == "FIX.4.4");
        REQUIRE(msg.header.msg_type == 'D');
        REQUIRE(msg.cl_ord_id == "CL001");
        REQUIRE(msg.symbol == "AAPL");
        REQUIRE(msg.side == Side::Buy);
        REQUIRE(msg.order_qty.whole() == 100);
        REQUIRE(msg.ord_type == OrdType::Limit);
        REQUIRE(msg.is_limit());
        REQUIRE_FALSE(msg.is_market());
        REQUIRE(msg.account == "ACCT1");
    }

    SECTION("Market order") {
        MessageAssembler asm_;
        auto raw = NewOrderSingle::Builder{}
            .sender_comp_id("CLIENT")
            .target_comp_id("BROKER")
            .msg_seq_num(2)
            .sending_time("20260427-09:30:01.000")
            .cl_ord_id("CL002")
            .symbol("MSFT")
            .side(Side::Sell)
            .transact_time("20260427-09:30:01.000")
            .order_qty(Qty::from_int(50))
            .ord_type(OrdType::Market)
            .build(asm_);

        auto result = NewOrderSingle::from_buffer(raw);
        REQUIRE(result.has_value());

        auto& msg = *result;
        REQUIRE(msg.side == Side::Sell);
        REQUIRE(msg.is_market());
        REQUIRE_FALSE(msg.is_limit());
        REQUIRE(msg.order_qty.whole() == 50);
    }
}

TEST_CASE("MarketDataRequest Builder produces parseable message",
          "[messages][fix44][builder][regression]") {
    MessageAssembler asm_;
    auto raw = MarketDataRequest::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260427-09:00:00.000")
        .md_req_id("REQ001")
        .subscription_type(SubscriptionRequestType::SnapshotPlusUpdates)
        .market_depth(1)
        .add_entry_type(MDEntryType::Bid)
        .add_entry_type(MDEntryType::Offer)
        .add_symbol("AAPL")
        .build(asm_);

    // Parse with IndexedParser to verify structure
    auto parsed = IndexedParser::parse(raw);
    REQUIRE(parsed.has_value());

    auto& p = *parsed;
    REQUIRE(p.msg_type() == 'V');
    REQUIRE(p.sender_comp_id() == "CLIENT");
    REQUIRE(p.target_comp_id() == "EXCHANGE");
    REQUIRE(p.get_string(tag::MDReqID::value) == "REQ001");
    REQUIRE(p.get_char(tag::SubscriptionRequestType::value) ==
            static_cast<char>(SubscriptionRequestType::SnapshotPlusUpdates));
}
