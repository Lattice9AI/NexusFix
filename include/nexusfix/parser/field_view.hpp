#pragma once

#include <span>
#include <string_view>
#include <cstdint>
#include <charconv>
#include <optional>
#include <limits>
#include <bitset>
#include <variant>
#include <type_traits>

#include "nexusfix/platform/platform.hpp"
#include "nexusfix/util/compiler.hpp"
#include "nexusfix/types/tag.hpp"
#include "nexusfix/types/field_types.hpp"
#include "nexusfix/interfaces/i_message.hpp"
#include "nexusfix/memory/buffer_pool.hpp"  // For CACHE_LINE_SIZE
#include "nexusfix/parser/timestamp_parser.hpp"

namespace nfx {

// ============================================================================
// Zero-Copy Field View
// ============================================================================

/// View into a FIX field without copying data
struct FieldView {
    int tag;                        // Field tag number
    std::span<const char> value;    // Points into original buffer

    constexpr FieldView() noexcept : tag{0}, value{} {}

    constexpr FieldView(int t, std::span<const char> v) noexcept
        : tag{t}, value{v} {}

    constexpr FieldView(int t, const char* data, size_t len) noexcept
        : tag{t}, value{data, len} {}

    // ========================================================================
    // Zero-copy Value Accessors
    // ========================================================================

    /// Get value as string_view (zero-copy)
    [[nodiscard]] constexpr std::string_view as_string() const noexcept {
        return std::string_view{value.data(), value.size()};
    }

    /// Get single character value
    [[nodiscard]] constexpr char as_char() const noexcept {
        return value.empty() ? '\0' : value[0];
    }

    /// Get value as boolean (Y/N)
    [[nodiscard]] constexpr bool as_bool() const noexcept {
        return !value.empty() && value[0] == 'Y';
    }

    /// Parse value as integer
    [[nodiscard]] constexpr std::optional<int64_t> as_int() const noexcept {
        if (value.empty()) [[unlikely]] return std::nullopt;
        NFX_ASSUME(!value.empty());

        int64_t result = 0;
        bool negative = false;
        size_t i = 0;

        if (value[0] == '-') [[unlikely]] {
            negative = true;
            i = 1;
        }

        for (; i < value.size(); ++i) [[likely]] {
            char c = value[i];
            if (c < '0' || c > '9') [[unlikely]] return std::nullopt;
            result = result * 10 + (c - '0');
        }

        return negative ? -result : result;
    }

    /// Parse value as unsigned integer
    [[nodiscard]] constexpr std::optional<uint64_t> as_uint() const noexcept {
        if (value.empty()) [[unlikely]] return std::nullopt;
        NFX_ASSUME(!value.empty());

        uint64_t result = 0;
        for (char c : value) [[likely]] {
            if (c < '0' || c > '9') [[unlikely]] return std::nullopt;
            result = result * 10 + (c - '0');
        }
        return result;
    }

    /// Parse value as fixed-point price
    [[nodiscard]] constexpr FixedPrice as_price() const noexcept {
        return FixedPrice::from_string(as_string());
    }

    /// Parse value as quantity
    [[nodiscard]] constexpr Qty as_qty() const noexcept {
        return Qty::from_string(as_string());
    }

    /// Parse value as Side enum
    [[nodiscard]] constexpr std::optional<Side> as_side() const noexcept {
        if (value.empty()) return std::nullopt;
        char c = value[0];
        if (c >= '1' && c <= '9') {
            return static_cast<Side>(c);
        }
        return std::nullopt;
    }

    /// Parse value as OrdType enum
    [[nodiscard]] constexpr std::optional<OrdType> as_ord_type() const noexcept {
        if (value.empty()) return std::nullopt;
        return static_cast<OrdType>(value[0]);
    }

    /// Parse value as OrdStatus enum
    [[nodiscard]] constexpr std::optional<OrdStatus> as_ord_status() const noexcept {
        if (value.empty()) return std::nullopt;
        return static_cast<OrdStatus>(value[0]);
    }

    /// Parse value as ExecType enum
    [[nodiscard]] constexpr std::optional<ExecType> as_exec_type() const noexcept {
        if (value.empty()) return std::nullopt;
        return static_cast<ExecType>(value[0]);
    }

    /// Parse value as TimeInForce enum
    [[nodiscard]] constexpr std::optional<TimeInForce> as_time_in_force() const noexcept {
        if (value.empty()) return std::nullopt;
        return static_cast<TimeInForce>(value[0]);
    }

    /// Parse value as FIX UTCTimestamp (YYYYMMDD-HH:MM:SS.mmm)
    [[nodiscard]] std::optional<ParsedTimestamp> as_timestamp() const noexcept {
        return parse_timestamp(as_string());
    }

