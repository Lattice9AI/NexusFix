#pragma once

// =============================================================================
// NexusFIX SIMD Timestamp Parser
// TICKET_443: Parse FIX UTCTimestamp (YYYYMMDD-HH:MM:SS.mmm) with AVX2
// =============================================================================

#include <cstdint>
#include <optional>
#include <string_view>
#include <span>

#include "nexusfix/platform/platform.hpp"
#include "nexusfix/util/compiler.hpp"

// Reuse SIMD feature detection from simd_scanner.hpp
#if defined(NFX_HAS_SIMD) && NFX_HAS_SIMD
    #include <immintrin.h>
    #ifndef NFX_SIMD_AVAILABLE
        #define NFX_SIMD_AVAILABLE 1
    #endif
#else
    #ifndef NFX_SIMD_AVAILABLE
        #define NFX_SIMD_AVAILABLE 0
    #endif
#endif

namespace nfx {

// =============================================================================
// ParsedTimestamp
// =============================================================================

/// Parsed FIX UTCTimestamp components
struct ParsedTimestamp {
    uint16_t year;    // 2024
    uint8_t  month;   // 1-12
    uint8_t  day;     // 1-31
    uint8_t  hour;    // 0-23
    uint8_t  minute;  // 0-59
    uint8_t  second;  // 0-59
    uint16_t millis;  // 0-999
};

/// FIX UTCTimestamp format: YYYYMMDD-HH:MM:SS.mmm
/// Positions:                01234567890123456789 0
///                           YYYYMMDD-HH:MM:SS.mmm
inline constexpr size_t FIX_TIMESTAMP_LEN = 21;

// =============================================================================
// Scalar Implementation
// =============================================================================

namespace detail {

/// Validate separator characters at fixed positions
[[nodiscard]] NFX_HOT
inline bool validate_separators(const char* buf) noexcept {
    return buf[8] == '-' && buf[11] == ':' && buf[14] == ':' && buf[17] == '.';
}

/// Validate a character is a digit
[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

/// Convert digit char to int (no validation)
[[nodiscard]] constexpr int d(char c) noexcept {
    return c - '0';
}

/// Validate range of parsed components
[[nodiscard]] constexpr bool validate_ranges(const ParsedTimestamp& ts) noexcept {
    return ts.month >= 1 && ts.month <= 12 &&
           ts.day >= 1 && ts.day <= 31 &&
           ts.hour <= 23 &&
           ts.minute <= 59 &&
           ts.second <= 59 &&
           ts.millis <= 999;
}

}  // namespace detail

/// Scalar timestamp parser (unrolled digit extraction at fixed positions)
[[nodiscard]] NFX_HOT
inline std::optional<ParsedTimestamp> parse_timestamp_scalar(
    std::string_view input) noexcept
{
    if (input.size() < FIX_TIMESTAMP_LEN) [[unlikely]] {
        return std::nullopt;
    }

    const char* buf = input.data();

    // Validate separator positions
    if (!detail::validate_separators(buf)) [[unlikely]] {
        return std::nullopt;
    }

    // Validate all digit positions
    // Positions: 0-3 (year), 4-5 (month), 6-7 (day), 9-10 (hour),
    //            12-13 (minute), 15-16 (second), 18-20 (millis)
    constexpr int digit_positions[] = {0,1,2,3, 4,5, 6,7, 9,10, 12,13, 15,16, 18,19,20};
    for (int pos : digit_positions) {
        if (!detail::is_digit(buf[pos])) [[unlikely]] {
            return std::nullopt;
        }
    }

    ParsedTimestamp ts;
    ts.year   = static_cast<uint16_t>(detail::d(buf[0]) * 1000 + detail::d(buf[1]) * 100 +
                                       detail::d(buf[2]) * 10 + detail::d(buf[3]));
    ts.month  = static_cast<uint8_t>(detail::d(buf[4]) * 10 + detail::d(buf[5]));
    ts.day    = static_cast<uint8_t>(detail::d(buf[6]) * 10 + detail::d(buf[7]));
    ts.hour   = static_cast<uint8_t>(detail::d(buf[9]) * 10 + detail::d(buf[10]));
    ts.minute = static_cast<uint8_t>(detail::d(buf[12]) * 10 + detail::d(buf[13]));
    ts.second = static_cast<uint8_t>(detail::d(buf[15]) * 10 + detail::d(buf[16]));
    ts.millis = static_cast<uint16_t>(detail::d(buf[18]) * 100 + detail::d(buf[19]) * 10 +
                                       detail::d(buf[20]));

    if (!detail::validate_ranges(ts)) [[unlikely]] {
        return std::nullopt;
    }

    return ts;
}

// =============================================================================
// AVX2 Implementation
// =============================================================================

#if NFX_SIMD_AVAILABLE

/// AVX2-accelerated timestamp parser
/// Requires at least 32 bytes readable from input (21 timestamp + 11 padding).
/// In FIX messages, timestamp fields are always followed by SOH + more fields,
/// so 32 bytes are always readable from the start of the field value.
[[nodiscard]] NFX_HOT
inline std::optional<ParsedTimestamp> parse_timestamp_simd(
    std::string_view input) noexcept
{
    // Need 32 bytes for AVX2 load (21 timestamp + 11 readable padding)
    if (input.size() < 32) [[unlikely]] {
        return parse_timestamp_scalar(input);
    }

    const char* buf = input.data();

    // Load 32 bytes from input (21 timestamp chars + padding from surrounding message)
    __m256i raw = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));

