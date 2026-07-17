// test_branch_coverage_499.cpp
//
// TICKET_499 WI3: close the remaining real branches named in the ticket so
// overall branch coverage climbs from the 73% baseline toward the 82% target.
// Each section targets uncovered BRDA lines measured on the 2026-07-17 merged
// tracefile (SIMD off + on, GCC 14):
//
//   sbe/codecs/execution_report.hpp   - encode-truncation + isValid chain
//   sbe/codecs/new_order_single.hpp   - encode-truncation + isValid chain
//   messages/common/trailer.hpp       - validate() edges, MessageAssembler
//                                        field(int64_t/char/FixedPrice) overloads
//   messages/common/header.hpp        - FixHeader::validate() remaining fields,
//                                        HeaderBuilder, from_fields
//   util/format_utils.hpp             - format_bytes/latency/throughput/hex tiers
//
// These are genuine source-level branches, not the GCC memcpy/memset dispatch
// artifacts in composite_types.hpp (already LCOV_EXCL_BR_LINE'd).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

#include "nexusfix/messages/common/header.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/sbe/sbe.hpp"
#include "nexusfix/sbe/codecs/execution_report.hpp"
#include "nexusfix/sbe/codecs/new_order_single.hpp"
#include "nexusfix/sbe/message_header.hpp"
#include "nexusfix/types/field_types.hpp"
#include "nexusfix/util/format_utils.hpp"

using namespace nfx;
using namespace nfx::sbe;

// ============================================================================
// SBE codec: encode-truncation branch (truncated_ = true)
// ============================================================================
// The encoders set truncated_ only when the value exceeds the fixed field
// width. Nothing in the existing suite passes an over-long string, so the
// false-arm of every `if (!encode(...))` was never taken.

TEST_CASE("SBE NewOrderSingleCodec encode truncation flags", "[sbe][nos_codec][branch499][regression]") {
    alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};
    auto enc = NewOrderSingleCodec::wrapForEncode(buffer, sizeof(buffer));
    enc.encodeHeader();

    SECTION("clOrdId over 20 chars sets truncated") {
        enc.clOrdId("012345678901234567890123");  // 24 > 20
        REQUIRE(enc.truncated());
    }
    SECTION("symbol over 8 chars sets truncated") {
        enc.symbol("VERYLONGSYMBOL");  // 14 > 8
        REQUIRE(enc.truncated());
    }
    SECTION("fields within width leave truncated false") {
        enc.clOrdId("ORD1").symbol("AAPL");
        REQUIRE_FALSE(enc.truncated());
    }
}

TEST_CASE("SBE ExecutionReportCodec encode truncation flags", "[sbe][er_codec][branch499][regression]") {
    alignas(8) char buffer[ExecutionReportCodec::TOTAL_SIZE]{};
    auto enc = ExecutionReportCodec::wrapForEncode(buffer, sizeof(buffer));
    enc.encodeHeader();

    SECTION("orderId over 20 chars sets truncated") {
        enc.orderId("012345678901234567890123");
        REQUIRE(enc.truncated());
    }
    SECTION("execId over 20 chars sets truncated") {
        enc.execId("012345678901234567890123");
        REQUIRE(enc.truncated());
    }
    SECTION("clOrdId over 20 chars sets truncated") {
        enc.clOrdId("012345678901234567890123");
        REQUIRE(enc.truncated());
    }
    SECTION("symbol over 8 chars sets truncated") {
        enc.symbol("VERYLONGSYMBOL");
        REQUIRE(enc.truncated());
    }
    SECTION("all fields within width leave truncated false") {
        enc.orderId("EX1").execId("EXEC1").clOrdId("ORD1").symbol("AAPL");
        REQUIRE_FALSE(enc.truncated());
    }
}

// isValid() is a short-circuit && chain: header.isValid() && templateId==
// && blockLength==. Existing tests hit wrong-templateId and wrong-blockLength,
// but not the case where the header itself is invalid (first arm false), nor
// the fully-valid true path reached purely through isValid().
TEST_CASE("SBE codec isValid short-circuit arms", "[sbe][branch499][regression]") {
    SECTION("NOS: garbage header fails first && arm") {
        alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};
        // Length is fine, but the header bytes are zero, so header.isValid()
        // is false and the chain short-circuits before templateId is read.
        auto dec = NewOrderSingleCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE_FALSE(dec.isValid());
    }
    SECTION("NOS: encoded buffer passes full chain") {
        alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};
        NewOrderSingleCodec::wrapForEncode(buffer, sizeof(buffer)).encodeHeader();
        auto dec = NewOrderSingleCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE(dec.isValid());
    }
    SECTION("ER: garbage header fails first && arm") {
        alignas(8) char buffer[ExecutionReportCodec::TOTAL_SIZE]{};
        auto dec = ExecutionReportCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE_FALSE(dec.isValid());
    }
    SECTION("ER: encoded buffer passes full chain") {
        alignas(8) char buffer[ExecutionReportCodec::TOTAL_SIZE]{};
        ExecutionReportCodec::wrapForEncode(buffer, sizeof(buffer)).encodeHeader();
        auto dec = ExecutionReportCodec::wrapForDecode(buffer, sizeof(buffer));
        REQUIRE(dec.isValid());
    }
}

