#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <array>

#include "nexusfix/sbe/codecs/execution_report.hpp"
#include "nexusfix/sbe/codecs/new_order_single.hpp"
#include "nexusfix/sbe/types/sbe_types.hpp"

using namespace nfx;
using namespace nfx::sbe;

// ============================================================================
// SBE Type Read/Write Tests
// ============================================================================

TEST_CASE("SBE read_le / write_le roundtrip", "[sbe][sbe_types][regression]") {
    SECTION("int64") {
        char buf[8]{};
        SbeInt64 val = 123456789012345LL;
        write_le(buf, val);
        REQUIRE(read_le<SbeInt64>(buf) == val);
    }

    SECTION("int32") {
        char buf[4]{};
        SbeInt32 val = -42;
        write_le(buf, val);
        REQUIRE(read_le<SbeInt32>(buf) == val);
    }

    SECTION("int16") {
        char buf[2]{};
        SbeInt16 val = 12345;
        write_le(buf, val);
        REQUIRE(read_le<SbeInt16>(buf) == val);
    }

    SECTION("uint8") {
        char buf[1]{};
        SbeUint8 val = 255;
        write_le(buf, val);
        REQUIRE(read_le<SbeUint8>(buf) == val);
    }

    SECTION("zero") {
        char buf[8]{};
        SbeInt64 val = 0;
        write_le(buf, val);
        REQUIRE(read_le<SbeInt64>(buf) == 0);
    }

    SECTION("negative") {
        char buf[8]{};
        SbeInt64 val = -1;
        write_le(buf, val);
        REQUIRE(read_le<SbeInt64>(buf) == -1);
    }
}

TEST_CASE("SBE convenience read/write functions", "[sbe][sbe_types][regression]") {
    char buf[8]{};

    write_int64(buf, 42);
    REQUIRE(read_int64(buf) == 42);

    write_int32(buf, -100);
    REQUIRE(read_int32(buf) == -100);

    write_uint16(buf, 65535);
    REQUIRE(read_uint16(buf) == 65535);

    write_char(buf, 'X');
    REQUIRE(read_char(buf) == 'X');
}

TEST_CASE("SBE null value constants", "[sbe][sbe_types][regression]") {
    REQUIRE(null_value::CHAR == '\0');
    REQUIRE(null_value::INT8 == INT8_MIN);
    REQUIRE(null_value::INT16 == INT16_MIN);
    REQUIRE(null_value::INT32 == INT32_MIN);
    REQUIRE(null_value::INT64 == INT64_MIN);
    REQUIRE(null_value::UINT8 == UINT8_MAX);
    REQUIRE(null_value::UINT16 == UINT16_MAX);
    REQUIRE(null_value::UINT32 == UINT32_MAX);
    REQUIRE(null_value::UINT64 == UINT64_MAX);
}

TEST_CASE("SBE check_bounds", "[sbe][sbe_types][regression]") {
    REQUIRE(check_bounds(0, 8, 64));
    REQUIRE(check_bounds(56, 8, 64));
    REQUIRE_FALSE(check_bounds(57, 8, 64));
    REQUIRE_FALSE(check_bounds(64, 1, 64));
    REQUIRE(check_bounds(0, 0, 0));
}

// ============================================================================
// SBE NewOrderSingleCodec Tests
// ============================================================================

TEST_CASE("NewOrderSingleCodec static constants", "[sbe][nos_codec][regression]") {
    REQUIRE(NewOrderSingleCodec::TOTAL_SIZE == 64);
    REQUIRE(NewOrderSingleCodec::BLOCK_LENGTH == 56);
    REQUIRE(NewOrderSingleCodec::encodedSize() == 64);
}

