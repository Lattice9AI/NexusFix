#include <catch2/catch_test_macros.hpp>
#include <string>
#include <cstring>

#include "nexusfix/messages/common/header.hpp"
#include "nexusfix/messages/common/trailer.hpp"
#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/parser/simd_checksum.hpp"

using namespace nfx;

// ============================================================================
// HeaderBuilder Tests
// ============================================================================

TEST_CASE("HeaderBuilder basic operations", "[messages][header][regression]") {
    SECTION("Minimal header") {
        HeaderBuilder hdr;
        hdr.begin_string("FIX.4.4")
           .body_length_placeholder()
           .msg_type('A')
           .sender_comp_id("CLIENT")
           .target_comp_id("SERVER")
           .msg_seq_num(1)
           .sending_time("20231215-10:30:00");

        auto data = hdr.data();
        REQUIRE(data.size() > 0);
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("8=FIX.4.4") != std::string_view::npos);
        REQUIRE(sv.find("35=A") != std::string_view::npos);
        REQUIRE(sv.find("49=CLIENT") != std::string_view::npos);
        REQUIRE(sv.find("56=SERVER") != std::string_view::npos);
    }

    SECTION("Field ordering - BeginString first") {
        HeaderBuilder hdr;
        hdr.begin_string("FIX.4.4")
           .body_length_placeholder()
           .msg_type('D');

        auto data = hdr.data();
        std::string_view sv{data.data(), data.size()};
        // BeginString should appear before MsgType
        REQUIRE(sv.find("8=FIX.4.4") < sv.find("35=D"));
    }

    SECTION("PossDupFlag") {
        HeaderBuilder hdr;
        hdr.begin_string("FIX.4.4")
           .body_length_placeholder()
           .msg_type('A')
           .sender_comp_id("C")
           .target_comp_id("S")
           .msg_seq_num(1)
           .sending_time("20231215-10:30:00")
           .poss_dup_flag(true)
           .orig_sending_time("20231215-10:29:00");

        auto data = hdr.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("43=Y") != std::string_view::npos);
        REQUIRE(sv.find("122=20231215-10:29:00") != std::string_view::npos);
    }

    SECTION("Reset clears state") {
        HeaderBuilder hdr;
        hdr.begin_string("FIX.4.4")
           .body_length_placeholder()
           .msg_type('A');
        REQUIRE(hdr.size() > 0);

        hdr.reset();
        REQUIRE(hdr.size() == 0);
        REQUIRE(hdr.body_length_pos() == 0);
    }

    SECTION("update_body_length") {
        HeaderBuilder hdr;
        hdr.begin_string("FIX.4.4")
           .body_length_placeholder()
           .msg_type('0');

        hdr.update_body_length(42);
        auto data = hdr.data();
        std::string_view sv{data.data(), data.size()};
        REQUIRE(sv.find("9=000042") != std::string_view::npos);
    }
}

// ============================================================================
// FixHeader Tests
// ============================================================================

TEST_CASE("FixHeader from_fields and validate", "[messages][header][regression]") {
    SECTION("Validate complete header") {
        FixHeader hdr;
        hdr.begin_string = "FIX.4.4";
        hdr.body_length = 70;
        hdr.msg_type = 'A';
        hdr.sender_comp_id = "CLIENT";
        hdr.target_comp_id = "SERVER";
        hdr.msg_seq_num = 1;
        hdr.sending_time = "20231215-10:30:00";

        auto err = hdr.validate();
        REQUIRE(err.code == ParseErrorCode::None);
        REQUIRE_FALSE(hdr.poss_dup_flag);
    }

    SECTION("Validate missing BeginString") {
        FixHeader hdr;
        // begin_string is empty by default
        hdr.body_length = 10;
        hdr.msg_type = 'A';
        hdr.sender_comp_id = "C";
        hdr.target_comp_id = "S";
        hdr.msg_seq_num = 1;
        hdr.sending_time = "20231215";

        auto err = hdr.validate();
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
        REQUIRE(err.tag == tag::BeginString::value);
    }

    SECTION("Validate missing MsgType") {
        FixHeader hdr;
        hdr.begin_string = "FIX.4.4";
        hdr.body_length = 10;
        // msg_type defaults to '\0'
        hdr.sender_comp_id = "C";
        hdr.target_comp_id = "S";
        hdr.msg_seq_num = 1;
        hdr.sending_time = "20231215";

        auto err = hdr.validate();
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
        REQUIRE(err.tag == tag::MsgType::value);
    }

    SECTION("Validate missing SenderCompID") {
        FixHeader hdr;
        hdr.begin_string = "FIX.4.4";
        hdr.body_length = 10;
        hdr.msg_type = 'A';
        // sender_comp_id empty
        hdr.target_comp_id = "S";
        hdr.msg_seq_num = 1;
        hdr.sending_time = "20231215";

        auto err = hdr.validate();
        REQUIRE(err.code == ParseErrorCode::MissingRequiredField);
        REQUIRE(err.tag == tag::SenderCompID::value);
    }
}