    // Step 1: Validate separator characters at positions 8('-'), 11(':'), 14(':'), 17('.')
    // Create expected separator vector: positions 8,11,14,17 have separator chars,
    // all other positions are 0 (will be masked out)
    //
    // We check separators individually since there are only 4 of them
    // (cheaper than building a full comparison vector)
    if (buf[8] != '-' || buf[11] != ':' || buf[14] != ':' || buf[17] != '.') [[unlikely]] {
        return std::nullopt;
    }

    // Step 2: Subtract '0' from all bytes to get digit values
    const __m256i ascii_zero = _mm256_set1_epi8('0');
    __m256i digits = _mm256_sub_epi8(raw, ascii_zero);

    // Step 3: Validate all digit positions contain 0-9
    // After subtracting '0', valid digits are 0-9 (signed).
    // Check two conditions:
    //   a) digit < 0 (char was below '0') -> high bit set, detected by movemask
    //   b) digit > 9 (char was above '9') -> use signed cmpgt, then movemask
    const __m256i nine = _mm256_set1_epi8(9);
    __m256i gt_nine = _mm256_cmpgt_epi8(digits, nine);  // 0xFF where digit > 9 (signed)

    // Mask: only check digit positions (not separator positions 8,11,14,17)
    // Digit positions in first 21 bytes: 0-7, 9-10, 12-13, 15-16, 18-20
    // Bit mask: bits 0-7, 9, 10, 12, 13, 15, 16, 18, 19, 20 = 1
    //           bits 8, 11, 14, 17, 21-31 = 0
    // = 0x001DB6FF
    constexpr uint32_t digit_mask = 0x001DB6FF;

    uint32_t over_nine_bits = static_cast<uint32_t>(_mm256_movemask_epi8(gt_nine));
    // Check for negative values (chars < '0'): high bit is set after subtraction
    uint32_t negative_bits = static_cast<uint32_t>(_mm256_movemask_epi8(digits));

    // Any digit position that is either > 9 or < 0 is invalid
    if (((over_nine_bits | negative_bits) & digit_mask) != 0) [[unlikely]] {
        return std::nullopt;
    }