TEST_CASE("NewOrderSingleCodec encode and decode roundtrip", "[sbe][nos_codec][regression]") {
    alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};

    auto encoder = NewOrderSingleCodec::wrapForEncode(buffer, sizeof(buffer));
    encoder.encodeHeader()
        .clOrdId("ORD123")
        .symbol("AAPL")
        .side(Side::Buy)
        .ordType(OrdType::Limit)
        .price(FixedPrice::from_double(150.50))
        .orderQty(Qty::from_int(100))
        .transactTime(Timestamp{1706000000000000000LL});

    auto decoder = NewOrderSingleCodec::wrapForDecode(buffer, sizeof(buffer));
    REQUIRE(decoder.isValid());
    REQUIRE(decoder.clOrdId() == "ORD123");
    REQUIRE(decoder.symbol() == "AAPL");
    REQUIRE(decoder.side() == Side::Buy);
    REQUIRE(decoder.ordType() == OrdType::Limit);
    REQUIRE(decoder.orderQty().whole() == 100);
    REQUIRE(decoder.transactTime().nanos == 1706000000000000000LL);
}

TEST_CASE("NewOrderSingleCodec invalid buffer", "[sbe][nos_codec][regression]") {
    SECTION("nullptr") {
        auto decoder = NewOrderSingleCodec::wrapForDecode(nullptr, 0);
        REQUIRE_FALSE(decoder.isValid());
    }

    SECTION("too small") {
        char buffer[10]{};
        auto decoder = NewOrderSingleCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE_FALSE(decoder.isValid());
    }
}

// ============================================================================
// SBE ExecutionReportCodec Tests
// ============================================================================

TEST_CASE("ExecutionReportCodec static constants", "[sbe][er_codec][regression]") {
    REQUIRE(ExecutionReportCodec::TOTAL_SIZE == 144);
    REQUIRE(ExecutionReportCodec::BLOCK_LENGTH == 136);
    REQUIRE(ExecutionReportCodec::encodedSize() == 144);
}

TEST_CASE("ExecutionReportCodec encode and decode roundtrip", "[sbe][er_codec][regression]") {
    alignas(8) char buffer[ExecutionReportCodec::TOTAL_SIZE]{};

    auto encoder = ExecutionReportCodec::wrapForEncode(buffer, sizeof(buffer));
    encoder.encodeHeader()
        .orderId("EX001")
        .execId("EXEC001")
        .clOrdId("ORD123")
        .symbol("AAPL")
        .side(Side::Buy)
        .execType(ExecType::Fill)
        .ordStatus(OrdStatus::Filled)
        .price(FixedPrice::from_double(150.50))
        .orderQty(Qty::from_int(100))
        .lastPx(FixedPrice::from_double(150.50))
        .lastQty(Qty::from_int(100))
        .leavesQty(Qty::from_int(0))
        .cumQty(Qty::from_int(100))
        .avgPx(FixedPrice::from_double(150.50))
        .transactTime(Timestamp{1706000000000000000LL});

    auto decoder = ExecutionReportCodec::wrapForDecode(buffer, sizeof(buffer));
    REQUIRE(decoder.isValid());
    REQUIRE(decoder.orderId() == "EX001");
    REQUIRE(decoder.execId() == "EXEC001");
    REQUIRE(decoder.clOrdId() == "ORD123");
    REQUIRE(decoder.symbol() == "AAPL");
    REQUIRE(decoder.side() == Side::Buy);
    REQUIRE(decoder.execType() == ExecType::Fill);
    REQUIRE(decoder.ordStatus() == OrdStatus::Filled);
    REQUIRE(decoder.lastQty().whole() == 100);
    REQUIRE(decoder.leavesQty().whole() == 0);
    REQUIRE(decoder.cumQty().whole() == 100);
}

TEST_CASE("ExecutionReportCodec invalid buffer", "[sbe][er_codec][regression]") {
    SECTION("nullptr") {
        auto decoder = ExecutionReportCodec::wrapForDecode(nullptr, 0);
        REQUIRE_FALSE(decoder.isValid());
    }

    SECTION("too small") {
        char buffer[32]{};
        auto decoder = ExecutionReportCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE_FALSE(decoder.isValid());
    }
}

TEST_CASE("ExecutionReportCodec field offsets are 8-byte aligned", "[sbe][er_codec][regression]") {
    REQUIRE(ExecutionReportCodec::Offset::Price % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::OrderQty % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::LastPx % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::LastQty % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::LeavesQty % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::CumQty % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::AvgPx % 8 == 0);
    REQUIRE(ExecutionReportCodec::Offset::TransactTime % 8 == 0);
}