    // ========================================================================
    // Validation
    // ========================================================================

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return tag > 0;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return value.empty();
    }

    [[nodiscard]] constexpr size_t size() const noexcept {
        return value.size();
    }
};

// ============================================================================
// Field Iterator (for parsing)
// ============================================================================

/// Iterator over FIX fields in a buffer
class FieldIterator {
public:
    constexpr FieldIterator() noexcept
        : data_{}, pos_{0}, last_error_{ParseErrorCode::None} {}

    constexpr explicit FieldIterator(std::span<const char> data) noexcept
        : data_{data}, pos_{0}, last_error_{ParseErrorCode::None} {}

    /// Get next field (returns invalid FieldView if no more fields)
    [[nodiscard]] NFX_HOT constexpr FieldView next() noexcept {
        last_error_ = ParseErrorCode::None;
        if (pos_ >= data_.size()) [[unlikely]] {
            return FieldView{};
        }
        NFX_ASSUME(pos_ < data_.size());

        const char* __restrict ptr = data_.data();

        // Parse tag number
        int tag = 0;
        while (pos_ < data_.size() && ptr[pos_] != fix::EQUALS) [[likely]] {
            char c = ptr[pos_];
            if (c < '0' || c > '9') [[unlikely]] {
                last_error_ = ParseErrorCode::InvalidTagNumber;
                return FieldView{};  // Invalid tag
            }
            tag = tag * 10 + (c - '0');
            ++pos_;
        }

        if (pos_ >= data_.size() || ptr[pos_] != fix::EQUALS) [[unlikely]] {
            last_error_ = ParseErrorCode::InvalidFieldFormat;
            return FieldView{};  // Missing '='
        }
        ++pos_;  // Skip '='

        // Find value (until SOH)
        size_t value_start = pos_;
        while (pos_ < data_.size() && ptr[pos_] != fix::SOH) [[likely]] {
            ++pos_;
        }

        if (pos_ >= data_.size()) [[unlikely]] {
            last_error_ = ParseErrorCode::UnterminatedField;
            return FieldView{};
        }

        size_t value_len = pos_ - value_start;

        ++pos_;  // Skip SOH

        return FieldView{tag, std::span<const char>{ptr + value_start, value_len}};
    }

    /// Check if more fields available
    [[nodiscard]] constexpr bool has_next() const noexcept {
        return pos_ < data_.size();
    }

    /// Get current position in buffer
    [[nodiscard]] constexpr size_t position() const noexcept {
        return pos_;
    }

    /// Get the error code for the most recent parse failure
    [[nodiscard]] constexpr ParseErrorCode last_error() const noexcept {
        return last_error_;
    }

    /// Reset to beginning
    constexpr void reset() noexcept {
        pos_ = 0;
    }

    /// Skip to a specific position
    constexpr void seek(size_t pos) noexcept {
        pos_ = pos < data_.size() ? pos : data_.size();
    }

private:
    std::span<const char> data_;
    size_t pos_;
    ParseErrorCode last_error_;
};

// ============================================================================
// Field Lookup Table (for fast tag access)
// ============================================================================

/// Fixed-size lookup table for common tags (O(1) access)
/// Flat array for tags 0..MaxTag-1, inline overflow for tags >= MaxTag.
/// Aligned to cache line boundary for optimal memory access
///
/// StrictMode (default false): when enabled, detects duplicate tags and
/// returns ParseErrorCode::DuplicateTag from set(). Pass allow_dup=true
/// for tags inside an active repeating group context (caller responsibility).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)  // structure was padded due to alignment specifier
#endif
template <size_t MaxTag = 512, size_t MaxOverflow = 8, bool StrictMode = false>
class alignas(CACHE_LINE_SIZE) FieldTable {
public:
    constexpr FieldTable() noexcept {
        for (auto& entry : entries_) {
            entry = FieldView{};
        }
    }

    /// Set field value
    /// Returns ParseError with code None on success, DuplicateTag (strict) or
    /// OverflowExhausted on failure.
    /// @param allow_dup When true (e.g. inside repeating group), skip duplicate check
    [[nodiscard]] constexpr ParseError set(int tag, std::span<const char> value,
                                           bool allow_dup = false) noexcept {
        if (tag > 0 && static_cast<size_t>(tag) < MaxTag) [[likely]] {
            if constexpr (StrictMode) {
                if (!allow_dup && seen_tags_.test(static_cast<size_t>(tag))) {
                    return ParseError{ParseErrorCode::DuplicateTag, tag};
                }
                seen_tags_.set(static_cast<size_t>(tag));
            }
            entries_[tag] = FieldView{tag, value};
            return ParseError{};
        }
        if (tag > 0 && overflow_count_ < MaxOverflow) {
            if constexpr (StrictMode) {
                if (!allow_dup) {
                    for (size_t i = 0; i < overflow_count_; ++i) {
                        if (overflow_[i].tag == tag) {
                            return ParseError{ParseErrorCode::DuplicateTag, tag};
                        }
                    }
                }
            }
            overflow_[overflow_count_++] = FieldView{tag, value};
            return ParseError{};
        }
        if (tag <= 0) return ParseError{};  // tag <= 0 is not a real field, not an error
        return ParseError{ParseErrorCode::OverflowExhausted, tag};
    }

