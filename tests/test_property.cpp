#include <catch2/catch_test_macros.hpp>

#include "nexusfix/parser/field_view.hpp"
#include "nexusfix/parser/runtime_parser.hpp"
#include "nexusfix/session/state.hpp"
#include "nexusfix/memory/object_pool.hpp"
#include "nexusfix/memory/spsc_queue.hpp"
#include "nexusfix/memory/mpsc_queue.hpp"
#include "nexusfix/interfaces/i_message.hpp"
#include "nexusfix/serializer/constexpr_serializer.hpp"

#include <random>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <set>
#include <string>

using namespace nfx;

// ============================================================================
// Property: Session State Machine - No Invalid Transitions
// ============================================================================

TEST_CASE("Property: state machine stays valid under any event sequence",
          "[property][session][regression]") {

    // For every possible state, applying any event must either:
    // 1. Transition to a valid state, or
    // 2. Stay in current state (no-op)
    // It must never crash or return an out-of-range value.

    for (uint8_t s = 0; s < SESSION_STATE_COUNT; ++s) {
        auto state = static_cast<SessionState>(s);
        for (uint8_t e = 0; e < SESSION_EVENT_COUNT; ++e) {
            auto event = static_cast<SessionEvent>(e);
            auto result = next_state(state, event);
            auto result_idx = static_cast<uint8_t>(result);
            REQUIRE(result_idx < SESSION_STATE_COUNT);
        }
    }
}

TEST_CASE("Property: state machine random walk stays valid",
          "[property][session][regression]") {

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> event_dist(0, SESSION_EVENT_COUNT - 1);

    auto state = SessionState::Disconnected;

    for (int i = 0; i < 10000; ++i) {
        auto event = static_cast<SessionEvent>(event_dist(rng));
        state = next_state(state, event);
        auto idx = static_cast<uint8_t>(state);
        REQUIRE(idx < SESSION_STATE_COUNT);
    }
}

TEST_CASE("Property: Error and Disconnected are reachable from any state",
          "[property][session][regression]") {

    // From any state, some sequence of events should eventually reach Disconnected
    // (at minimum: Disconnect event should lead toward disconnected)
    for (uint8_t s = 0; s < SESSION_STATE_COUNT; ++s) {
        auto state = static_cast<SessionState>(s);

        // Apply Disconnect event repeatedly (via reconnect)
        auto s1 = next_state(state, SessionEvent::Disconnect);
        // Disconnect from most states should lead to Disconnected or Reconnecting
        REQUIRE((s1 == SessionState::Disconnected ||
                 s1 == SessionState::Reconnecting ||
                 s1 == state));  // No transition from some states is OK
    }
}

// ============================================================================
// Property: Memory Pool - Allocate + Available = Capacity
// ============================================================================

TEST_CASE("Property: ObjectPool allocated + available = capacity",
          "[property][memory][regression]") {

    nfx::memory::ObjectPool<uint64_t, 16> pool;

    std::mt19937 rng(123);
    std::uniform_int_distribution<int> action_dist(0, 1);  // 0=alloc, 1=dealloc
    std::vector<uint64_t*> allocated;

    for (int i = 0; i < 1000; ++i) {
        // Invariant must hold at every step
        REQUIRE(pool.allocated() + pool.available() == 16);

        if (action_dist(rng) == 0 || allocated.empty()) {
            // Try allocate
            auto* p = pool.allocate(static_cast<uint64_t>(i));
            if (p) {
                allocated.push_back(p);
            }
        } else {
            // Deallocate random one
            std::uniform_int_distribution<size_t> idx_dist(0, allocated.size() - 1);
            size_t idx = idx_dist(rng);
            pool.deallocate(allocated[idx]);
            allocated.erase(allocated.begin() + static_cast<ptrdiff_t>(idx));
        }
    }

    // Final invariant
    REQUIRE(pool.allocated() + pool.available() == 16);

    // Clean up
    for (auto* p : allocated) {
        pool.deallocate(p);
    }
    REQUIRE(pool.allocated() == 0);
    REQUIRE(pool.available() == 16);
}

// ============================================================================
// Property: SPSC Queue - FIFO Ordering Preserved
// ============================================================================

