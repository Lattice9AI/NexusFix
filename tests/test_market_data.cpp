#include <catch2/catch_test_macros.hpp>
#include <string>
#include <cstring>

#include "nexusfix/messages/fix44/market_data.hpp"
#include "nexusfix/messages/common/trailer.hpp"

using namespace nfx;
using namespace nfx::fix44;

// Helper to replace | with SOH for test data
static std::string make_fix_message(std::string_view msg) {
    std::string result{msg};
    for (char& c : result) {
        if (c == '|') c = fix::SOH;
    }
    return result;
}

// ============================================================================
// MarketDataRequest Tests
// ============================================================================

TEST_CASE("MarketDataRequest Builder - Basic subscription", "[market_data][request][regression]") {
    MessageAssembler asm_;
    MarketDataRequest::Builder builder;

    auto msg = builder
        .sender_comp_id("SENDER")
        .target_comp_id("TARGET")
        .msg_seq_num(1)
        .sending_time("20260122-10:00:00.000")
        .md_req_id("MD001")
        .subscription_type(SubscriptionRequestType::SnapshotPlusUpdates)
        .market_depth(5)
        .md_update_type(MDUpdateType::IncrementalRefresh)
        .add_entry_type(MDEntryType::Bid)
        .add_entry_type(MDEntryType::Offer)
        .add_symbol("AAPL")
        .add_symbol("GOOGL")
        .build(asm_);

    std::string msg_str{msg.data(), msg.size()};

    // Verify message type
    REQUIRE(msg_str.contains("35=V"));

    // Verify MDReqID
    REQUIRE(msg_str.contains("262=MD001"));

    // Verify subscription type (1 = SnapshotPlusUpdates)
    REQUIRE(msg_str.contains("263=1"));

    // Verify market depth
    REQUIRE(msg_str.contains("264=5"));

    // Verify entry types count
    REQUIRE(msg_str.contains("267=2"));

    // Verify symbols count
    REQUIRE(msg_str.contains("146=2"));

    // Verify symbols
    REQUIRE(msg_str.contains("55=AAPL"));
    REQUIRE(msg_str.contains("55=GOOGL"));
}

TEST_CASE("MarketDataRequest Builder - Snapshot only", "[market_data][request][regression]") {
    MessageAssembler asm_;
    MarketDataRequest::Builder builder;

    auto msg = builder
        .sender_comp_id("SENDER")
        .target_comp_id("TARGET")
        .msg_seq_num(1)
        .sending_time("20260122-10:00:00.000")
        .md_req_id("SNAP001")
        .subscription_type(SubscriptionRequestType::Snapshot)
        .market_depth(0)  // Full book
        .add_entry_type(MDEntryType::Bid)
        .add_entry_type(MDEntryType::Offer)
        .add_entry_type(MDEntryType::Trade)
        .add_symbol("MSFT")
        .build(asm_);

    std::string msg_str{msg.data(), msg.size()};

    // Verify subscription type (0 = Snapshot)
    REQUIRE(msg_str.contains("263=0"));

    // Verify 3 entry types
    REQUIRE(msg_str.contains("267=3"));
}

// ============================================================================
// MarketDataSnapshotFullRefresh Tests
// ============================================================================

TEST_CASE("MarketDataSnapshotFullRefresh Parser - Basic snapshot", "[market_data][snapshot][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=155|35=W|49=SERVER|56=CLIENT|34=1|52=20260122-10:00:00.000|"
        "262=MD001|55=AAPL|268=3|"
        "269=0|270=150.25|271=1000|"
        "269=1|270=150.30|271=500|"
        "269=2|270=150.27|271=100|"
        "10=024|"
    );

    auto result = MarketDataSnapshotFullRefresh::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.symbol == "AAPL");
    REQUIRE(msg.md_req_id == "MD001");
    REQUIRE(msg.no_md_entries == 3);
    REQUIRE(msg.entry_count() == 3);
}