// sbe::dispatch has three non-happy-path arms the existing dispatch tests miss:
// a too-short buffer, a valid-size buffer whose header is invalid, and a valid
// header carrying an unknown templateId (the switch default). All route to the
// UnknownMessage handler.
TEST_CASE("SBE dispatch routes non-message inputs to UnknownMessage", "[sbe][dispatch][branch499][regression]") {
    SECTION("buffer shorter than header -> unknown") {
        char buffer[4]{};
        bool unknown = false;
        sbe::dispatch(buffer, sizeof(buffer), [&](auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, sbe::UnknownMessage>) { unknown = true; }
        });
        REQUIRE(unknown);
    }
    SECTION("all-zero header (templateId 0) -> unknown via switch default") {
        alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};
        // All-zero header: MessageHeader::isValid() is true (non-null, long
        // enough) so the !isValid arm is skipped and templateId 0 falls
        // through to the switch default.
        bool unknown = false;
        sbe::SbeUint16 seen_tid = 0xFFFF;
        sbe::dispatch(buffer, sizeof(buffer), [&](auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, sbe::UnknownMessage>) {
                unknown = true;
                seen_tid = m.templateId;
            }
        });
        REQUIRE(unknown);
        REQUIRE(seen_tid == 0);  // unknown template id from the default arm
    }
    SECTION("valid header, unrecognized templateId -> unknown default arm") {
        alignas(8) char buffer[NewOrderSingleCodec::TOTAL_SIZE]{};
        // Encode a proper header then overwrite the templateId with a value
        // that matches neither NewOrderSingle nor ExecutionReport.
        NewOrderSingleCodec::wrapForEncode(buffer, sizeof(buffer)).encodeHeader();
        auto hdr = sbe::MessageHeader::wrapForEncode(buffer, sizeof(buffer));
        hdr.encodeHeader(NewOrderSingleCodec::BLOCK_LENGTH, 9999);
        bool unknown = false;
        sbe::dispatch(buffer, sizeof(buffer), [&](auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, sbe::UnknownMessage>) { unknown = true; }
        });
        REQUIRE(unknown);
    }
}

// ============================================================================
// messages/common/header.hpp
// ============================================================================
// FixHeader::validate(): existing tests cover BeginString/MsgType/SenderCompID.
// Close the rest of the ladder plus the PossDup/OrigSendingTime interaction.

namespace {
// A header that passes validate(); each section knocks out one field.
FixHeader make_valid_header() noexcept {
    FixHeader h;
    h.begin_string = "FIX.4.4";
    h.body_length = 70;
    h.msg_type = 'D';
    h.sender_comp_id = "CLIENT";
    h.target_comp_id = "SERVER";
    h.msg_seq_num = 1;
    h.sending_time = "20260717-10:30:00";
    return h;
}
}  // namespace

TEST_CASE("FixHeader::validate remaining missing-field branches", "[messages][header][branch499][regression]") {
    SECTION("missing BodyLength (<= 0)") {
        auto h = make_valid_header();
        h.body_length = 0;
        auto err = h.validate();
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
        REQUIRE(err.tag == tag::BodyLength::value);
    }
    SECTION("negative BodyLength also rejected") {
        auto h = make_valid_header();
        h.body_length = -5;
        REQUIRE(h.validate().code == ParseErrorCode::MissingRequiredField);
    }
    SECTION("missing TargetCompID") {
        auto h = make_valid_header();
        h.target_comp_id = {};
        auto err = h.validate();
        REQUIRE(err.tag == tag::TargetCompID::value);
    }
    SECTION("missing MsgSeqNum (== 0)") {
        auto h = make_valid_header();
        h.msg_seq_num = 0;
        auto err = h.validate();
        REQUIRE(err.tag == tag::MsgSeqNum::value);
    }
    SECTION("missing SendingTime") {
        auto h = make_valid_header();
        h.sending_time = {};
        auto err = h.validate();
        REQUIRE(err.tag == tag::SendingTime::value);
    }
    SECTION("PossDupFlag=Y requires OrigSendingTime") {
        auto h = make_valid_header();
        h.poss_dup_flag = true;
        h.orig_sending_time = {};
        auto err = h.validate();
        REQUIRE(err.tag == tag::OrigSendingTime::value);
    }
    SECTION("PossDupFlag=Y with OrigSendingTime present is valid") {
        auto h = make_valid_header();
        h.poss_dup_flag = true;
        h.orig_sending_time = "20260717-10:29:59";
        REQUIRE(h.validate().code == ParseErrorCode::None);
    }
}

