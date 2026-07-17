// test_branch_coverage_499_msgs2.cpp
//
// TICKET_499 WI3 (batch 5): remaining message-family builder arms and status
// predicates across FIX 4.3 ExecutionReport, FIX 5.0 NewOrderSingle (the
// per-message ApplVerID path), and FIXT 1.1 session messages. Same round-trip
// discipline: all optionals present (true arms) then absent (false arms).

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/messages/fix43/execution_report.hpp"
#include "nexusfix/messages/fix50/new_order_single.hpp"
#include "nexusfix/messages/fixt11/session.hpp"

using namespace nfx;

namespace {
std::string_view wire(std::span<const char> s) {
    return std::string_view{s.data(), s.size()};
}
}  // namespace

// ============================================================================
// FIX 4.3 ExecutionReport
// ============================================================================

TEST_CASE("FIX43 ExecutionReport builder all optionals + predicates", "[messages][fix43][exec_report][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fix43::ExecutionReport::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(3)
        .sending_time("20260717-10:00:00")
        .order_id("OID1").exec_id("EX1")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::Fill).ord_status(OrdStatus::Filled)
        .symbol("AAPL").side(Side::Buy)
        .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(100))
        .avg_px(FixedPrice::from_double(150.0))
        .cl_ord_id("CL1").orig_cl_ord_id("OCL1")
        .order_qty(Qty::from_int(100)).ord_type(OrdType::Limit)
        .price(FixedPrice::from_double(150.0))
        .time_in_force(TimeInForce::Day)
        .last_px(FixedPrice::from_double(150.0)).last_qty(Qty::from_int(100))
        .transact_time("20260717-10:00:00").account("ACC1")
        .text("filled")
        .build(asm_);

    auto r = fix43::ExecutionReport::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE(r->is_fill());          // exec_type == Fill
    REQUIRE(r->is_terminal());      // ord_status Filled is terminal
    REQUIRE_FALSE(r->is_rejected());
    REQUIRE(r->cl_ord_id == "CL1");
    REQUIRE(r->account == "ACC1");
    REQUIRE(r->text == "filled");
}

TEST_CASE("FIX43 ExecutionReport builder minimal (optionals absent)", "[messages][fix43][exec_report][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fix43::ExecutionReport::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00")
        .order_id("OID2").exec_id("EX2")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::New).ord_status(OrdStatus::New)
        .symbol("MSFT").side(Side::Sell)
        .leaves_qty(Qty::from_int(50)).cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        // no cl_ord_id, orig_cl_ord_id, order_qty, ord_type, price, tif,
        // last_qty, transact_time, account, text -> all false arms
        .build(asm_);

    auto r = fix43::ExecutionReport::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->is_fill());       // New is not a fill
    REQUIRE_FALSE(r->is_terminal());   // New is not terminal
    REQUIRE(r->cl_ord_id.empty());
    REQUIRE(r->account.empty());
}

TEST_CASE("FIX43 ExecutionReport is_rejected via exec_type", "[messages][fix43][exec_report][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fix43::ExecutionReport::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00")
        .order_id("OID3").exec_id("EX3")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::Rejected).ord_status(OrdStatus::Rejected)
        .symbol("AAPL").side(Side::Buy)
        .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .build(asm_);
    auto r = fix43::ExecutionReport::from_buffer(raw);
    REQUIRE(r.has_value());
    REQUIRE(r->is_rejected());
    REQUIRE(r->is_terminal());
}

// ============================================================================
// FIX 5.0 NewOrderSingle (per-message ApplVerID)
// ============================================================================

TEST_CASE("FIX50 NewOrderSingle round-trip with ApplVerID", "[messages][fix50][nos][branch499][regression]") {
    SECTION("appl_ver_id emitted and parsed back") {
        MessageAssembler asm_;
        auto raw = fix50::NewOrderSingle::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .use_fix50_sp2()                       // include_appl_ver_ true arm
            .cl_ord_id("CL1").symbol("AAPL").side(Side::Buy)
            .transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(100)).ord_type(OrdType::StopLimit)
            .price(FixedPrice::from_double(150.0))
            .stop_px(FixedPrice::from_double(149.0))
            .time_in_force(TimeInForce::GoodTillCancel)
            .account("ACC1").handl_inst('1')
            .ex_destination("NASDAQ").text("t")
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("1128=") != std::string_view::npos);  // ApplVerID present
        auto r = fix50::NewOrderSingle::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->account == "ACC1");
        REQUIRE(r->text == "t");
    }
    SECTION("no appl_ver_id and no optionals") {
        MessageAssembler asm_;
        auto raw = fix50::NewOrderSingle::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("CL2").symbol("MSFT").side(Side::Sell)
            .transact_time("20260717-10:00:00")
            .order_qty(Qty::from_int(10)).ord_type(OrdType::Market)
            .handl_inst('\0')
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("1128=") == std::string_view::npos);  // ApplVerID absent
        auto r = fix50::NewOrderSingle::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->account.empty());
    }
}

// ============================================================================
// FIXT 1.1 session messages
// ============================================================================

TEST_CASE("FIXT11 Heartbeat round-trip", "[messages][fixt11][heartbeat][branch499][regression]") {
    SECTION("plain heartbeat (no TestReqID)") {
        MessageAssembler asm_;
        auto raw = fixt11::Heartbeat::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .build(asm_);
        auto r = fixt11::Heartbeat::from_buffer(raw);
        REQUIRE(r.has_value());
        auto s = wire(raw);
        REQUIRE(s.find("112=") == std::string_view::npos);  // no TestReqID
    }
    SECTION("heartbeat responding to a TestRequest carries TestReqID") {
        MessageAssembler asm_;
        auto raw = fixt11::Heartbeat::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(2)
            .sending_time("20260717-10:00:00")
            .test_req_id("TRQ1")            // true arm of the optional emit
            .build(asm_);
        auto r = fixt11::Heartbeat::from_buffer(raw);
        REQUIRE(r.has_value());
        auto s = wire(raw);
        REQUIRE(s.find("112=TRQ1") != std::string_view::npos);
    }
}

TEST_CASE("FIXT11 TestRequest round-trip", "[messages][fixt11][testrequest][branch499][regression]") {
    MessageAssembler asm_;
    auto raw = fixt11::TestRequest::Builder{}
        .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00")
        .test_req_id("PING-1")
        .build(asm_);
    auto r = fixt11::TestRequest::from_buffer(raw);
    REQUIRE(r.has_value());
    auto s = wire(raw);
    REQUIRE(s.find("112=PING-1") != std::string_view::npos);
}