TEST_CASE("MarketDataSnapshotFullRefresh Parser - Iterate entries", "[market_data][snapshot][iterator][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=144|35=W|49=SERVER|56=CLIENT|34=1|52=20260122-10:00:00.000|"
        "262=MD002|55=GOOGL|268=2|"
        "269=0|270=2800.50|271=200|290=1|"
        "269=1|270=2801.00|271=150|290=1|"
        "10=060|"
    );

    auto result = MarketDataSnapshotFullRefresh::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.symbol == "GOOGL");

    auto iter = msg.entries();
    REQUIRE(iter.count() == 2);

    // First entry (Bid)
    REQUIRE(iter.has_next());
    MDEntry entry1 = iter.next();
    REQUIRE(entry1.entry_type == MDEntryType::Bid);
    REQUIRE(entry1.has_price());
    REQUIRE(entry1.has_size());

    // Second entry (Offer)
    REQUIRE(iter.has_next());
    MDEntry entry2 = iter.next();
    REQUIRE(entry2.entry_type == MDEntryType::Offer);

    // No more entries
    REQUIRE_FALSE(iter.has_next());
}

// ============================================================================
// MarketDataIncrementalRefresh Tests
// ============================================================================

TEST_CASE("MarketDataIncrementalRefresh Parser - Basic update", "[market_data][incremental][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=150|35=X|49=SERVER|56=CLIENT|34=2|52=20260122-10:00:01.000|"
        "262=MD001|268=2|"
        "279=0|269=0|55=AAPL|270=150.30|271=1200|"
        "279=1|269=1|55=AAPL|270=150.35|271=400|"
        "10=128|"
    );

    auto result = MarketDataIncrementalRefresh::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.md_req_id == "MD001");
    REQUIRE(msg.no_md_entries == 2);
    REQUIRE(msg.entry_count() == 2);
}

TEST_CASE("MarketDataIncrementalRefresh Parser - Update actions", "[market_data][incremental][actions][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=170|35=X|49=SERVER|56=CLIENT|34=3|52=20260122-10:00:02.000|"
        "268=3|"
        "279=0|269=0|55=MSFT|270=400.00|271=100|"
        "279=1|269=0|55=MSFT|270=399.95|271=150|"
        "279=2|269=1|55=MSFT|270=400.10|"
        "10=160|"
    );

    auto result = MarketDataIncrementalRefresh::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.entry_count() == 3);

    auto iter = msg.entries();

    // First: New bid
    REQUIRE(iter.has_next());
    MDEntry e1 = iter.next();
    REQUIRE(e1.update_action == MDUpdateAction::New);
    REQUIRE(e1.entry_type == MDEntryType::Bid);

    // Second: Change bid
    REQUIRE(iter.has_next());
    MDEntry e2 = iter.next();
    REQUIRE(e2.update_action == MDUpdateAction::Change);

    // Third: Delete offer
    REQUIRE(iter.has_next());
    MDEntry e3 = iter.next();
    REQUIRE(e3.update_action == MDUpdateAction::Delete);
    REQUIRE(e3.entry_type == MDEntryType::Offer);
}

// ============================================================================
// MarketDataRequestReject Tests
// ============================================================================

TEST_CASE("MarketDataRequestReject Parser - Unknown symbol", "[market_data][reject][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=91|35=Y|49=SERVER|56=CLIENT|34=1|52=20260122-10:00:00.000|"
        "262=MD001|281=0|58=Symbol not found|"
        "10=070|"
    );

    auto result = MarketDataRequestReject::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.md_req_id == "MD001");
    REQUIRE(msg.md_req_rej_reason == MDReqRejReason::UnknownSymbol);
    REQUIRE(msg.text == "Symbol not found");
    REQUIRE(msg.rejection_reason_name() == "UnknownSymbol");
}

TEST_CASE("MarketDataRequestReject Parser - Insufficient permissions", "[market_data][reject][regression]") {
    std::string raw_msg = make_fix_message(
        "8=FIX.4.4|9=105|35=Y|49=SERVER|56=CLIENT|34=2|52=20260122-10:00:00.000|"
        "262=MD002|281=2|58=Not authorized for this symbol|"
        "10=216|"
    );

    auto result = MarketDataRequestReject::from_buffer(
        std::span<const char>{raw_msg.data(), raw_msg.size()});

    REQUIRE(result.has_value());

    auto& msg = *result;

    REQUIRE(msg.md_req_rej_reason == MDReqRejReason::InsufficientPermissions);
    REQUIRE(msg.rejection_reason_name() == "InsufficientPermissions");
}

