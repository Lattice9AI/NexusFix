#include <catch2/catch_test_macros.hpp>
#include <string>

#include "nexusfix/types/fix_version.hpp"
#include "nexusfix/messages/fix42/fix42.hpp"
#include "nexusfix/messages/fix44/execution_report.hpp"
#include "nexusfix/messages/fix44/new_order_single.hpp"
#include "nexusfix/messages/fix50/fix50.hpp"
#include "nexusfix/messages/common/trailer.hpp"

using namespace nfx;

// ============================================================================
// Multi-version Protocol Interaction Tests (TICKET_479 Phase 5B)
// ============================================================================

TEST_CASE("FIX 4.2 message parsed by FIX 4.4 parser returns wrong BeginString",
          "[fix_version][integration][regression]") {
    // Build a FIX 4.2 ExecutionReport using fix42 builder
    MessageAssembler asm_;
    auto raw = nfx::fix42::ExecutionReport::Builder{}
        .sender_comp_id("BROKER")
        .target_comp_id("CLIENT")
        .msg_seq_num(1)
        .sending_time("20260427-10:00:00.000")
        .order_id("ORD001")
        .exec_id("EXEC001")
        .exec_trans_type(ExecTransType::New)
        .exec_type(ExecType::New)
        .ord_status(OrdStatus::New)
        .symbol("AAPL")
        .side(Side::Buy)
        .cum_qty(Qty::from_int(0))
        .avg_px(FixedPrice::from_double(0.0))
        .leaves_qty(Qty::from_int(100))
        .build(asm_);

    // FIX 4.4 parser can parse it structurally (same MsgType=8)
    // but BeginString will be FIX.4.2, not FIX.4.4
    auto result44 = fix44::ExecutionReport::from_buffer(raw);
    REQUIRE(result44.has_value());
    REQUIRE(result44->header.begin_string == "FIX.4.2");
    REQUIRE(detect_version(result44->header.begin_string) == FixVersion::FIX_4_2);
    REQUIRE_FALSE(result44->header.begin_string == "FIX.4.4");
}

TEST_CASE("FIXT 1.1 transport with FIX 5.0 application layer round-trip",
          "[fix_version][integration][regression]") {
    // Build a FIX 5.0 NOS which uses FIXT.1.1 as BeginString
    MessageAssembler asm_;
    auto raw = fix50::NewOrderSingle::Builder{}
        .sender_comp_id("CLIENT")
        .target_comp_id("EXCHANGE")
        .msg_seq_num(1)
        .sending_time("20260427-14:30:00.000")
        .cl_ord_id("ORD001")
        .symbol("GOOG")
        .side(Side::Buy)
        .transact_time("20260427-14:30:00.000")
        .order_qty(Qty::from_int(50))
        .ord_type(OrdType::Market)
        .use_fix50()
        .build(asm_);

    auto result = fix50::NewOrderSingle::from_buffer(raw);
    REQUIRE(result.has_value());

    // Transport layer is FIXT.1.1
    REQUIRE(result->header.begin_string == "FIXT.1.1");
    REQUIRE(detect_version(result->header.begin_string) == FixVersion::FIXT_1_1);
    REQUIRE(is_fixt_version(detect_version(result->header.begin_string)));
    REQUIRE_FALSE(is_fix4_version(detect_version(result->header.begin_string)));

    // Application layer version is FIX 5.0 via ApplVerID
    REQUIRE(result->appl_ver_id == appl_ver_id::FIX_5_0);
    REQUIRE(appl_ver_id::to_string(result->appl_ver_id) == "FIX.5.0");
}

TEST_CASE("Ambiguous BeginString returns Unknown version",
          "[fix_version][integration][regression]") {
    SECTION("Truncated BeginString") {
        REQUIRE(detect_version("FIX.4") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX.") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX") == FixVersion::Unknown);
        REQUIRE(detect_version("F") == FixVersion::Unknown);
    }

    SECTION("Garbage BeginString") {
        REQUIRE(detect_version("GARBAGE") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX.9.9") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX.4.5") == FixVersion::Unknown);
        REQUIRE(detect_version("FIXT.2.0") == FixVersion::Unknown);
    }

    SECTION("Empty and null") {
        REQUIRE(detect_version("") == FixVersion::Unknown);
        REQUIRE(detect_version(std::string_view{}) == FixVersion::Unknown);
    }

    SECTION("FIX 5.0 requires FIXT.1.1 transport, not 7-char detection") {
        // "FIX.5.0" is 7 chars but has '5' not '4' at position 4
        REQUIRE(detect_version("FIX.5.0") == FixVersion::Unknown);
    }
}

TEST_CASE("Cross-version BeginString detection consistency",
          "[fix_version][integration][regression]") {
    // Verify detect_version and version_string are inverses for all FIX 4.x
    SECTION("FIX 4.x round-trip") {
        for (int minor = 0; minor <= 4; ++minor) {
            auto ver = static_cast<FixVersion>(minor + 1);  // FIX_4_0=1
            auto str = version_string(ver);
            REQUIRE(detect_version(str) == ver);
        }
    }

    SECTION("FIXT.1.1 round-trip") {
        auto str = version_string(FixVersion::FIXT_1_1);
        REQUIRE(str == "FIXT.1.1");
        REQUIRE(detect_version(str) == FixVersion::FIXT_1_1);
    }

    SECTION("FIX 5.0 has no BeginString detection path") {
        // FIX 5.0+ uses FIXT.1.1 as BeginString + ApplVerID
        // version_string returns "FIX.5.0" but detect_version("FIX.5.0") is Unknown
        auto str = version_string(FixVersion::FIX_5_0);
        REQUIRE(str == "FIX.5.0");
        REQUIRE(detect_version(str) == FixVersion::Unknown);
    }
}
