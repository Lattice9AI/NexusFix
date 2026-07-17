// test_branch_coverage_499_er.cpp
//
// TICKET_499 WI3 (batch 14): FIX 4.4 and 4.2 ExecutionReport builder arms and
// status predicates (is_fill / is_terminal / is_rejected). Each predicate is a
// short-circuit chain whose alternate arms the happy-path tests skip.

#include <catch2/catch_test_macros.hpp>

#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/messages/fix44/execution_report.hpp"
#include "nexusfix/messages/fix42/execution_report.hpp"

using namespace nfx;

TEST_CASE("FIX44 ExecutionReport round-trip and predicates", "[messages][fix44][exec_report][branch499][regression]") {
    SECTION("filled with all optionals") {
        MessageAssembler asm_;
        auto raw = fix44::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .order_id("O1").exec_id("E1")
            .exec_type(ExecType::Fill).ord_status(OrdStatus::Filled)
            .symbol("AAPL").side(Side::Buy)
            .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(100))
            .avg_px(FixedPrice::from_double(150.0))
            .cl_ord_id("CL1").order_qty(Qty::from_int(100))
            .ord_type(OrdType::Limit).price(FixedPrice::from_double(150.0))
            .time_in_force(TimeInForce::Day)
            .last_px(FixedPrice::from_double(150.0)).last_qty(Qty::from_int(100))
            .transact_time("20260717-10:00:00").account("A1").text("ok")
            .build(asm_);
        auto r = fix44::ExecutionReport::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->is_fill());
        REQUIRE(r->is_terminal());
        REQUIRE_FALSE(r->is_rejected());
        REQUIRE(r->account == "A1");
        REQUIRE(r->text == "ok");
    }
    SECTION("partial fill via Trade exec type") {
        MessageAssembler asm_;
        auto raw = fix44::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .order_id("O2").exec_id("E2")
            .exec_type(ExecType::Trade).ord_status(OrdStatus::PartiallyFilled)
            .symbol("MSFT").side(Side::Sell)
            .leaves_qty(Qty::from_int(50)).cum_qty(Qty::from_int(50))
            .avg_px(FixedPrice::from_double(100.0))
            .build(asm_);
        auto r = fix44::ExecutionReport::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->is_fill());          // Trade counts as a fill
        REQUIRE_FALSE(r->is_terminal()); // PartiallyFilled is not terminal
    }
    SECTION("rejected via ord_status") {
        MessageAssembler asm_;
        auto raw = fix44::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .order_id("O3").exec_id("E3")
            .exec_type(ExecType::New).ord_status(OrdStatus::Rejected)
            .symbol("AAPL").side(Side::Buy)
            .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .build(asm_);
        auto r = fix44::ExecutionReport::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->is_rejected());       // via ord_status == Rejected arm
        REQUIRE_FALSE(r->is_fill());
    }
}

TEST_CASE("FIX42 ExecutionReport round-trip", "[messages][fix42][exec_report][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fix42::ExecutionReport::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00")
        .order_id("O1").exec_id("E1")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::Fill).ord_status(OrdStatus::Filled)
        .symbol("AAPL").side(Side::Buy)
        .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(100))
        .avg_px(FixedPrice::from_double(150.0))
        .build(asm_);
    auto r = fix42::ExecutionReport::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE(r->order_id == "O1");
    REQUIRE(r->exec_id == "E1");
}