// ============================================================================
// Market Data Types Tests
// ============================================================================

TEST_CASE("MDEntryType - Name conversion", "[market_data][types][regression]") {
    REQUIRE(md_entry_type_name(MDEntryType::Bid) == "Bid");
    REQUIRE(md_entry_type_name(MDEntryType::Offer) == "Offer");
    REQUIRE(md_entry_type_name(MDEntryType::Trade) == "Trade");
    REQUIRE(md_entry_type_name(MDEntryType::SettlementPrice) == "SettlementPrice");
}

TEST_CASE("MDEntryType - Classification", "[market_data][types][regression]") {
    REQUIRE(is_quote_type(MDEntryType::Bid));
    REQUIRE(is_quote_type(MDEntryType::Offer));
    REQUIRE_FALSE(is_quote_type(MDEntryType::Trade));

    REQUIRE(is_trade_type(MDEntryType::Trade));
    REQUIRE(is_trade_type(MDEntryType::TradeVolume));
    REQUIRE_FALSE(is_trade_type(MDEntryType::Bid));
}

TEST_CASE("MDUpdateAction - Name conversion", "[market_data][types][regression]") {
    REQUIRE(md_update_action_name(MDUpdateAction::New) == "New");
    REQUIRE(md_update_action_name(MDUpdateAction::Change) == "Change");
    REQUIRE(md_update_action_name(MDUpdateAction::Delete) == "Delete");
}

TEST_CASE("SubscriptionRequestType - Name conversion", "[market_data][types][regression]") {
    REQUIRE(subscription_type_name(SubscriptionRequestType::Snapshot) == "Snapshot");
    REQUIRE(subscription_type_name(SubscriptionRequestType::SnapshotPlusUpdates) == "Subscribe");
    REQUIRE(subscription_type_name(SubscriptionRequestType::DisablePreviousSnapshot) == "Unsubscribe");
}

TEST_CASE("MDReqRejReason - Name conversion", "[market_data][types][regression]") {
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnknownSymbol) == "UnknownSymbol");
    REQUIRE(md_rej_reason_name(MDReqRejReason::DuplicateMDReqID) == "DuplicateMDReqID");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedMarketDepth) == "UnsupportedMarketDepth");
}

// ============================================================================
// Phase 7A: Market Data Type Metadata (TICKET_479)
// ============================================================================

TEST_CASE("MDEntryType - All name conversions", "[market_data][types][metadata][regression]") {
    REQUIRE(md_entry_type_name(MDEntryType::Bid) == "Bid");
    REQUIRE(md_entry_type_name(MDEntryType::Offer) == "Offer");
    REQUIRE(md_entry_type_name(MDEntryType::Trade) == "Trade");
    REQUIRE(md_entry_type_name(MDEntryType::IndexValue) == "IndexValue");
    REQUIRE(md_entry_type_name(MDEntryType::OpeningPrice) == "OpeningPrice");
    REQUIRE(md_entry_type_name(MDEntryType::ClosingPrice) == "ClosingPrice");
    REQUIRE(md_entry_type_name(MDEntryType::SettlementPrice) == "SettlementPrice");
    REQUIRE(md_entry_type_name(MDEntryType::TradingSessionHighPrice) == "SessionHigh");
    REQUIRE(md_entry_type_name(MDEntryType::TradingSessionLowPrice) == "SessionLow");
    REQUIRE(md_entry_type_name(MDEntryType::TradingSessionVWAPPrice) == "VWAP");
    REQUIRE(md_entry_type_name(MDEntryType::Imbalance) == "Imbalance");
    REQUIRE(md_entry_type_name(MDEntryType::TradeVolume) == "TradeVolume");
    REQUIRE(md_entry_type_name(MDEntryType::OpenInterest) == "OpenInterest");
}

