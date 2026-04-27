#pragma once

#include <span>
#include <string_view>

#include "nexusfix/types/tag.hpp"
#include "nexusfix/types/field_types.hpp"
#include "nexusfix/types/error.hpp"
#include "nexusfix/interfaces/i_message.hpp"
#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/messages/common/header.hpp"
#include "nexusfix/messages/common/trailer.hpp"

namespace nfx::fix42 {

// ============================================================================
// FIX 4.2 OrderCancelRequest Message (MsgType = F)
// ============================================================================

/// FIX 4.2 OrderCancelRequest message (35=F)
/// Structure matches FIX 4.4 with BeginString = FIX.4.2
struct OrderCancelRequest {
    static constexpr char MSG_TYPE = msg_type::OrderCancelRequest;
    static constexpr std::string_view BEGIN_STRING = fix::FIX_4_2;

    FixHeader header;
    std::string_view orig_cl_ord_id;  // Tag 41 - Required
    std::string_view cl_ord_id;       // Tag 11 - Required
    std::string_view symbol;          // Tag 55 - Required
    Side side;                        // Tag 54 - Required
    std::string_view transact_time;   // Tag 60 - Required
    Qty order_qty;                    // Tag 38 - Optional
    std::string_view order_id;        // Tag 37 - Optional
    std::string_view text;            // Tag 58 - Optional
    std::span<const char> raw_data;

    constexpr OrderCancelRequest() noexcept
        : header{}
        , orig_cl_ord_id{}
        , cl_ord_id{}
        , symbol{}
        , side{Side::Buy}
        , transact_time{}
        , order_qty{}
        , order_id{}
        , text{}
        , raw_data{} {}

    [[nodiscard]] constexpr std::span<const char> raw() const noexcept { return raw_data; }
    [[nodiscard]] constexpr uint32_t msg_seq_num() const noexcept { return header.msg_seq_num; }
    [[nodiscard]] constexpr std::string_view sender_comp_id() const noexcept { return header.sender_comp_id; }
    [[nodiscard]] constexpr std::string_view target_comp_id() const noexcept { return header.target_comp_id; }
    [[nodiscard]] constexpr std::string_view sending_time() const noexcept { return header.sending_time; }

    [[nodiscard]] static ParseResult<OrderCancelRequest> from_buffer(
        std::span<const char> buffer) noexcept
    {
        auto parsed = IndexedParser::parse(buffer);
        if (!parsed.has_value()) {
            return std::unexpected{parsed.error()};
        }

        auto& p = *parsed;

        if (p.msg_type() != MSG_TYPE) {
            return std::unexpected{ParseError{ParseErrorCode::InvalidMsgType}};
        }

        OrderCancelRequest msg;
        msg.raw_data = buffer;
        msg.header.begin_string = p.get_string(tag::BeginString::value);
        msg.header.msg_type = p.msg_type();
        msg.header.sender_comp_id = p.sender_comp_id();
        msg.header.target_comp_id = p.target_comp_id();
        msg.header.msg_seq_num = p.msg_seq_num();
        msg.header.sending_time = p.sending_time();

        msg.orig_cl_ord_id = p.get_string(tag::OrigClOrdID::value);
        if (msg.orig_cl_ord_id.empty()) {
            return std::unexpected{ParseError{ParseErrorCode::MissingRequiredField, tag::OrigClOrdID::value}};
        }

        msg.cl_ord_id = p.get_string(tag::ClOrdID::value);
        if (msg.cl_ord_id.empty()) {
            return std::unexpected{ParseError{ParseErrorCode::MissingRequiredField, tag::ClOrdID::value}};
        }

        msg.symbol = p.get_string(tag::Symbol::value);
        if (msg.symbol.empty()) {
            return std::unexpected{ParseError{ParseErrorCode::MissingRequiredField, tag::Symbol::value}};
        }

        char side_char = p.get_char(tag::Side::value);
        if (side_char == '\0') {
            return std::unexpected{ParseError{ParseErrorCode::MissingRequiredField, tag::Side::value}};
        }
        msg.side = static_cast<Side>(side_char);

        msg.transact_time = p.get_string(tag::TransactTime::value);
        if (msg.transact_time.empty()) {
            return std::unexpected{ParseError{ParseErrorCode::MissingRequiredField, tag::TransactTime::value}};
        }

        msg.order_qty = p.get_field(tag::OrderQty::value).as_qty();
        msg.order_id = p.get_string(tag::OrderID::value);
        msg.text = p.get_string(tag::Text::value);

        return msg;
    }

    class Builder {
    public:
        Builder& sender_comp_id(std::string_view v) noexcept { sender_comp_id_ = v; return *this; }
        Builder& target_comp_id(std::string_view v) noexcept { target_comp_id_ = v; return *this; }
        Builder& msg_seq_num(uint32_t v) noexcept { msg_seq_num_ = v; return *this; }
        Builder& sending_time(std::string_view v) noexcept { sending_time_ = v; return *this; }
        Builder& orig_cl_ord_id(std::string_view v) noexcept { orig_cl_ord_id_ = v; return *this; }
        Builder& cl_ord_id(std::string_view v) noexcept { cl_ord_id_ = v; return *this; }
        Builder& symbol(std::string_view v) noexcept { symbol_ = v; return *this; }
        Builder& side(Side v) noexcept { side_ = v; return *this; }
        Builder& transact_time(std::string_view v) noexcept { transact_time_ = v; return *this; }
        Builder& order_qty(Qty v) noexcept { order_qty_ = v; return *this; }
        Builder& order_id(std::string_view v) noexcept { order_id_ = v; return *this; }

        [[nodiscard]] std::span<const char> build(MessageAssembler& asm_) const noexcept {
            asm_.start(fix::FIX_4_2)
                .field(tag::MsgType::value, MSG_TYPE)
                .field(tag::SenderCompID::value, sender_comp_id_)
                .field(tag::TargetCompID::value, target_comp_id_)
                .field(tag::MsgSeqNum::value, static_cast<int64_t>(msg_seq_num_))
                .field(tag::SendingTime::value, sending_time_)
                .field(tag::OrigClOrdID::value, orig_cl_ord_id_)
                .field(tag::ClOrdID::value, cl_ord_id_)
                .field(tag::Symbol::value, symbol_)
                .field(tag::Side::value, static_cast<char>(side_))
                .field(tag::TransactTime::value, transact_time_);

            if (order_qty_.raw > 0) {
                asm_.field(tag::OrderQty::value, static_cast<int64_t>(order_qty_.whole()));
            }

            if (!order_id_.empty()) {
                asm_.field(tag::OrderID::value, order_id_);
            }

            return asm_.finish();
        }

    private:
        std::string_view sender_comp_id_;
        std::string_view target_comp_id_;
        uint32_t msg_seq_num_{1};
        std::string_view sending_time_;
        std::string_view orig_cl_ord_id_;
        std::string_view cl_ord_id_;
        std::string_view symbol_;
        Side side_{Side::Buy};
        std::string_view transact_time_;
        Qty order_qty_;
        std::string_view order_id_;
    };
};

} // namespace nfx::fix42