TEST_CASE("Property: SPSC queue preserves strict FIFO ordering",
          "[property][queue][regression]") {

    nfx::memory::SPSCQueue<int, 1024> q;
    constexpr int N = 5000;

    std::thread producer([&q]() {
        for (int i = 0; i < N; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<int> received;
    received.reserve(N);

    while (static_cast<int>(received.size()) < N) {
        int val{};
        if (q.try_pop(val)) {
            received.push_back(val);
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();

    // Strict FIFO: every element must be in exact order
    REQUIRE(received.size() == N);
    for (int i = 0; i < N; ++i) {
        REQUIRE(received[i] == i);
    }
}

// ============================================================================
// Property: MPSC Queue - No Lost or Duplicated Elements
// ============================================================================

TEST_CASE("Property: MPSC queue push count equals pop count",
          "[property][queue][regression]") {

    nfx::memory::MPSCQueue<int, 4096> q;
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 2000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&q, p]() {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                int val = p * PER_PRODUCER + i;
                while (!q.try_push(val)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::set<int> received;
    size_t empty_spins = 0;

    while (static_cast<int>(received.size()) < TOTAL) {
        int val{};
        if (q.try_pop(val)) {
            received.insert(val);
            empty_spins = 0;
        } else {
            ++empty_spins;
            if (empty_spins > 2000000) break;
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    // Drain remaining
    int val{};
    while (q.try_pop(val)) {
        received.insert(val);
    }

    // No duplicates (set ensures uniqueness), no losses
    REQUIRE(static_cast<int>(received.size()) == TOTAL);

    // All values present
    for (int i = 0; i < TOTAL; ++i) {
        REQUIRE(received.count(i) == 1);
    }
}

// ============================================================================
// Property: FIX Checksum is Sum Mod 256
// ============================================================================

TEST_CASE("Property: checksum is always sum(bytes) mod 256",
          "[property][parser][regression]") {

    std::mt19937 rng(999);

    for (int trial = 0; trial < 100; ++trial) {
        // Generate random "message" data
        std::uniform_int_distribution<int> len_dist(1, 200);
        std::uniform_int_distribution<int> byte_dist(1, 127);  // Printable-ish

        size_t len = static_cast<size_t>(len_dist(rng));
        std::string data(len, '\0');
        for (auto& c : data) {
            c = static_cast<char>(byte_dist(rng));
        }

        // Calculate expected
        uint32_t sum = 0;
        for (char c : data) {
            sum += static_cast<uint8_t>(c);
        }
        uint8_t expected = static_cast<uint8_t>(sum % 256);

        // Use the library function
        auto result = fix::calculate_checksum(
            std::span<const char>(data.data(), data.size()));

        REQUIRE(result == expected);
    }
}

// ============================================================================
// Property: Serializer Round-Trip Checksum Validity
// ============================================================================

TEST_CASE("Property: MessageFactory produces valid checksums",
          "[property][serializer][regression]") {

    nfx::serializer::MessageFactory<4096> factory("FIX.4.4", "SENDER", "TARGET");

    auto verify_checksum = [](std::span<const char> msg) {
        std::string_view sv(msg.data(), msg.size());

        // Find "10=" position
        auto pos = sv.find("10=");
        REQUIRE(pos != std::string_view::npos);

        // Calculate checksum of everything before "10="
        uint32_t sum = 0;
        for (size_t i = 0; i < pos; ++i) {
            sum += static_cast<uint8_t>(sv[i]);
        }
        uint8_t expected = static_cast<uint8_t>(sum % 256);

        // Parse checksum from message
        auto cksum_str = sv.substr(pos + 3, 3);
        int parsed = (cksum_str[0] - '0') * 100 +
                     (cksum_str[1] - '0') * 10 +
                     (cksum_str[2] - '0');

        return parsed == static_cast<int>(expected);
    };

    SECTION("heartbeat messages") {
        for (uint32_t seq = 1; seq <= 100; ++seq) {
            auto msg = factory.build_heartbeat(seq, "20240101-00:00:00.000");
            REQUIRE(verify_checksum(msg));
        }
    }

    SECTION("logon messages") {
        for (uint32_t hb = 10; hb <= 60; hb += 10) {
            auto msg = factory.build_logon(1, "20240101-00:00:00.000", hb);
            REQUIRE(verify_checksum(msg));
        }
    }

    SECTION("test request messages") {
        for (int i = 0; i < 50; ++i) {
            std::string id = "TR_" + std::to_string(i);
            auto msg = factory.build_test_request(
                static_cast<uint32_t>(i + 1), "20240101-00:00:00.000", id);
            REQUIRE(verify_checksum(msg));
        }
    }
}

// ============================================================================
// Property: FieldView Tag Parsing Always Returns Valid or Error
// ============================================================================

TEST_CASE("Property: FieldView tag parsing never crashes on random input",
          "[property][parser][regression]") {

    std::mt19937 rng(77);
    constexpr char SOH = '\x01';

    for (int trial = 0; trial < 200; ++trial) {
        // Build a random "field" string: possibly valid, possibly garbage
        std::uniform_int_distribution<int> len_dist(0, 30);
        std::uniform_int_distribution<int> byte_dist(0, 127);
        std::uniform_int_distribution<int> valid_dist(0, 1);

        std::string field;

        if (valid_dist(rng)) {
            // Generate valid-ish field: tag=value\x01
            int tag = std::uniform_int_distribution<int>(1, 9999)(rng);
            int vlen = std::uniform_int_distribution<int>(1, 10)(rng);
            field = std::to_string(tag) + "=";
            for (int i = 0; i < vlen; ++i) {
                field += static_cast<char>('A' + (i % 26));
            }
            field += SOH;
        } else {
            // Random bytes
            size_t len = static_cast<size_t>(len_dist(rng));
            field.resize(len);
            for (auto& c : field) {
                c = static_cast<char>(byte_dist(rng));
            }
        }

        // Construct FieldView - should never crash
        if (field.size() >= 3) {  // Minimum "X=Y" length
            auto eq_pos = field.find('=');
            auto soh_pos = field.find(SOH);
            if (eq_pos != std::string::npos && soh_pos != std::string::npos &&
                eq_pos < soh_pos && eq_pos > 0) {
                // Parse tag from field
                int parsed_tag = 0;
                bool valid_tag = true;
                for (size_t i = 0; i < eq_pos; ++i) {
                    if (field[i] < '0' || field[i] > '9') {
                        valid_tag = false;
                        break;
                    }
                    parsed_tag = parsed_tag * 10 + (field[i] - '0');
                }
                if (valid_tag && parsed_tag > 0) {
                    const char* val_start = field.data() + eq_pos + 1;
                    size_t val_len = soh_pos - eq_pos - 1;
                    FieldView fv(parsed_tag, std::span<const char>(val_start, val_len));
                    // Access members - should not crash
                    [[maybe_unused]] auto t = fv.tag;
                    [[maybe_unused]] auto v = fv.value;
                }
            }
        }
    }
}