TEST_CASE("MDEntryType - Invalid value returns Unknown", "[market_data][types][metadata][regression]") {
    REQUIRE(md_entry_type_name(static_cast<MDEntryType>('Z')) == "Unknown");
    REQUIRE(md_entry_type_name(static_cast<MDEntryType>('\0')) == "Unknown");
    REQUIRE(md_entry_type_name(static_cast<MDEntryType>('D')) == "Unknown");
}

TEST_CASE("MDEntryType - Consteval template lookup", "[market_data][types][metadata][regression]") {
    static_assert(md_entry_type_name<MDEntryType::Bid>() == "Bid");
    static_assert(md_entry_type_name<MDEntryType::Offer>() == "Offer");
    static_assert(md_entry_type_name<MDEntryType::Trade>() == "Trade");
    static_assert(md_entry_type_name<MDEntryType::TradeVolume>() == "TradeVolume");
    static_assert(md_entry_type_name<MDEntryType::OpenInterest>() == "OpenInterest");
    REQUIRE(md_entry_type_name<MDEntryType::Bid>() == "Bid");
}

TEST_CASE("is_quote_type - Exhaustive", "[market_data][types][metadata][regression]") {
    REQUIRE(is_quote_type(MDEntryType::Bid));
    REQUIRE(is_quote_type(MDEntryType::Offer));
    REQUIRE_FALSE(is_quote_type(MDEntryType::Trade));
    REQUIRE_FALSE(is_quote_type(MDEntryType::IndexValue));
    REQUIRE_FALSE(is_quote_type(MDEntryType::TradeVolume));
    REQUIRE_FALSE(is_quote_type(MDEntryType::OpenInterest));
}

TEST_CASE("is_trade_type - Exhaustive", "[market_data][types][metadata][regression]") {
    REQUIRE(is_trade_type(MDEntryType::Trade));
    REQUIRE(is_trade_type(MDEntryType::TradeVolume));
    REQUIRE_FALSE(is_trade_type(MDEntryType::Bid));
    REQUIRE_FALSE(is_trade_type(MDEntryType::Offer));
    REQUIRE_FALSE(is_trade_type(MDEntryType::IndexValue));
    REQUIRE_FALSE(is_trade_type(MDEntryType::OpenInterest));
}

TEST_CASE("MDUpdateAction - All name conversions", "[market_data][types][metadata][regression]") {
    REQUIRE(md_update_action_name(MDUpdateAction::New) == "New");
    REQUIRE(md_update_action_name(MDUpdateAction::Change) == "Change");
    REQUIRE(md_update_action_name(MDUpdateAction::Delete) == "Delete");
    REQUIRE(md_update_action_name(MDUpdateAction::DeleteThru) == "DeleteThru");
    REQUIRE(md_update_action_name(MDUpdateAction::DeleteFrom) == "DeleteFrom");
}

TEST_CASE("MDUpdateAction - Invalid value returns Unknown", "[market_data][types][metadata][regression]") {
    REQUIRE(md_update_action_name(static_cast<MDUpdateAction>('9')) == "Unknown");
    REQUIRE(md_update_action_name(static_cast<MDUpdateAction>('A')) == "Unknown");
}

TEST_CASE("MDUpdateAction - Consteval template lookup", "[market_data][types][metadata][regression]") {
    static_assert(md_update_action_name<MDUpdateAction::New>() == "New");
    static_assert(md_update_action_name<MDUpdateAction::Delete>() == "Delete");
    static_assert(md_update_action_name<MDUpdateAction::DeleteFrom>() == "DeleteFrom");
    REQUIRE(md_update_action_name<MDUpdateAction::Change>() == "Change");
}

TEST_CASE("SubscriptionRequestType - Invalid value returns Unknown", "[market_data][types][metadata][regression]") {
    REQUIRE(subscription_type_name(static_cast<SubscriptionRequestType>('9')) == "Unknown");
    REQUIRE(subscription_type_name(static_cast<SubscriptionRequestType>('A')) == "Unknown");
}

TEST_CASE("SubscriptionRequestType - Consteval template lookup", "[market_data][types][metadata][regression]") {
    static_assert(subscription_type_name<SubscriptionRequestType::Snapshot>() == "Snapshot");
    static_assert(subscription_type_name<SubscriptionRequestType::SnapshotPlusUpdates>() == "Subscribe");
    static_assert(subscription_type_name<SubscriptionRequestType::DisablePreviousSnapshot>() == "Unsubscribe");
    REQUIRE(subscription_type_name<SubscriptionRequestType::Snapshot>() == "Snapshot");
}