// ============================================================================
// FixTrailer Tests
// ============================================================================

TEST_CASE("FixTrailer operations", "[messages][trailer][regression]") {
    SECTION("Default constructor") {
        FixTrailer trailer;
        REQUIRE(trailer.as_string() == "000");
        REQUIRE(trailer.as_int() == 0);
    }

    SECTION("Constructor from checksum") {
        FixTrailer trailer{static_cast<uint8_t>(185)};
        REQUIRE(trailer.as_string() == "185");
        REQUIRE(trailer.as_int() == 185);
    }

    SECTION("Boundary values") {
        FixTrailer t0{static_cast<uint8_t>(0)};
        REQUIRE(t0.as_string() == "000");
        REQUIRE(t0.as_int() == 0);

        FixTrailer t255{static_cast<uint8_t>(255)};
        REQUIRE(t255.as_string() == "255");
        REQUIRE(t255.as_int() == 255);

        FixTrailer t4{static_cast<uint8_t>(4)};
        REQUIRE(t4.as_string() == "004");
        REQUIRE(t4.as_int() == 4);
    }
}

// ============================================================================
// TrailerBuilder Tests
// ============================================================================

TEST_CASE("TrailerBuilder operations", "[messages][trailer][regression]") {
    SECTION("Build from checksum value") {
        TrailerBuilder builder;
        auto trailer = builder.build(static_cast<uint8_t>(42));
        REQUIRE(trailer.size() == TrailerBuilder::TRAILER_SIZE);

        std::string_view sv{trailer.data(), trailer.size()};
        REQUIRE(sv.find("10=042") != std::string_view::npos);
        REQUIRE(sv.back() == fix::SOH);
    }

    SECTION("Build from message body span") {
        std::string body = "8=FIX.4.4\x01" "9=5\x01" "35=0\x01";
        TrailerBuilder builder;
        auto trailer = builder.build(std::span<const char>{body.data(), body.size()});
        REQUIRE(trailer.size() == TrailerBuilder::TRAILER_SIZE);

        // Verify the checksum is correct
        uint8_t expected = fix::calculate_checksum(
            std::span<const char>{body.data(), body.size()});
        auto cs_arr = fix::format_checksum(expected);
        std::string_view trailer_sv{trailer.data(), trailer.size()};
        REQUIRE(trailer_sv.find(std::string_view{cs_arr.data(), 3}) != std::string_view::npos);
    }
}

// ============================================================================
// MessageAssembler Tests
// ============================================================================

