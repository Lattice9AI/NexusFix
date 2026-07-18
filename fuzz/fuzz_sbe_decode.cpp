// fuzz_sbe_decode.cpp
//
// libFuzzer harness: raw bytes -> SBE decode path (TICKET_499 WI1).
//
// The SBE codecs are flyweights over caller-supplied buffers: MessageHeader,
// NewOrderSingleCodec, ExecutionReportCodec, and the templateId dispatch()
// switch. They sit on the IPC ingest path, so a peer (or a corrupted ring
// buffer) can hand them arbitrary bytes. The contract under fuzz:
//
//   1. dispatch() and isValid() must be safe on any input, any length.
//   2. Field accessors are only defined after isValid() returns true (the
//      documented usage); once valid, every accessor must stay in bounds.
//   3. Decode -> re-encode -> decode must be a fixed point: the second decode
//      yields exactly the fields of the first. A mismatch means the codec
//      pair is lossy and two processes can disagree about an order.
//
// Reads go through memcpy (read_int64/read_uint16) and the enums have fixed
// char underlying types, so ASan/UBSan findings here are real bugs, not
// alignment or enum-range artifacts.
//
// Build: clang++ -std=c++23 -fsanitize=fuzzer,address,undefined ...

#include "nexusfix/sbe/sbe.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using nfx::sbe::ExecutionReportCodec;
using nfx::sbe::MessageHeader;
using nfx::sbe::NewOrderSingleCodec;

// Read every header accessor; must be in-bounds whenever isValid() holds.
void exercise_header(const MessageHeader& header) {
    if (!header.isValid()) return;
    (void)header.blockLength();
    (void)header.templateId();
    (void)header.schemaId();
    (void)header.version();
    (void)header.messageSize();
    (void)header.bodyLength();
    (void)header.validateSchema();
}

// Decode all fields, re-encode them into a fresh buffer, decode again, and
// trap on any field that fails to round-trip.
void roundtrip_nos(const NewOrderSingleCodec& first) {
    alignas(8) char out[NewOrderSingleCodec::TOTAL_SIZE];
    auto enc = NewOrderSingleCodec::wrapForEncode(out, sizeof(out));
    enc.encodeHeader()
        .clOrdId(first.clOrdId())
        .symbol(first.symbol())
        .side(first.side())
        .ordType(first.ordType())
        .price(first.price())
        .orderQty(first.orderQty())
        .transactTime(first.transactTime());

    // Decoded views are <= field capacity by construction; encoding them back
    // must never report truncation.
    if (enc.truncated()) __builtin_trap();

    auto second = NewOrderSingleCodec::wrapForDecode(out, sizeof(out));
    if (!second.isValid()) __builtin_trap();
    if (second.clOrdId() != first.clOrdId()) __builtin_trap();
    if (second.symbol() != first.symbol()) __builtin_trap();
    if (second.side() != first.side()) __builtin_trap();
    if (second.ordType() != first.ordType()) __builtin_trap();
    if (second.price().raw != first.price().raw) __builtin_trap();
    if (second.orderQty().raw != first.orderQty().raw) __builtin_trap();
    if (second.transactTime().nanos != first.transactTime().nanos) __builtin_trap();
}

void roundtrip_er(const ExecutionReportCodec& first) {
    alignas(8) char out[ExecutionReportCodec::TOTAL_SIZE];
    auto enc = ExecutionReportCodec::wrapForEncode(out, sizeof(out));
    enc.encodeHeader()
        .orderId(first.orderId())
        .execId(first.execId())
        .clOrdId(first.clOrdId())
        .symbol(first.symbol())
        .side(first.side())
        .execType(first.execType())
        .ordStatus(first.ordStatus())
        .price(first.price())
        .orderQty(first.orderQty())
        .lastPx(first.lastPx())
        .lastQty(first.lastQty())
        .leavesQty(first.leavesQty())
        .cumQty(first.cumQty())
        .avgPx(first.avgPx())
        .transactTime(first.transactTime());

    if (enc.truncated()) __builtin_trap();

    auto second = ExecutionReportCodec::wrapForDecode(out, sizeof(out));
    if (!second.isValid()) __builtin_trap();
    if (second.orderId() != first.orderId()) __builtin_trap();
    if (second.execId() != first.execId()) __builtin_trap();
    if (second.clOrdId() != first.clOrdId()) __builtin_trap();
    if (second.symbol() != first.symbol()) __builtin_trap();
    if (second.side() != first.side()) __builtin_trap();
    if (second.execType() != first.execType()) __builtin_trap();
    if (second.ordStatus() != first.ordStatus()) __builtin_trap();
    if (second.price().raw != first.price().raw) __builtin_trap();
    if (second.orderQty().raw != first.orderQty().raw) __builtin_trap();
    if (second.lastPx().raw != first.lastPx().raw) __builtin_trap();
    if (second.lastQty().raw != first.lastQty().raw) __builtin_trap();
    if (second.leavesQty().raw != first.leavesQty().raw) __builtin_trap();
    if (second.cumQty().raw != first.cumQty().raw) __builtin_trap();
    if (second.avgPx().raw != first.avgPx().raw) __builtin_trap();
    if (second.transactTime().nanos != first.transactTime().nanos) __builtin_trap();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Copy into an owned allocation: the codec contract documents 8-aligned
    // buffers (alignas(8) at every encode site), and the exact-size heap block
    // gives ASan a redzone right past the last input byte.
    std::vector<char> buf(size);
    if (size > 0) std::memcpy(buf.data(), data, size);
    const char* bytes = buf.data();

    // Path 1: templateId dispatch over arbitrary bytes. The handler mirrors
    // documented usage: gate every accessor on isValid().
    nfx::sbe::dispatch(bytes, size, [](auto& codec) {
        using T = std::decay_t<decltype(codec)>;
        if constexpr (std::is_same_v<T, nfx::sbe::UnknownMessage>) {
            (void)codec.templateId;
            (void)codec.length;
        } else {
            exercise_header(codec.header());
            if (!codec.isValid()) return;
            if constexpr (std::is_same_v<T, NewOrderSingleCodec>) {
                roundtrip_nos(codec);
            } else {
                roundtrip_er(codec);
            }
        }
    });

    // Path 2: direct wrap with the "wrong" codec. A buffer carrying an
    // ExecutionReport templateId wrapped as NewOrderSingle (and vice versa)
    // must be rejected by isValid(), never mis-read.
    auto nos = NewOrderSingleCodec::wrapForDecode(bytes, size);
    if (nos.isValid()) roundtrip_nos(nos);
    auto er = ExecutionReportCodec::wrapForDecode(bytes, size);
    if (er.isValid()) roundtrip_er(er);

    // Path 3: bare header decode, including the null-buffer guard.
    exercise_header(MessageHeader::wrapForDecode(bytes, size));
    (void)MessageHeader::wrapForDecode(nullptr, size).isValid();

    return 0;
}
