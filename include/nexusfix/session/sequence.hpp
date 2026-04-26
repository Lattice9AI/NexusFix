#pragma once

#include <cstdint>
#include <atomic>

#include "nexusfix/types/field_types.hpp"
#include "nexusfix/types/error.hpp"

namespace nfx {

// ============================================================================
// Sequence Number Manager
// ============================================================================

/// Manages inbound and outbound sequence numbers for a FIX session
class SequenceManager {
public:
    static constexpr uint32_t INITIAL_SEQ_NUM = 1;
    static constexpr uint32_t MAX_SEQ_NUM = SeqNum::MAX_VALUE;

    /// Result of sequence validation
    enum class SequenceResult : uint8_t {
        Ok,           // Sequence number matches expected
        GapDetected,  // Sequence number higher than expected
        TooLow        // Sequence number lower than expected (duplicate)
    };

    SequenceManager() noexcept
        : next_outbound_{INITIAL_SEQ_NUM}
        , expected_inbound_{INITIAL_SEQ_NUM} {}

    // ========================================================================
    // Outbound Sequence Numbers
    // ========================================================================

    /// Get next outbound sequence number and increment
    [[nodiscard]] uint32_t next_outbound() noexcept {
        uint32_t seq = next_outbound_.load(std::memory_order_relaxed);
        uint32_t next = (seq >= MAX_SEQ_NUM) ? INITIAL_SEQ_NUM : seq + 1;
        next_outbound_.store(next, std::memory_order_relaxed);
        return seq;
    }

    /// Get current outbound sequence number (without incrementing)
    [[nodiscard]] uint32_t current_outbound() const noexcept {
        return next_outbound_.load(std::memory_order_relaxed);
    }

    /// Set next outbound sequence number (for reset)
    void set_outbound(uint32_t seq) noexcept {
        next_outbound_.store(seq, std::memory_order_relaxed);
    }

    /// Rollback outbound sequence number after a failed send
    void rollback_outbound() noexcept {
        uint32_t current = next_outbound_.load(std::memory_order_relaxed);
        uint32_t prev = (current <= INITIAL_SEQ_NUM) ? MAX_SEQ_NUM : current - 1;
        next_outbound_.store(prev, std::memory_order_relaxed);
    }

    // ========================================================================
    // Inbound Sequence Numbers
    // ========================================================================

    /// Validate and process incoming sequence number
    /// Returns true if sequence is expected, false if gap detected
    [[nodiscard]] SequenceResult validate_inbound(uint32_t received) noexcept {
        uint32_t expected = expected_inbound_.load(std::memory_order_relaxed);

        if (received == expected) {
            // Normal case - sequence matches
            expected_inbound_.store(expected + 1, std::memory_order_relaxed);
            return SequenceResult::Ok;
        }

        if (received > expected) {
            // Gap detected - need resend request
            return SequenceResult::GapDetected;
        }

        // received < expected
        // Possible duplicate or already processed
        return SequenceResult::TooLow;
    }

    /// Get expected inbound sequence number
    [[nodiscard]] uint32_t expected_inbound() const noexcept {
        return expected_inbound_.load(std::memory_order_relaxed);
    }

    /// Set expected inbound sequence number (for reset or gap fill)
    void set_inbound(uint32_t seq) noexcept {
        expected_inbound_.store(seq, std::memory_order_relaxed);
    }

    // ========================================================================
    // Session Control
    // ========================================================================

    /// Reset both sequence numbers to initial values
    void reset() noexcept {
        next_outbound_.store(INITIAL_SEQ_NUM, std::memory_order_relaxed);
        expected_inbound_.store(INITIAL_SEQ_NUM, std::memory_order_relaxed);
    }

    /// Check if sequence gap exists
    [[nodiscard]] bool has_gap(uint32_t received) const noexcept {
        return received > expected_inbound_.load(std::memory_order_relaxed);
    }

    /// Get gap range (begin, end) for resend request
    [[nodiscard]] std::pair<uint32_t, uint32_t> gap_range(uint32_t received) const noexcept {
        uint32_t expected = expected_inbound_.load(std::memory_order_relaxed);
        if (received > expected) {
            return {expected, received - 1};
        }
        return {0, 0};  // No gap
    }

private:
    std::atomic<uint32_t> next_outbound_;
    std::atomic<uint32_t> expected_inbound_;
};

// ============================================================================
// Sequence Gap Tracker
// ============================================================================

/// Tracks sequence number gaps for resend handling
class GapTracker {
public:
    static constexpr size_t MAX_GAPS = 32;

    struct Gap {
        uint32_t begin;
        uint32_t end;
    };

    GapTracker() noexcept : count_{0} {}

    /// Add a gap to track
    bool add_gap(uint32_t begin, uint32_t end) noexcept {
        if (count_ >= MAX_GAPS) return false;
        gaps_[count_++] = {begin, end};
        return true;
    }

    /// Mark a sequence number as filled
    void fill(uint32_t seq_num) noexcept {
        for (size_t i = 0; i < count_; ) {
            auto& gap = gaps_[i];
            if (seq_num >= gap.begin && seq_num <= gap.end) {
                if (seq_num == gap.begin && seq_num == gap.end) {
                    // Gap completely filled, remove
                    remove_gap(i);
                } else if (seq_num == gap.begin) {
                    ++gap.begin;
                    ++i;
                } else if (seq_num == gap.end) {
                    --gap.end;
                    ++i;
                } else {
                    // Split gap
                    if (count_ < MAX_GAPS) {
                        gaps_[count_++] = {seq_num + 1, gap.end};
                        gap.end = seq_num - 1;
                    }
                    ++i;
                }
            } else {
                ++i;
            }
        }
    }

    /// Check if any gaps remain
    [[nodiscard]] bool has_gaps() const noexcept {
        return count_ > 0;
    }

    /// Get number of gaps
    [[nodiscard]] size_t gap_count() const noexcept {
        return count_;
    }

    /// Get gap at index
    [[nodiscard]] const Gap* get_gap(size_t idx) const noexcept {
        return idx < count_ ? &gaps_[idx] : nullptr;
    }

    /// Clear all gaps
    void clear() noexcept {
        count_ = 0;
    }

private:
    void remove_gap(size_t idx) noexcept {
        if (idx < count_ - 1) {
            gaps_[idx] = gaps_[count_ - 1];
        }
        --count_;
    }

    std::array<Gap, MAX_GAPS> gaps_;
    size_t count_;
};

} // namespace nfx
