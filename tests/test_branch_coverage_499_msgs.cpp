// test_branch_coverage_499_msgs.cpp
//
// TICKET_499 WI3 (batch 2): close real control-flow branches in the message
// families and the fixed-point parse paths. The bulk of the still-open branches
// live in the less-exercised FIX 4.4 message types (OrderCancelReplaceRequest,
// OrderStatusRequest) and in the FixedPrice/Qty::from_string ladders that run on
// untrusted counterparty bytes.
//
// Pattern: build a message with all optional fields present (true arm of every
// `if (!field.empty())`), parse it back (from_buffer success path), then build
// again with optionals absent (false arms). Missing-required inputs drive the
// from_buffer error ladder.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "nexusfix/messages/fix44/new_order_single.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/types/field_types.hpp"

using namespace nfx;

// ============================================================================
// FixedPrice / Qty from_string branch ladders (untrusted parse path)
// ============================================================================

TEST_CASE("FixedPrice::from_string branch ladder", "[types][field_types][branch499][regression]") {
    SECTION("empty string -> zero") {
        REQUIRE(FixedPrice::from_string("").raw == 0);
    }
    SECTION("negative value") {
        REQUIRE(FixedPrice::from_string("-12.5").to_double() < 0.0);
    }
    SECTION("plain integer, no fraction") {
        REQUIRE(FixedPrice::from_string("150").to_double() == 150.0);
    }
    SECTION("fraction within precision") {
        REQUIRE(FixedPrice::from_string("1.25").to_double() == 1.25);
    }
    SECTION("fraction beyond DECIMAL_PLACES is clamped") {
        // More than 8 fractional digits: the extra digits hit the
        // fractional_digits < DECIMAL_PLACES false arm and are dropped.
        auto v = FixedPrice::from_string("1.1234567890123");
        REQUIRE(v.to_double() > 1.12);
        REQUIRE(v.to_double() < 1.13);
    }
    SECTION("non-digit terminates parsing") {
        REQUIRE(FixedPrice::from_string("12x34").to_double() == 12.0);
    }
    SECTION("integer overflow guard breaks early") {
        // 20+ nines exceeds the headroom guard; parser stops rather than UB.
        auto v = FixedPrice::from_string("999999999999999999999999");
        REQUIRE(v.raw > 0);  // produced a bounded value, did not wrap negative
    }
}

TEST_CASE("Qty::from_string branch ladder", "[types][field_types][branch499][regression]") {
    REQUIRE(Qty::from_string("").raw == 0);
    REQUIRE(Qty::from_string("-100").whole() == -100);
    REQUIRE(Qty::from_string("100").whole() == 100);
    REQUIRE(Qty::from_string("100.5").raw != 0);
    // fraction past DECIMAL_PLACES (Qty scale 10^4) clamps
    REQUIRE(Qty::from_string("1.123456").whole() == 1);
    REQUIRE(Qty::from_string("50z").whole() == 50);   // non-digit break
    REQUIRE(Qty::from_string("999999999999999999999").raw > 0);  // overflow guard
}

TEST_CASE("Side and OrdStatus predicate helpers", "[types][field_types][branch499][regression]") {
    REQUIRE(is_buy_side(Side::Buy));
    REQUIRE(is_buy_side(Side::BuyMinus));
    REQUIRE_FALSE(is_buy_side(Side::Sell));

    REQUIRE(is_sell_side(Side::Sell));
    REQUIRE(is_sell_side(Side::SellPlus));
    REQUIRE(is_sell_side(Side::SellShort));
    REQUIRE(is_sell_side(Side::SellShortExempt));
    REQUIRE_FALSE(is_sell_side(Side::Buy));

    REQUIRE(is_terminal_status(OrdStatus::Filled));
    REQUIRE(is_terminal_status(OrdStatus::Canceled));
    REQUIRE(is_terminal_status(OrdStatus::Rejected));
    REQUIRE(is_terminal_status(OrdStatus::Expired));
    REQUIRE(is_terminal_status(OrdStatus::DoneForDay));
    REQUIRE_FALSE(is_terminal_status(OrdStatus::New));
}

// ============================================================================
// FIX 4.4 OrderCancelReplaceRequest (MsgType = G)
// ============================================================================

namespace {
// Build a valid OCRR with every optional set; the caller parses it back.
std::span<const char> build_full_ocrr(MessageAssembler& asm_) {
    return fix44::OrderCancelReplaceRequest::Builder{}
        .sender_comp_id("CLIENT").target_comp_id("SERVER").msg_seq_num(2)
        .sending_time("20260717-10:00:00")
        .orig_cl_ord_id("ORIG1").cl_ord_id("NEW1").symbol("AAPL")
        .side(Side::Buy).transact_time("20260717-10:00:00")
        .order_qty(Qty::from_int(100)).ord_type(OrdType::StopLimit)
        .order_id("OID1")
        .price(FixedPrice::from_double(150.0))
        .stop_px(FixedPrice::from_double(149.0))
        .time_in_force(TimeInForce::GoodTillCancel)
        .account("ACCT1").handl_inst('1')
        .ex_destination("NASDAQ").text("replace")
        .build(asm_);
}
}  // namespace