    // Step 4: Extract digit values using SIMD multiply-add
    // _mm256_maddubs_epi16 multiplies pairs of adjacent bytes and adds:
    // result[i] = a[2i] * b[2i] + a[2i+1] * b[2i+1]  (unsigned * signed -> 16-bit)
    //
    // We want to combine adjacent digits: tens*10 + ones
    // Multiplier pattern: [10, 1, 10, 1, ...] for digit pair positions
    //
    // Byte layout (positions 0-20):
    //   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
    //   Y  Y  Y  Y  M  M  D  D  -  H  H  :  M  M  :  S  S  .  m  m  m
    //
    // After maddubs with [10,1,...]:
    //   word[0] = d[0]*10+d[1]  = year_hi (0-99)
    //   word[1] = d[2]*10+d[3]  = year_lo (0-99)
    //   word[2] = d[4]*10+d[5]  = month
    //   word[3] = d[6]*10+d[7]  = day
    //   word[4] = sep*10+d[9]   = garbage (skip)
    //   word[5] = d[10]*10+sep  = garbage (skip)
    //   word[6] = d[12]*10+d[13]= minute
    //   word[7] = sep*10+d[15]  = garbage (skip)
    //   word[8] = d[16]*10+sep  = garbage (skip)
    //   word[9] = d[18]*10+d[19]= millis_hi (0-99)
    //   word[10]= d[20]*10+...  = garbage
    //
    // Separator positions contaminate adjacent pairs. We need to handle hour/second
    // and millis differently since their digits straddle separator positions.

    // For clean extraction, manually read from the digit vector
    // This is still fast since the validation was done in SIMD
    alignas(32) uint8_t dval[32];
    _mm256_store_si256(reinterpret_cast<__m256i*>(dval), digits);

    ParsedTimestamp ts;
    ts.year   = static_cast<uint16_t>(dval[0] * 1000 + dval[1] * 100 + dval[2] * 10 + dval[3]);
    ts.month  = static_cast<uint8_t>(dval[4] * 10 + dval[5]);
    ts.day    = static_cast<uint8_t>(dval[6] * 10 + dval[7]);
    ts.hour   = static_cast<uint8_t>(dval[9] * 10 + dval[10]);
    ts.minute = static_cast<uint8_t>(dval[12] * 10 + dval[13]);
    ts.second = static_cast<uint8_t>(dval[15] * 10 + dval[16]);
    ts.millis = static_cast<uint16_t>(dval[18] * 100 + dval[19] * 10 + dval[20]);

    if (!detail::validate_ranges(ts)) [[unlikely]] {
        return std::nullopt;
    }

    return ts;
}

#endif  // NFX_SIMD_AVAILABLE

// =============================================================================
// Unified API (auto-selects SIMD or scalar)
// =============================================================================

/// Parse FIX UTCTimestamp (auto-selects best implementation)
[[nodiscard]] NFX_HOT
inline std::optional<ParsedTimestamp> parse_timestamp(
    std::string_view input) noexcept
{
#if NFX_SIMD_AVAILABLE
    return parse_timestamp_simd(input);
#else
    return parse_timestamp_scalar(input);
#endif
}

// =============================================================================
// Epoch Conversion
// =============================================================================

/// Convert ParsedTimestamp to milliseconds since Unix epoch
/// Uses a branchless days-from-civil algorithm (Howard Hinnant)
[[nodiscard]] constexpr uint64_t to_epoch_ms(const ParsedTimestamp& ts) noexcept {
    // Civil date to days since epoch (Howard Hinnant's algorithm)
    // https://howardhinnant.github.io/date_algorithms.html#days_from_civil
    int y = ts.year;
    unsigned m = ts.month;
    unsigned d = ts.day;

    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);           // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
    const int days = era * 146097 + static_cast<int>(doe) - 719468;

    // Convert to milliseconds
    uint64_t epoch_ms = static_cast<uint64_t>(days) * 86400000ULL;
    epoch_ms += static_cast<uint64_t>(ts.hour) * 3600000ULL;
    epoch_ms += static_cast<uint64_t>(ts.minute) * 60000ULL;
    epoch_ms += static_cast<uint64_t>(ts.second) * 1000ULL;
    epoch_ms += ts.millis;

    return epoch_ms;
}

}  // namespace nfx