TEST_CASE("MessageAssembler operations", "[messages][assembler][regression]") {
    SECTION("Complete message assembly") {
        MessageAssembler asm_;
        auto msg = asm_.start()
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "CLIENT")
            .field(tag::TargetCompID::value, "SERVER")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215-10:30:00")
            .finish();

        std::string_view sv{msg.data(), msg.size()};
        REQUIRE(sv.find("8=FIX.4.4") != std::string_view::npos);
        REQUIRE(sv.find("35=0") != std::string_view::npos);
        REQUIRE(sv.find("49=CLIENT") != std::string_view::npos);
        REQUIRE(sv.find("10=") != std::string_view::npos);
    }

    SECTION("Body length accuracy") {
        MessageAssembler asm_;
        auto msg = asm_.start()
            .field(tag::MsgType::value, 'A')
            .field(tag::SenderCompID::value, "C")
            .field(tag::TargetCompID::value, "S")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215")
            .finish();

        // Parse the assembled message and verify body length
        auto parsed = IndexedParser::parse(msg);
        REQUIRE(parsed.has_value());
        auto bl = parsed->get_int(tag::BodyLength::value);
        REQUIRE(bl.has_value());
        REQUIRE(*bl > 0);
    }

    SECTION("Checksum validity") {
        MessageAssembler asm_;
        auto msg = asm_.start()
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "SENDER")
            .field(tag::TargetCompID::value, "TARGET")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(5))
            .field(tag::SendingTime::value, "20231215-10:30:00")
            .finish();

        // Validate checksum
        auto err = checksum::validate(msg);
        REQUIRE(err.code == ParseErrorCode::None);
    }

    SECTION("Integer and char field types") {
        MessageAssembler asm_;
        auto msg = asm_.start()
            .field(tag::MsgType::value, 'D')
            .field(tag::SenderCompID::value, "C")
            .field(tag::TargetCompID::value, "S")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(42))
            .field(tag::SendingTime::value, "20231215")
            .field(54, '1')     // Side = Buy (char)
            .field(38, static_cast<int64_t>(100))  // OrderQty (int)
            .finish();

        std::string_view sv{msg.data(), msg.size()};
        REQUIRE(sv.find("34=42") != std::string_view::npos);
        REQUIRE(sv.find("54=1") != std::string_view::npos);
        REQUIRE(sv.find("38=100") != std::string_view::npos);
    }

    SECTION("Reset and rebuild") {
        MessageAssembler asm_;
        [[maybe_unused]] auto first = asm_.start()
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "A")
            .field(tag::TargetCompID::value, "B")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215")
            .finish();

        asm_.reset();

        auto msg2 = asm_.start()
            .field(tag::MsgType::value, 'A')
            .field(tag::SenderCompID::value, "X")
            .field(tag::TargetCompID::value, "Y")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(2))
            .field(tag::SendingTime::value, "20231216")
            .finish();

        std::string_view sv{msg2.data(), msg2.size()};
        REQUIRE(sv.find("35=A") != std::string_view::npos);
        REQUIRE(sv.find("49=X") != std::string_view::npos);
        REQUIRE(sv.find("34=2") != std::string_view::npos);
        // Should not contain old data
        REQUIRE(sv.find("49=A") == std::string_view::npos);
    }
}

// ============================================================================
// HeaderBuilder Overflow / Truncation Tests
// ============================================================================

TEST_CASE("HeaderBuilder overflow sets truncated flag", "[messages][header][regression]") {
    SECTION("default constructed is not truncated") {
        HeaderBuilder hdr;
        REQUIRE_FALSE(hdr.truncated());
    }

    SECTION("exactly full buffer does not truncate") {
        HeaderBuilder hdr;
        // MAX_HEADER_SIZE is 256. Fill with single-char raw appends via
        // append_field. Use a repeating small field to fill precisely.
        // Each call to begin_string("X") writes "8=X\x01" = 4 bytes.
        // 256 / 4 = 64 calls fills exactly 256 bytes.
        for (int i = 0; i < 64; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.size() == HeaderBuilder::MAX_HEADER_SIZE);
        REQUIRE_FALSE(hdr.truncated());
    }

    SECTION("one byte over sets truncated") {
        HeaderBuilder hdr;
        // Fill to exactly 256 bytes
        for (int i = 0; i < 64; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.size() == HeaderBuilder::MAX_HEADER_SIZE);
        REQUIRE_FALSE(hdr.truncated());

        // One more field triggers truncation
        hdr.begin_string("X");
        REQUIRE(hdr.truncated());
        REQUIRE(hdr.size() == HeaderBuilder::MAX_HEADER_SIZE);
    }

    SECTION("reset clears truncated flag") {
        HeaderBuilder hdr;
        // Trigger truncation
        for (int i = 0; i < 65; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.truncated());

        hdr.reset();
        REQUIRE_FALSE(hdr.truncated());
        REQUIRE(hdr.size() == 0);
    }

    SECTION("update_body_length is safe when placeholder was truncated") {
        HeaderBuilder hdr;
        // Fill buffer so that body_length_placeholder starts near the end.
        // Each begin_string("X") = "8=X\x01" = 4 bytes.
        // 60 calls = 240 bytes, leaving 16 bytes.
        for (int i = 0; i < 60; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.size() == 240);

        // body_length_placeholder writes "9=000000\x01" = 9 bytes.
        // 240 + 9 = 249, fits. But let's push further to force partial write.
        // 62 calls = 248 bytes. Placeholder needs 9 bytes: 248+9=257 > 256.
        hdr.reset();
        for (int i = 0; i < 62; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.size() == 248);
        hdr.body_length_placeholder();
        // Placeholder started at pos 248, only 8 bytes fit (248..255).
        // The 9th byte (SOH) was truncated.
        REQUIRE(hdr.truncated());

        // update_body_length must not write OOB.
        // body_length_pos_ = 248, needs positions [250..255] for the 6 digits.
        // 248 + 8 = 256 = MAX_HEADER_SIZE, so the guard passes and write is safe.
        hdr.update_body_length(42);
        // Verify the digits were written (positions 250..255 are in-bounds)
        auto data = hdr.data();
        REQUIRE(data.size() == HeaderBuilder::MAX_HEADER_SIZE);

        // Now test the case where placeholder itself is mostly out of bounds.
        hdr.reset();
        // 63 calls = 252 bytes. Placeholder at 252: only 4 bytes fit.
        // 252 + 8 = 260 > 256, guard blocks the write-back.
        for (int i = 0; i < 63; ++i) {
            hdr.begin_string("X");
        }
        REQUIRE(hdr.size() == 252);
        hdr.body_length_placeholder();
        REQUIRE(hdr.truncated());
        // This must be a no-op (guard: 252 + 8 = 260 > 256)
        hdr.update_body_length(99);
        // Buffer should not have been corrupted - size stays at MAX
        REQUIRE(hdr.size() == HeaderBuilder::MAX_HEADER_SIZE);
    }
}