TEST_CASE("FixHeader::from_fields populates all fields", "[messages][header][branch499][regression]") {
    FieldTable<512> fields;
    auto put = [&fields](int tag, std::string_view v) {
        (void)fields.set(tag, std::span<const char>{v.data(), v.size()});
    };
    put(tag::BeginString::value, "FIX.4.4");
    put(tag::BodyLength::value, "70");
    put(tag::MsgType::value, "D");
    put(tag::SenderCompID::value, "CLIENT");
    put(tag::TargetCompID::value, "SERVER");
    put(tag::MsgSeqNum::value, "42");
    put(tag::SendingTime::value, "20260717-10:30:00");
    put(tag::PossDupFlag::value, "Y");
    put(tag::PossResend::value, "Y");
    put(tag::OrigSendingTime::value, "20260717-10:29:59");

    auto hdr = FixHeader::from_fields(fields);
    REQUIRE(hdr.begin_string == "FIX.4.4");
    REQUIRE(hdr.body_length == 70);
    REQUIRE(hdr.msg_type == 'D');
    REQUIRE(hdr.msg_seq_num == 42u);
    REQUIRE(hdr.poss_dup_flag);
    REQUIRE(hdr.poss_resend);
    REQUIRE(hdr.orig_sending_time == "20260717-10:29:59");
    REQUIRE(hdr.validate().code == ParseErrorCode::None);
}

TEST_CASE("HeaderBuilder assembles and updates body length", "[messages][header][branch499][regression]") {
    HeaderBuilder b;
    b.begin_string("FIX.4.4")
        .body_length_placeholder()
        .msg_type('D')
        .sender_comp_id("CLIENT")
        .target_comp_id("SERVER")
        .msg_seq_num(7)          // exercises append_field(int, uint32_t)
        .sending_time("20260717-10:30:00")
        .poss_dup_flag(true)     // ternary "Y"
        .orig_sending_time("20260717-10:29:59");

    REQUIRE_FALSE(b.truncated());
    REQUIRE(b.size() > 0);
    REQUIRE(b.body_length_pos() > 0);

    b.update_body_length(55);
    auto data = b.data();
    std::string_view sv{data.data(), data.size()};
    REQUIRE(sv.find("9=000055") != std::string_view::npos);

    SECTION("poss_dup_flag=false emits N") {
        HeaderBuilder b2;
        b2.begin_string("FIX.4.4").poss_dup_flag(false);
        auto d = b2.data();
        std::string_view s{d.data(), d.size()};
        REQUIRE(s.find("43=N") != std::string_view::npos);
    }

    SECTION("update_body_length is a no-op without placeholder") {
        HeaderBuilder b3;
        b3.begin_string("FIX.4.4");  // no body_length_placeholder()
        auto before = b3.size();
        b3.update_body_length(99);   // body_length_pos_ == 0 -> early return
        REQUIRE(b3.size() == before);
    }

    SECTION("multi-digit seq exercises append_field(uint32_t) reverse loop") {
        HeaderBuilder b4;
        b4.msg_seq_num(123456);
        auto d = b4.data();
        std::string_view s{d.data(), d.size()};
        REQUIRE(s.find("34=123456") != std::string_view::npos);
    }
}

// ============================================================================
// messages/common/trailer.hpp
// ============================================================================
// checksum::validate() ladder: too-short, missing 10=, non-digit checksum,
// mismatch, and the success path. Then the MessageAssembler field() overloads
// (int64_t incl. negative, char, FixedPrice incl. integer-zero / negative /
// trailing-zero-strip), which no existing test drives.

TEST_CASE("checksum::validate error ladder", "[messages][trailer][branch499][regression]") {
    SECTION("buffer under 8 bytes is too short") {
        std::array<char, 4> buf{'1', '0', '=', '0'};
        auto err = checksum::validate(std::span<const char>{buf.data(), buf.size()});
        REQUIRE(err.code == ParseErrorCode::BufferTooShort);
    }
    SECTION("no 10= trailer -> missing required field") {
        std::string msg = "8=FIX.4.4\x01" "35=0\x01";  // no checksum field
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
    }
    SECTION("non-digit checksum value -> invalid checksum") {
        std::string msg = "8=FIX\x01" "10=1X9\x01";
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code == ParseErrorCode::InvalidChecksum);
    }
    SECTION("wrong checksum value -> invalid checksum") {
        std::string body = "8=FIX.4.4\x01" "35=0\x01";
        uint8_t good = checksum::calculate(
            std::span<const char>{body.data(), body.size()});
        (void)good;
        std::string msg = body + "10=000\x01";  // deliberately wrong
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code == ParseErrorCode::InvalidChecksum);
    }
    SECTION("correct checksum validates clean") {
        std::string body = "8=FIX.4.4\x01" "35=0\x01" "49=A\x01" "56=B\x01";
        uint8_t sum = checksum::calculate(
            std::span<const char>{body.data(), body.size()});
        auto fmt = checksum::format(sum);
        std::string msg = body + "10=" + std::string(fmt.data(), 3) + "\x01";
        auto err = checksum::validate(std::span<const char>{msg.data(), msg.size()});
        REQUIRE(err.code == ParseErrorCode::None);
    }
}

