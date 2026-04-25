#include <catch2/catch_test_macros.hpp>

#include "nexusfix/memory/spsc_queue.hpp"  // CACHE_LINE_SIZE definition
#include "nexusfix/memory/mpsc_queue.hpp"

#include <thread>
#include <vector>
#include <set>
#include <algorithm>
#include <numeric>

using namespace nfx::memory;

// ============================================================================
// MPSCQueue Basic Tests
// ============================================================================

TEST_CASE("MPSCQueue basic operations", "[queue][mpsc][regression]") {
    MPSCQueue<int, 8> q;

    SECTION("newly created queue is empty") {
        REQUIRE(q.empty());
        REQUIRE(q.size_approx() == 0);
        REQUIRE_FALSE(q.full());
        REQUIRE(MPSCQueue<int, 8>::capacity() == 8);
    }

    SECTION("push and pop single element") {
        REQUIRE(q.try_push(42));
        REQUIRE_FALSE(q.empty());
        REQUIRE(q.size_approx() == 1);

        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == 42);
        REQUIRE(q.empty());
    }

    SECTION("push and pop multiple elements preserves FIFO order") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(q.try_push(i * 10));
        }
        REQUIRE(q.size_approx() == 8);

        for (int i = 0; i < 8; ++i) {
            int val{};
            REQUIRE(q.try_pop(val));
            REQUIRE(val == i * 10);
        }
        REQUIRE(q.empty());
    }

    SECTION("pop from empty queue returns nullopt") {
        auto val = q.try_pop();
        REQUIRE_FALSE(val.has_value());
    }

    SECTION("pop with output param from empty queue returns false") {
        int val{};
        REQUIRE_FALSE(q.try_pop(val));
    }

    SECTION("push to full queue returns false") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(q.try_push(i));
        }
        REQUIRE(q.full());
        REQUIRE_FALSE(q.try_push(999));
    }

    SECTION("push after pop from full queue succeeds") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(q.try_push(i));
        }
        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(q.try_push(100));
    }
}

TEST_CASE("MPSCQueue move semantics", "[queue][mpsc][regression]") {
    MPSCQueue<std::string, 4> q;

    SECTION("push rvalue") {
        std::string s = "hello";
        REQUIRE(q.try_push(std::move(s)));

        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == "hello");
    }
}

TEST_CASE("MPSCQueue emplace", "[queue][mpsc][regression]") {
    struct Pair {
        int a;
        double b;
        Pair() : a(0), b(0.0) {}
        Pair(int x, double y) : a(x), b(y) {}
    };

    MPSCQueue<Pair, 4> q;

    SECTION("emplace constructs in-place") {
        REQUIRE(q.try_emplace(42, 3.14));

        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(val->a == 42);
        REQUIRE(val->b == 3.14);
    }
}

TEST_CASE("MPSCQueue batch pop", "[queue][mpsc][regression]") {
    MPSCQueue<int, 16> q;

    for (int i = 0; i < 10; ++i) {
        REQUIRE(q.try_push(i));
    }

    std::vector<int> batch;
    size_t count = q.try_pop_batch(std::back_inserter(batch), 5);
    REQUIRE(count == 5);
    REQUIRE(batch.size() == 5);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(batch[i] == i);
    }

    // Pop remaining
    count = q.try_pop_batch(std::back_inserter(batch), 100);
    REQUIRE(count == 5);
    REQUIRE(batch.size() == 10);
}

TEST_CASE("MPSCQueue blocking push and pop", "[queue][mpsc][regression]") {
    MPSCQueue<int, 4> q;

    // push() should succeed immediately on non-full queue
    q.push(1);
    q.push(2);

    // pop() should succeed immediately on non-empty queue
    int val = q.pop();
    REQUIRE(val == 1);
    val = q.pop();
    REQUIRE(val == 2);
}

TEST_CASE("MPSCQueue with different wait strategies", "[queue][mpsc][regression]") {
    SECTION("YieldingWait") {
        MPSCQueue<int, 4, YieldingWait> q;
        REQUIRE(q.try_push(1));
        auto v = q.try_pop();
        REQUIRE(v.has_value());
        REQUIRE(*v == 1);
    }

    SECTION("SleepingWait") {
        MPSCQueue<int, 4, SleepingWait<>> q;
        REQUIRE(q.try_push(2));
        auto v = q.try_pop();
        REQUIRE(v.has_value());
        REQUIRE(*v == 2);
    }

    SECTION("BackoffWait") {
        MPSCQueue<int, 4, BackoffWait<>> q;
        REQUIRE(q.try_push(3));
        auto v = q.try_pop();
        REQUIRE(v.has_value());
        REQUIRE(*v == 3);
    }
}

// ============================================================================
// MPSCQueue Multi-threaded Tests
// ============================================================================

TEST_CASE("MPSCQueue multi-producer single-consumer", "[queue][mpsc][regression]") {
    constexpr size_t NUM_PRODUCERS = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 1000;
    MPSCQueue<int, 8192> q;

    std::vector<std::thread> producers;
    for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&q, p]() {
            for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int val = static_cast<int>(p * ITEMS_PER_PRODUCER + i);
                while (!q.try_push(val)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Consumer collects all values
    std::vector<int> received;
    received.reserve(NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    size_t total = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    size_t empty_spins = 0;

    while (received.size() < total) {
        int val{};
        if (q.try_pop(val)) {
            received.push_back(val);
            empty_spins = 0;
        } else {
            ++empty_spins;
            if (empty_spins > 1000000) {
                // Failsafe: producers must have finished
                break;
            }
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    // Drain any remaining
    int val{};
    while (q.try_pop(val)) {
        received.push_back(val);
    }

    REQUIRE(received.size() == total);

    // All values should be present (no duplicates, no losses)
    std::sort(received.begin(), received.end());
    std::vector<int> expected(total);
    std::iota(expected.begin(), expected.end(), 0);
    REQUIRE(received == expected);
}

// ============================================================================
// SimpleMPSCQueue Tests
// ============================================================================

TEST_CASE("SimpleMPSCQueue basic operations", "[queue][mpsc][regression]") {
    SimpleMPSCQueue<int, 8> q;

    SECTION("empty queue") {
        REQUIRE(q.empty());
        REQUIRE(q.size_approx() == 0);
        REQUIRE(SimpleMPSCQueue<int, 8>::capacity() == 8);
    }

    SECTION("push and pop") {
        REQUIRE(q.try_push(10));
        REQUIRE(q.try_push(20));
        REQUIRE_FALSE(q.empty());

        int val{};
        REQUIRE(q.try_pop(val));
        REQUIRE(val == 10);
        REQUIRE(q.try_pop(val));
        REQUIRE(val == 20);
        REQUIRE(q.empty());
    }

    SECTION("pop from empty returns false") {
        int val{};
        REQUIRE_FALSE(q.try_pop(val));
    }
}

TEST_CASE("SimpleMPSCQueue multi-producer", "[queue][mpsc][regression]") {
    constexpr size_t NUM_PRODUCERS = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 500;
    SimpleMPSCQueue<int, 4096> q;

    std::vector<std::thread> producers;
    for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&q, p]() {
            for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int val = static_cast<int>(p * ITEMS_PER_PRODUCER + i);
                while (!q.try_push(val)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::set<int> received;
    size_t total = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    size_t empty_spins = 0;

    while (received.size() < total) {
        int val{};
        if (q.try_pop(val)) {
            received.insert(val);
            empty_spins = 0;
        } else {
            ++empty_spins;
            if (empty_spins > 1000000) break;
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

    REQUIRE(received.size() == total);
}
