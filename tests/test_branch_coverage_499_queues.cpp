// test_branch_coverage_499_queues.cpp
//
// TICKET_499 WI3 (batch 10): SPSC/MPSC queue full/empty arms and the FIX 5.0
// ExecutionReport builder path. Queues have a full-arm (try_push fails) and an
// empty-arm (try_pop returns nullopt/false) that steady-state throughput tests
// skip; the fix50 ExecutionReport (with its per-message ApplVerID) is otherwise
// untested.

#include <catch2/catch_test_macros.hpp>

#include "nexusfix/memory/spsc_queue.hpp"
#include "nexusfix/memory/mpsc_queue.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/messages/fix50/execution_report.hpp"

using namespace nfx;
using namespace nfx::memory;

namespace {
std::string_view wire(std::span<const char> s) {
    return std::string_view{s.data(), s.size()};
}
}  // namespace

TEST_CASE("SPSCQueue full and empty arms", "[memory][spsc][branch499][regression]") {
    // Capacity 2 reserves one slot for full detection -> exactly 1 usable.
    SPSCQueue<int, 2> q;
    REQUIRE(q.empty());

    int out = 0;
    REQUIRE_FALSE(q.try_pop(out));       // empty-arm of try_pop(T&)
    REQUIRE_FALSE(q.try_pop().has_value());  // empty-arm of try_pop()

    REQUIRE(q.try_push(1));
    REQUIRE(q.full());
    REQUIRE_FALSE(q.try_push(2));         // full-arm of try_push(const&)
    int moved = 3;
    REQUIRE_FALSE(q.try_push(std::move(moved)));  // full-arm of try_push(&&)

    REQUIRE(q.try_pop(out));
    REQUIRE(out == 1);
    REQUIRE(q.empty());
}

TEST_CASE("MPSCQueue full and empty arms", "[memory][mpsc][branch499][regression]") {
    MPSCQueue<int, 2> q;
    REQUIRE(q.empty());

    int out = 0;
    REQUIRE_FALSE(q.try_pop(out));            // empty arm
    REQUIRE_FALSE(q.try_pop().has_value());

    REQUIRE(q.try_push(10));
    // A 2-slot MPSC queue holds 2 items; fill it then overflow.
    REQUIRE(q.try_push(20));
    REQUIRE_FALSE(q.try_push(30));            // full arm

    REQUIRE(q.try_pop(out));
    REQUIRE(out == 10);
}

TEST_CASE("FIX50 ExecutionReport round-trip", "[messages][fix50][exec_report][branch499][regression]") {
    SECTION("with ApplVerID and optionals") {
        MessageAssembler asm_;
        auto raw = fix50::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .use_fix50_sp2()
            .order_id("OID1").exec_id("EX1")
            .exec_type(ExecType::Fill).ord_status(OrdStatus::Filled)
            .symbol("AAPL").side(Side::Buy)
            .leaves_qty(Qty::from_int(0)).cum_qty(Qty::from_int(100))
            .avg_px(FixedPrice::from_double(150.0))
            .cl_ord_id("CL1").order_qty(Qty::from_int(100))
            .ord_type(OrdType::Limit).price(FixedPrice::from_double(150.0))
            .time_in_force(TimeInForce::Day)
            .last_px(FixedPrice::from_double(150.0)).last_qty(Qty::from_int(100))
            .transact_time("20260717-10:00:00").account("ACC1").text("done")
            .build(asm_);
        REQUIRE(wire(raw).find("1128=") != std::string_view::npos);
        auto r = fix50::ExecutionReport::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->order_id == "OID1");
        REQUIRE(r->account == "ACC1");
    }
    SECTION("minimal, no ApplVerID or optionals") {
        MessageAssembler asm_;
        auto raw = fix50::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .order_id("OID2").exec_id("EX2")
            .exec_type(ExecType::New).ord_status(OrdStatus::New)
            .symbol("MSFT").side(Side::Sell)
            .leaves_qty(Qty::from_int(50)).cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .build(asm_);
        REQUIRE(wire(raw).find("1128=") == std::string_view::npos);
        auto r = fix50::ExecutionReport::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->account.empty());
    }
    SECTION("wrong MsgType rejected") {
        MessageAssembler asm_;
        auto raw = fix50::ExecutionReport::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .order_id("OID3").exec_id("EX3")
            .exec_type(ExecType::New).ord_status(OrdStatus::New)
            .symbol("MSFT").side(Side::Sell)
            .leaves_qty(Qty::from_int(1)).cum_qty(Qty::from_int(0))
            .avg_px(FixedPrice::from_double(0.0))
            .build(asm_);
        auto r = fix50::OrderCancelReject::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ParseErrorCode::InvalidMsgType);
    }
}