TEST_CASE("MessageAssembler field overloads", "[messages][trailer][branch499][regression]") {
    MessageAssembler asm_;

    SECTION("int64_t field: positive, negative, and zero") {
        asm_.start(fix::FIX_4_4)
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(0))
            .field(38, static_cast<int64_t>(100))
            .field(44, static_cast<int64_t>(-250));
        auto data = asm_.finish();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("38=100") != std::string_view::npos);
        REQUIRE(sv.find("44=-250") != std::string_view::npos);
        REQUIRE_FALSE(asm_.truncated());
    }

    SECTION("char field overload") {
        asm_.start(fix::FIX_4_4).field(tag::MsgType::value, 'D');
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("35=D") != std::string_view::npos);
    }

    SECTION("FixedPrice: integer-only value (frac stripped to zero)") {
        asm_.start(fix::FIX_4_4).field(44, FixedPrice::from_double(150.0));
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("44=150") != std::string_view::npos);
    }

    SECTION("FixedPrice: fractional with trailing-zero strip") {
        asm_.start(fix::FIX_4_4).field(44, FixedPrice::from_double(150.50));
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("44=150.5") != std::string_view::npos);
    }

    SECTION("FixedPrice: value below 1.0 pads leading fractional zeros") {
        asm_.start(fix::FIX_4_4).field(44, FixedPrice::from_double(0.005));
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("44=0.005") != std::string_view::npos);
    }

    SECTION("FixedPrice: negative value prepends '-'") {
        asm_.start(fix::FIX_4_4).field(44, FixedPrice::from_double(-12.25));
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("44=-12.25") != std::string_view::npos);
    }

    SECTION("FIXT 1.1 start variant") {
        asm_.start_fixt11().field(tag::MsgType::value, 'A');
        auto data = asm_.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("8=FIXT.1.1") != std::string_view::npos);
    }
}

// ============================================================================
// util/format_utils.hpp
// ============================================================================
// Each formatter is a threshold ladder; drive every tier so both arms of each
// comparison are taken.

TEST_CASE("format_bytes covers every unit tier", "[util][format][branch499][regression]") {
    REQUIRE(util::format_bytes(512).find("bytes") != std::string::npos);
    REQUIRE(util::format_bytes(2 * 1024).find("KB") != std::string::npos);
    REQUIRE(util::format_bytes(3 * 1024 * 1024).find("MB") != std::string::npos);
    REQUIRE(util::format_bytes(static_cast<size_t>(4) * 1024 * 1024 * 1024)
                .find("GB") != std::string::npos);
}

TEST_CASE("format_latency_ns covers every unit tier", "[util][format][branch499][regression]") {
    REQUIRE(util::format_latency_ns(500).find("ns") != std::string::npos);
    REQUIRE(util::format_latency_ns(5'000).find("us") != std::string::npos);
    REQUIRE(util::format_latency_ns(5'000'000).find("ms") != std::string::npos);
    REQUIRE(util::format_latency_ns(2'000'000'000ULL).find("s") != std::string::npos);
}

TEST_CASE("format_throughput covers every unit tier", "[util][format][branch499][regression]") {
    REQUIRE(util::format_throughput(500.0).find("msg/s") != std::string::npos);
    REQUIRE(util::format_throughput(5'000.0).find("K msg/s") != std::string::npos);
    REQUIRE(util::format_throughput(5'000'000.0).find("M msg/s") != std::string::npos);
}

TEST_CASE("format_hex truncation branch", "[util][format][branch499][regression]") {
    std::array<std::byte, 40> data{};
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i);
    }
    SECTION("under limit: no truncation suffix") {
        auto s = util::format_hex(std::span<const std::byte>{data.data(), 4});
        REQUIRE(s.find("more bytes") == std::string::npos);
    }
    SECTION("over limit: truncation suffix appended") {
        auto s = util::format_hex(std::span<const std::byte>{data.data(), data.size()}, 32);
        REQUIRE(s.find("more bytes") != std::string::npos);
    }
}
