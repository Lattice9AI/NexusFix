#include <catch2/catch_test_macros.hpp>

#include "nexusfix/memory/spsc_queue.hpp"

#include <thread>
#include <vector>
#include <numeric>

using namespace nfx::memory;

// ============================================================================
// SPSCQueue Basic Tests
// ============================================================================

TEST_CASE("SPSCQueue basic operations", "[queue][spsc][regression]") {
    SPSCQueue<int, 8> q;

    SECTION("newly created queue is empty") {
        REQUIRE(q.empty());
        REQUIRE(q.size_approx() == 0);
        REQUIRE_FALSE(q.full());
        // Capacity is N-1 due to one slot reserved
        REQUIRE(SPSCQueue<int, 8>::capacity() == 7);
    }

    SECTION("push and pop single element") {
        REQUIRE(q.try_push(42));
        REQUIRE_FALSE(q.empty());

        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == 42);
        REQUIRE(q.empty());
    }

    SECTION("FIFO ordering preserved") {
        for (int i = 0; i < 7; ++i) {
            REQUIRE(q.try_push(i));
        }

        for (int i = 0; i < 7; ++i) {
            int val{};
            REQUIRE(q.try_pop(val));
            REQUIRE(val == i);
        }
    }

    SECTION("pop from empty returns nullopt") {
        REQUIRE_FALSE(q.try_pop().has_value());
    }

    SECTION("pop with output param from empty returns false") {
        int val{};
        REQUIRE_FALSE(q.try_pop(val));
    }

    SECTION("push to full queue returns false") {
        for (int i = 0; i < 7; ++i) {
            REQUIRE(q.try_push(i));
        }
        REQUIRE(q.full());
        REQUIRE_FALSE(q.try_push(999));
    }

    SECTION("push after pop from full queue succeeds") {
        for (int i = 0; i < 7; ++i) {
            REQUIRE(q.try_push(i));
        }
        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(q.try_push(100));
    }

    SECTION("wrap around works correctly") {
        // Fill and drain multiple times to exercise wrap-around
        for (int round = 0; round < 5; ++round) {
            for (int i = 0; i < 7; ++i) {
                REQUIRE(q.try_push(round * 100 + i));
            }
            for (int i = 0; i < 7; ++i) {
                int val{};
                REQUIRE(q.try_pop(val));
                REQUIRE(val == round * 100 + i);
            }
            REQUIRE(q.empty());
        }
    }
}

TEST_CASE("SPSCQueue move semantics", "[queue][spsc][regression]") {
    SPSCQueue<std::string, 4> q;

    SECTION("push rvalue") {
        std::string s = "test_string";
        REQUIRE(q.try_push(std::move(s)));

        auto val = q.try_pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == "test_string");
    }
}

TEST_CASE("SPSCQueue emplace", "[queue][spsc][regression]") {
    struct Data {
        int x;
        float y;
        Data() : x(0), y(0.0f) {}
        Data(int a, float b) : x(a), y(b) {}
    };

    SPSCQueue<Data, 4> q;
    REQUIRE(q.try_emplace(7, 2.5f));

    auto val = q.try_pop();
    REQUIRE(val.has_value());
    REQUIRE(val->x == 7);
    REQUIRE(val->y == 2.5f);
}

TEST_CASE("SPSCQueue front/peek", "[queue][spsc][regression]") {
    SPSCQueue<int, 4> q;

    SECTION("front on empty queue returns nullptr") {
        REQUIRE(q.front() == nullptr);
    }

    SECTION("front returns pointer to first element without removing it") {
        REQUIRE(q.try_push(10));
        REQUIRE(q.try_push(20));

        const int* f = q.front();
        REQUIRE(f != nullptr);
        REQUIRE(*f == 10);

        // Still in queue
        REQUIRE(q.size_approx() == 2);
    }
}

TEST_CASE("SPSCQueue blocking push and pop", "[queue][spsc][regression]") {
    SPSCQueue<int, 4> q;

    q.push(1);
    q.push(2);

    int val = q.pop();
    REQUIRE(val == 1);
    val = q.pop();
    REQUIRE(val == 2);
}

// ============================================================================
// SPSCQueue Producer-Consumer Test
// ============================================================================

TEST_CASE("SPSCQueue single-producer single-consumer", "[queue][spsc][regression]") {
    constexpr size_t NUM_ITEMS = 10000;
    SPSCQueue<int, 1024> q;

    std::thread producer([&q]() {
        for (size_t i = 0; i < NUM_ITEMS; ++i) {
            while (!q.try_push(static_cast<int>(i))) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    while (received.size() < NUM_ITEMS) {
        int val{};
        if (q.try_pop(val)) {
            received.push_back(val);
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();

    // Verify FIFO ordering (strict for SPSC)
    REQUIRE(received.size() == NUM_ITEMS);
    for (size_t i = 0; i < NUM_ITEMS; ++i) {
        REQUIRE(received[i] == static_cast<int>(i));
    }
}

// ============================================================================
// BoundedSPSCQueue Tests
// ============================================================================

TEST_CASE("BoundedSPSCQueue basic operations", "[queue][spsc][regression]") {
    BoundedSPSCQueue<int, 8, 256> q;

    SECTION("empty queue") {
        REQUIRE(q.empty());
        REQUIRE(q.size_approx() == 0);
        REQUIRE(q.bytes_used() == 0);
    }

    SECTION("push and pop with byte tracking") {
        REQUIRE(q.try_push(42, 16));
        REQUIRE(q.bytes_used() == 16);
        REQUIRE_FALSE(q.empty());

        int val{};
        REQUIRE(q.try_pop(val, 16));
        REQUIRE(val == 42);
        REQUIRE(q.bytes_used() == 0);
        REQUIRE(q.empty());
    }

    SECTION("byte limit enforced") {
        // Push items until byte limit
        REQUIRE(q.try_push(1, 100));
        REQUIRE(q.try_push(2, 100));
        REQUIRE(q.bytes_used() == 200);

        // This should fail: 200 + 100 > 256
        REQUIRE_FALSE(q.try_push(3, 100));

        // Pop one and try again
        int val{};
        REQUIRE(q.try_pop(val, 100));
        REQUIRE(q.bytes_used() == 100);
        REQUIRE(q.try_push(3, 100));
    }
}