TEST_CASE("MDReqRejReason - All name conversions", "[market_data][types][metadata][regression]") {
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnknownSymbol) == "UnknownSymbol");
    REQUIRE(md_rej_reason_name(MDReqRejReason::DuplicateMDReqID) == "DuplicateMDReqID");
    REQUIRE(md_rej_reason_name(MDReqRejReason::InsufficientPermissions) == "InsufficientPermissions");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedSubscriptionType) == "UnsupportedSubscriptionType");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedMarketDepth) == "UnsupportedMarketDepth");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedMDUpdateType) == "UnsupportedMDUpdateType");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedAggregatedBook) == "UnsupportedAggregatedBook");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedMDEntryType) == "UnsupportedMDEntryType");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedTradingSessionID) == "UnsupportedTradingSessionID");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedScope) == "UnsupportedScope");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedOpenCloseSettleFlag) == "UnsupportedOpenCloseSettleFlag");
    REQUIRE(md_rej_reason_name(MDReqRejReason::UnsupportedMDImplicitDelete) == "UnsupportedMDImplicitDelete");
    REQUIRE(md_rej_reason_name(MDReqRejReason::InsufficientCredit) == "InsufficientCredit");
    REQUIRE(md_rej_reason_name(MDReqRejReason::Other) == "Other");
}

TEST_CASE("MDReqRejReason - Invalid value returns Unknown", "[market_data][types][metadata][regression]") {
    REQUIRE(md_rej_reason_name(static_cast<MDReqRejReason>('E')) == "Unknown");
    REQUIRE(md_rej_reason_name(static_cast<MDReqRejReason>('Z')) == "Unknown");
    REQUIRE(md_rej_reason_name(static_cast<MDReqRejReason>('\0')) == "Unknown");
}

TEST_CASE("MDReqRejReason - Consteval template lookup", "[market_data][types][metadata][regression]") {
    static_assert(md_rej_reason_name<MDReqRejReason::UnknownSymbol>() == "UnknownSymbol");
    static_assert(md_rej_reason_name<MDReqRejReason::Other>() == "Other");
    static_assert(md_rej_reason_name<MDReqRejReason::InsufficientCredit>() == "InsufficientCredit");
    REQUIRE(md_rej_reason_name<MDReqRejReason::UnknownSymbol>() == "UnknownSymbol");
}

TEST_CASE("MDEntry - Convenience predicates", "[market_data][types][metadata][regression]") {
    SECTION("is_bid / is_offer / is_trade") {
        MDEntry bid{.entry_type = MDEntryType::Bid};
        REQUIRE(bid.is_bid());
        REQUIRE_FALSE(bid.is_offer());
        REQUIRE_FALSE(bid.is_trade());

        MDEntry offer{.entry_type = MDEntryType::Offer};
        REQUIRE_FALSE(offer.is_bid());
        REQUIRE(offer.is_offer());
        REQUIRE_FALSE(offer.is_trade());

        MDEntry trade{.entry_type = MDEntryType::Trade};
        REQUIRE_FALSE(trade.is_bid());
        REQUIRE_FALSE(trade.is_offer());
        REQUIRE(trade.is_trade());
    }

    SECTION("has_price with zero and non-zero") {
        MDEntry no_price{.price_raw = 0};
        REQUIRE_FALSE(no_price.has_price());

        MDEntry with_price{.price_raw = 15025};
        REQUIRE(with_price.has_price());

        MDEntry negative_price{.price_raw = -100};
        REQUIRE(negative_price.has_price());
    }

    SECTION("has_size with zero and non-zero") {
        MDEntry no_size{.size_raw = 0};
        REQUIRE_FALSE(no_size.has_size());

        MDEntry with_size{.size_raw = 1000};
        REQUIRE(with_size.has_size());

        MDEntry negative_size{.size_raw = -1};
        REQUIRE(negative_size.has_size());
    }
}
