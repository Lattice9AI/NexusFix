// test_branch_coverage_499_sweep.cpp
//
// TICKET_499 WI3 (batch 8): a garbled-input rejection sweep across the message
// types whose from_buffer failure arms are otherwise untested, plus round-trips
// for FIXT 1.1 Logon (rich optional arms: reset-seq, username, password) and
// FIX 4.3 OrderCancelRequest and the FIX 4.4 Heartbeat optional TestReqID.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/messages/fix42/new_order_single.hpp"
#include "nexusfix/messages/fix43/new_order_single.hpp"
#include "nexusfix/messages/fix43/order_cancel_request.hpp"
#include "nexusfix/messages/fix43/execution_report.hpp"
#include "nexusfix/messages/fix44/heartbeat.hpp"
#include "nexusfix/messages/fix50/new_order_single.hpp"
#include "nexusfix/messages/fixt11/logon.hpp"

using namespace nfx;

namespace {
constexpr std::string_view kGarbage = "definitely not a fix message, plain prose";
std::span<const char> garbage() {
    return std::span<const char>{kGarbage.data(), kGarbage.size()};
}
std::string_view wire(std::span<const char> s) {
    return std::string_view{s.data(), s.size()};
}
}  // namespace

TEST_CASE("from_buffer garbled-input sweep across families", "[messages][error][branch499][regression]") {
    REQUIRE_FALSE(fix42::NewOrderSingle::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fix43::NewOrderSingle::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fix43::OrderCancelRequest::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fix43::ExecutionReport::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fix44::Heartbeat::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fix50::NewOrderSingle::from_buffer(garbage()).has_value());
    REQUIRE_FALSE(fixt11::Logon::from_buffer(garbage()).has_value());
}

TEST_CASE("FIX44 Heartbeat optional TestReqID round-trip", "[messages][fix44][heartbeat][branch499][regression]") {
    SECTION("plain heartbeat omits TestReqID") {
        MessageAssembler asm_;
        auto raw = fix44::Heartbeat::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .build(asm_);
        REQUIRE(wire(raw).find("112=") == std::string_view::npos);
        REQUIRE(fix44::Heartbeat::from_buffer(raw).has_value());
    }
    SECTION("responding heartbeat carries TestReqID") {
        MessageAssembler asm_;
        auto raw = fix44::Heartbeat::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(2)
            .sending_time("20260717-10:00:00")
            .test_req_id("TR9")
            .build(asm_);
        REQUIRE(wire(raw).find("112=TR9") != std::string_view::npos);
    }
}

TEST_CASE("FIXT11 Logon builder optional arms round-trip", "[messages][fixt11][logon][branch499][regression]") {
    SECTION("all optionals: reset-seq, username, password") {
        MessageAssembler asm_;
        auto raw = fixt11::Logon::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .encrypt_method(0).heart_bt_int(30)
            .use_fix50_sp2()
            .reset_seq_num_flag(true)          // reset-seq arm
            .username("trader1")               // username arm
            .password("secret")                // password arm
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("141=Y") != std::string_view::npos);   // ResetSeqNumFlag
        REQUIRE(s.find("553=trader1") != std::string_view::npos);
        REQUIRE(s.find("554=secret") != std::string_view::npos);
        auto r = fixt11::Logon::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->heart_bt_int == 30);
    }
    SECTION("no optionals: reset-seq false, no credentials") {
        MessageAssembler asm_;
        auto raw = fixt11::Logon::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .encrypt_method(0).heart_bt_int(30)
            .use_fix50()
            .reset_seq_num_flag(false)
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("553=") == std::string_view::npos);  // no Username
        REQUIRE(s.find("554=") == std::string_view::npos);  // no Password
        REQUIRE(fixt11::Logon::from_buffer(raw).has_value());
    }
}

TEST_CASE("FIX43 OrderCancelRequest round-trip and error ladder", "[messages][fix43][ocr][branch499][regression]") {
    SECTION("valid round-trip") {
        MessageAssembler asm_;
        auto raw = fix43::OrderCancelRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .orig_cl_ord_id("O1").cl_ord_id("C1").symbol("AAPL")
            .side(Side::Buy).transact_time("20260717-10:00:00")
            .build(asm_);
        auto r = fix43::OrderCancelRequest::from_buffer(raw);
        REQUIRE(r.has_value());
        REQUIRE(r->orig_cl_ord_id == "O1");
        REQUIRE(r->cl_ord_id == "C1");
    }
    SECTION("missing OrigClOrdID rejected") {
        MessageAssembler asm_;
        auto raw = fix43::OrderCancelRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .cl_ord_id("C1").symbol("AAPL")   // no orig_cl_ord_id
            .side(Side::Buy).transact_time("20260717-10:00:00")
            .build(asm_);
        auto r = fix43::OrderCancelRequest::from_buffer(raw);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().tag == tag::OrigClOrdID::value);
    }
}