TEST_CASE("FIX44 OrderCancelReplaceRequest round-trip with all optionals", "[messages][fix44][ocrr][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = build_full_ocrr(asm_);
    auto r = fix44::OrderCancelReplaceRequest::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE(r->orig_cl_ord_id == "ORIG1");
    REQUIRE(r->cl_ord_id == "NEW1");
    REQUIRE(r->order_id == "OID1");
    REQUIRE(r->account == "ACCT1");
    REQUIRE(r->ex_destination == "NASDAQ");
    REQUIRE(r->text == "replace");
    REQUIRE(r->price.raw != 0);
    REQUIRE(r->stop_px.raw != 0);
    REQUIRE(r->is_limit());
    REQUIRE(r->is_stop());
}

TEST_CASE("FIX44 OrderCancelReplaceRequest round-trip without optionals", "[messages][fix44][ocrr][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fix44::OrderCancelReplaceRequest::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00")
        .orig_cl_ord_id("O1").cl_ord_id("N1").symbol("MSFT")
        .side(Side::Sell).transact_time("20260717-10:00:00")
        .order_qty(Qty::from_int(10)).ord_type(OrdType::Market)
        .handl_inst('\0')
        .build(asm_);
    auto r = fix44::OrderCancelReplaceRequest::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE(r->is_market());
    REQUIRE(r->order_id.empty());
    REQUIRE(r->account.empty());
    REQUIRE(r->ex_destination.empty());
    REQUIRE(r->text.empty());
}

TEST_CASE("FIX44 OrderCancelReplaceRequest from_buffer error ladder", "[messages][fix44][ocrr][error][branch499][regression]") {
    // Helper: build then strip one required tag by rebuilding without it.
    SECTION("wrong MsgType rejected") {
        MessageAssembler asm_;
        auto raw = fix44::NewOrderSingle::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("X").symbol("AAPL").side(Side::Buy)
            .transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(1)).ord_type(OrdType::Market)
            .build(asm_);
        auto r = fix44::OrderCancelReplaceRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidMsgType);
    }

    SECTION("limit order missing Price is rejected") {
        MessageAssembler asm_;
        auto raw = fix44::OrderCancelReplaceRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .orig_cl_ord_id("O1").cl_ord_id("N1").symbol("AAPL")
            .side(Side::Buy).transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(10)).ord_type(OrdType::Limit)
            // no price -> is_limit() && price==0
            .build(asm_);
        auto r = fix44::OrderCancelReplaceRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().tag == tag::Price::value);
    }

    SECTION("stop order missing StopPx is rejected") {
        MessageAssembler asm_;
        auto raw = fix44::OrderCancelReplaceRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .orig_cl_ord_id("O1").cl_ord_id("N1").symbol("AAPL")
            .side(Side::Buy).transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(10)).ord_type(OrdType::Stop)
            // Stop needs stop_px; leave it unset
            .build(asm_);
        auto r = fix44::OrderCancelReplaceRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().tag == tag::StopPx::value);
    }
}

// ============================================================================
// FIX 4.4 OrderStatusRequest (MsgType = H)
// ============================================================================

TEST_CASE("FIX44 OrderStatusRequest round-trip", "[messages][fix44][osr][branch499][regression]") {
    SECTION("with optional OrderID and Account") {
        MessageAssembler asm_;
        auto raw = fix44::OrderStatusRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("CL1").symbol("AAPL").side(Side::Buy)
            .order_id("OID9").account("ACC9")
            .build(asm_);
        auto r = fix44::OrderStatusRequest::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->order_id == "OID9");
        REQUIRE(r->account == "ACC9");
    }
    SECTION("without optionals") {
        MessageAssembler asm_;
        auto raw = fix44::OrderStatusRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("CL2").symbol("MSFT").side(Side::Sell)
            .build(asm_);
        auto r = fix44::OrderStatusRequest::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->order_id.empty());
        REQUIRE(r->account.empty());
    }
}

TEST_CASE("FIX44 OrderStatusRequest from_buffer error ladder", "[messages][fix44][osr][error][branch499][regression]") {
    // Missing ClOrdID: build with everything except cl_ord_id.
    SECTION("missing ClOrdID") {
        MessageAssembler asm_;
        auto raw = fix44::OrderStatusRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .symbol("AAPL").side(Side::Buy)  // no cl_ord_id
            .build(asm_);
        auto r = fix44::OrderStatusRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().tag == tag::ClOrdID::value);
    }
    SECTION("missing Symbol") {
        MessageAssembler asm_;
        auto raw = fix44::OrderStatusRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("CL1").side(Side::Buy)  // no symbol
            .build(asm_);
        auto r = fix44::OrderStatusRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().tag == tag::Symbol::value);
    }
    SECTION("wrong MsgType") {
        MessageAssembler asm_;
        auto raw = fix44::OrderStatusRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("CL1").symbol("AAPL").side(Side::Buy)
            .build(asm_);
        // Parse an OSR buffer as OCR -> InvalidMsgType
        auto r = fix44::OrderCancelRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidMsgType);
    }
}