// ============================================================================
// MessageAssembler Overflow / Truncation Tests
// ============================================================================

TEST_CASE("MessageAssembler overflow sets truncated flag", "[messages][assembler][regression]") {
    SECTION("default constructed is not truncated") {
        MessageAssembler asm_;
        REQUIRE_FALSE(asm_.truncated());
    }

    SECTION("normal message is not truncated") {
        MessageAssembler asm_;
        auto msg = asm_.start()
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "C")
            .field(tag::TargetCompID::value, "S")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215")
            .finish();

        REQUIRE_FALSE(asm_.truncated());
        REQUIRE(msg.size() > 0);
    }

    SECTION("overflow sets truncated flag") {
        MessageAssembler asm_;
        asm_.start();
        // MAX_MESSAGE_SIZE is 4096. Fill with large field values to exceed it.
        // A 200-char value + tag overhead ~ 206 bytes per field. 20 fields = ~4120.
        std::string big_value(200, 'A');
        for (int i = 0; i < 21; ++i) {
            asm_.field(58, big_value);  // tag 58 = Text
        }
        REQUIRE(asm_.truncated());
        REQUIRE(asm_.data().size() == MessageAssembler::MAX_MESSAGE_SIZE);
    }

    SECTION("reset clears truncated flag") {
        MessageAssembler asm_;
        asm_.start();
        std::string big_value(200, 'A');
        for (int i = 0; i < 21; ++i) {
            asm_.field(58, big_value);
        }
        REQUIRE(asm_.truncated());

        asm_.reset();
        REQUIRE_FALSE(asm_.truncated());
    }

    SECTION("start after truncation recovers for next message") {
        MessageAssembler asm_;

        // First message: trigger truncation
        asm_.start();
        std::string big_value(200, 'A');
        for (int i = 0; i < 21; ++i) {
            asm_.field(58, big_value);
        }
        REQUIRE(asm_.truncated());

        // Second message: start() must clear truncated so normal messages work
        auto msg = asm_.start()
            .field(tag::MsgType::value, '0')
            .field(tag::SenderCompID::value, "C")
            .field(tag::TargetCompID::value, "S")
            .field(tag::MsgSeqNum::value, static_cast<int64_t>(1))
            .field(tag::SendingTime::value, "20231215")
            .finish();

        REQUIRE_FALSE(asm_.truncated());
        REQUIRE(msg.size() > 0);

        // Verify the message is well-formed
        std::string_view sv{msg.data(), msg.size()};
        REQUIRE(sv.find("8=FIX.4.4") != std::string_view::npos);
        REQUIRE(sv.find("35=0") != std::string_view::npos);
    }
}
