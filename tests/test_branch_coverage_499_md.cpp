// test_branch_coverage_499_md.cpp
//
// TICKET_499 WI3 (batch 3): market-data message builders. The MarketDataRequest
// builder has several conditional field-emission arms (MDUpdateType only when
// subscribing, aggregated-book Y/N, the two repeating groups, the add_* bounds
// guards) plus the is_snapshot/is_subscribe/is_unsubscribe predicates. The
// existing market-data tests only drive the basic subscribe path, so the false
// arms and the alternate subscription types are uncovered.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "nexusfix/messages/fix44/market_data.hpp"
#include "nexusfix/messages/common/trailer.hpp"

using namespace nfx;
using namespace nfx::fix44;

namespace {
std::string_view wire(std::span<const char> s) {
    return std::string_view{s.data(), s.size()};
}
}  // namespace

TEST_CASE("FIX44 MarketDataRequest subscription-type predicates", "[market_data][request][branch499][regression]") {
    MarketDataRequest snap;
    snap.subscription_type = SubscriptionRequestType::Snapshot;
    REQUIRE(snap.is_snapshot());
    REQUIRE_FALSE(snap.is_subscribe());
    REQUIRE_FALSE(snap.is_unsubscribe());

    MarketDataRequest sub;
    sub.subscription_type = SubscriptionRequestType::SnapshotPlusUpdates;
    REQUIRE(sub.is_subscribe());
    REQUIRE_FALSE(sub.is_snapshot());

    MarketDataRequest unsub;
    unsub.subscription_type = SubscriptionRequestType::DisablePreviousSnapshot;
    REQUIRE(unsub.is_unsubscribe());
    REQUIRE_FALSE(unsub.is_subscribe());
}

TEST_CASE("FIX44 MarketDataRequest builder conditional arms", "[market_data][request][branch499][regression]") {
    SECTION("subscribe emits MDUpdateType, entries, and symbols") {
        MessageAssembler asm_;
        auto raw = MarketDataRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .md_req_id("REQ1")
            .subscription_type(SubscriptionRequestType::SnapshotPlusUpdates)
            .market_depth(5)
            .md_update_type(MDUpdateType::IncrementalRefresh)
            .aggregated_book(true)
            .add_entry_type(MDEntryType::Bid)
            .add_entry_type(MDEntryType::Offer)
            .add_symbol("AAPL")
            .add_symbol("MSFT")
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("263=1") != std::string_view::npos);   // SubscriptionRequestType
        REQUIRE(s.find("265=") != std::string_view::npos);    // MDUpdateType emitted
        REQUIRE(s.find("267=2") != std::string_view::npos);   // NoMDEntryTypes
        REQUIRE(s.find("146=2") != std::string_view::npos);   // NoRelatedSym
        REQUIRE(s.find("266=Y") != std::string_view::npos);   // AggregatedBook Y
    }

    SECTION("snapshot request omits MDUpdateType") {
        MessageAssembler asm_;
        auto raw = MarketDataRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .md_req_id("REQ2")
            .subscription_type(SubscriptionRequestType::Snapshot)  // not SnapshotPlusUpdates
            .market_depth(1)
            .aggregated_book(false)                                 // AggregatedBook N arm
            .add_entry_type(MDEntryType::Trade)
            .add_symbol("TSLA")
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("265=") == std::string_view::npos);   // MDUpdateType NOT emitted
        REQUIRE(s.find("266=N") != std::string_view::npos);  // AggregatedBook N
    }

    SECTION("no entry types and no symbols: both repeating groups skipped") {
        MessageAssembler asm_;
        auto raw = MarketDataRequest::Builder{}
            .sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
            .sending_time("20260717-10:00:00")
            .md_req_id("REQ3")
            .subscription_type(SubscriptionRequestType::DisablePreviousSnapshot)
            .market_depth(0)
            .build(asm_);
        auto s = wire(raw);
        REQUIRE(s.find("267=") == std::string_view::npos);  // NoMDEntryTypes skipped
        REQUIRE(s.find("146=") == std::string_view::npos);  // NoRelatedSym skipped
    }
}

TEST_CASE("FIX44 MarketDataRequest builder bounds guards", "[market_data][request][branch499][regression]") {
    // add_entry_type / add_symbol both bound-check; push past the limits so the
    // false arm (drop-on-full) is taken. MAX_ENTRY_TYPES=16, MAX_SYMBOLS=64.
    MarketDataRequest::Builder b;
    b.sender_comp_id("C").target_comp_id("B").msg_seq_num(1)
        .sending_time("20260717-10:00:00").md_req_id("R")
        .subscription_type(SubscriptionRequestType::Snapshot).market_depth(1);
    for (int i = 0; i < 20; ++i) { b.add_entry_type(MDEntryType::Bid); }   // > 16
    for (int i = 0; i < 70; ++i) { b.add_symbol("SYM"); }                   // > 64
    MessageAssembler asm_;
    auto raw = b.build(asm_);
    auto s = wire(raw);
    // The count fields reflect the caps, not the attempted overflow.
    REQUIRE(s.find("267=16") != std::string_view::npos);
    REQUIRE(s.find("146=64") != std::string_view::npos);
}
