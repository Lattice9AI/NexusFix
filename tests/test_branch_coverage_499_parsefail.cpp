// test_branch_coverage_499_parsefail.cpp
//
// TICKET_499 WI3 (batch 6): the from_buffer failure arms that the happy-path
// message tests skip. Every message type's from_buffer starts with two guards:
// the IndexedParser failing on garbled bytes (!parsed.has_value()) and a
// MsgType mismatch. Plus the FIX 4.4 OrderCancelRequest round-trip (its Side
// conditional and order_qty/order_id optional emission are otherwise untested).

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/messages/fix44/new_order_single.hpp"

using namespace nfx;

namespace {
// Bytes that the parser cannot index into a valid message: no SOH structure,
// no BeginString, no BodyLength. IndexedParser::parse returns an error, so
// from_buffer takes its !parsed.has_value() arm.
constexpr std::string_view kGarbage = "not a fix message at all, just text";

std::string_view wire(std::span<const char> s) {
    return std::string_view{s.data(), s.size()};
}
}  // namespace

TEST_CASE("FIX44 from_buffer rejects garbled input", "[messages][fix44][error][branch499][regression]") {
    auto garbage = std::span<const char>{kGarbage.data(), kGarbage.size()};

    SECTION("NewOrderSingle") {
        auto r = fix44::NewOrderSingle::from_buffer(garbage);
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("OrderCancelRequest") {
        auto r = fix44::OrderCancelRequest::from_buffer(garbage);
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("OrderCancelReplaceRequest") {
        auto r = fix44::OrderCancelReplaceRequest::from_buffer(garbage);
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("OrderStatusRequest") {
        auto r = fix44::OrderStatusRequest::from_buffer(garbage);
        REQUIRE_FALSE(r.has_value());
    }
}

TEST_CASE("FIX44 OrderCancelRequest round-trip", "[messages][fix44][ocr][branch499][regression]") {
    SECTION("with optional order_qty and order_id") {
        MessageAssembler asm_;
        auto raw = fix44::OrderCancelRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .orig_cl_ord_id("ORIG1").cl_ord_id("CANCEL1").symbol("AAPL")
            .side(Side::Buy).transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(100))   // order_qty > 0 arm
            .order_id("OID1")                 // !order_id.empty() arm
            .build(asm_);
        auto r = fix44::OrderCancelRequest::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->orig_cl_ord_id == "ORIG1");
        REQUIRE(r->cl_ord_id == "CANCEL1");
        REQUIRE(r->order_id == "OID1");
        REQUIRE(r->side == Side::Buy);        // Side-present conditional taken
        auto s = wire(raw);
        REQUIRE(s.find("38=100") != std::string_view::npos);
    }
    SECTION("without optionals (order_qty zero, order_id empty)") {
        MessageAssembler asm_;
        auto raw = fix44::OrderCancelRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .orig_cl_ord_id("ORIG2").cl_ord_id("CANCEL2").symbol("MSFT")
            .side(Side::Sell).transact_time("20260717-10:00:00")
            .build(asm_);
        auto r = fix44::OrderCancelRequest::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->order_id.empty());
        auto s = wire(raw);
        REQUIRE(s.find("38=") == std::string_view::npos);  // OrderQty omitted
    }
    SECTION("wrong MsgType is rejected") {
        MessageAssembler asm_;
        auto raw = fix44::NewOrderSingle::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("X").symbol("AAPL").side(Side::Buy)
            .transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(1)).ord_type(OrdType::Market)
            .build(asm_);
        auto r = fix44::OrderCancelRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidMsgType);
    }
}