    /// Get field value (O(1) for tags < MaxTag, linear scan for overflow)
    [[nodiscard]] NFX_HOT constexpr FieldView get(int tag) const noexcept {
        if (tag > 0 && static_cast<size_t>(tag) < MaxTag) [[likely]] {
            return entries_[tag];
        }
        for (size_t i = 0; i < overflow_count_; ++i) {
            if (overflow_[i].tag == tag) return overflow_[i];
        }
        return FieldView{};
    }

    /// Check if tag exists
    [[nodiscard]] NFX_HOT constexpr bool has(int tag) const noexcept {
        if (tag > 0 && static_cast<size_t>(tag) < MaxTag) [[likely]] {
            return entries_[tag].is_valid();
        }
        for (size_t i = 0; i < overflow_count_; ++i) {
            if (overflow_[i].tag == tag) return true;
        }
        return false;
    }

    /// Get string value for tag
    [[nodiscard]] NFX_HOT constexpr std::string_view get_string(int tag) const noexcept {
        return get(tag).as_string();
    }

    /// Get int value for tag
    [[nodiscard]] NFX_HOT constexpr std::optional<int64_t> get_int(int tag) const noexcept {
        return get(tag).as_int();
    }

    /// Get char value for tag
    [[nodiscard]] NFX_HOT constexpr char get_char(int tag) const noexcept {
        return get(tag).as_char();
    }

    /// Clear all entries
    constexpr void clear() noexcept {
        for (auto& entry : entries_) {
            entry = FieldView{};
        }
        overflow_count_ = 0;
        if constexpr (StrictMode) {
            seen_tags_.reset();
        }
    }

private:
    std::array<FieldView, MaxTag> entries_;
    std::array<FieldView, MaxOverflow> overflow_{};
    size_t overflow_count_{0};
    [[no_unique_address]] std::conditional_t<StrictMode, std::bitset<MaxTag>, std::monostate> seen_tags_{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Verify constexpr bitset support enables compile-time StrictMode FieldTable
#if defined(__cpp_lib_constexpr_bitset) && __cpp_lib_constexpr_bitset >= 202207L
static_assert([] {
    FieldTable<64, 4, true> table;
    auto err = table.set(1, {});
    return err.code == ParseErrorCode::None;
}(), "StrictMode FieldTable must be usable in constexpr context with constexpr bitset");
#endif

// Static assertion: FieldTable should be cache-line aligned
static_assert(alignof(FieldTable<512, 8, false>) >= CACHE_LINE_SIZE,
    "FieldTable must be cache-line aligned for optimal memory access");

static_assert(alignof(FieldTable<512, 8, true>) >= CACHE_LINE_SIZE,
    "FieldTable (strict) must be cache-line aligned for optimal memory access");

// StrictMode=false must have zero overhead vs original layout
static_assert(sizeof(FieldTable<512, 8, false>) == sizeof(FieldTable<512, 8>),
    "Non-strict FieldTable must have no size overhead");

// ============================================================================
// Utility Functions
// ============================================================================

/// Parse single field at specific position
[[nodiscard]] NFX_HOT
constexpr FieldView parse_field_at(
    std::span<const char> data,
    size_t pos) noexcept
{
    FieldIterator iter{data};
    iter.seek(pos);
    return iter.next();
}

/// Find field by tag (linear scan)
[[nodiscard]] NFX_HOT
constexpr FieldView find_field(
    std::span<const char> data,
    int target_tag) noexcept
{
    FieldIterator iter{data};
    while (iter.has_next()) [[likely]] {
        FieldView field = iter.next();
        if (field.tag == target_tag) [[unlikely]] {
            return field;
        }
    }
    return FieldView{};
}

// ============================================================================
// Static Assertions for Struct Layout
// ============================================================================

// FieldView should be compact (typically 24 bytes: 4 for tag + padding + 16 for span)
static_assert(sizeof(FieldView) <= 32,
    "FieldView should be compact for cache efficiency");

// FieldIterator should be compact
static_assert(sizeof(FieldIterator) <= 32,
    "FieldIterator should be compact for stack allocation");

} // namespace nfx
