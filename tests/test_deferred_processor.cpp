#include <catch2/catch_test_macros.hpp>

#include "nexusfix/util/deferred_processor.hpp"
#include "nexusfix/util/thread_local_pool.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <string>
#include <cstring>

using namespace nfx::util;

// ============================================================================
// ThreadLocalPool Tests
// ============================================================================

TEST_CASE("ThreadLocalPool basic operations", "[utils][thread_local_pool][regression]") {
    // Use a unique type per test to get a fresh thread_local pool
    struct TestObj_Basic {
        int value{0};
    };

    auto& pool = ThreadLocalPool<TestObj_Basic, 8>::instance();

    SECTION("initial state") {
        REQUIRE(pool.available() <= ThreadLocalPool<TestObj_Basic, 8>::capacity());
        REQUIRE(ThreadLocalPool<TestObj_Basic, 8>::capacity() == 8);
    }

    SECTION("acquire and release") {
        size_t initial = pool.available();
        TestObj_Basic* obj = pool.acquire();
        REQUIRE(obj != nullptr);
        REQUIRE(pool.available() == initial - 1);

        pool.release(obj);
        REQUIRE(pool.available() == initial);
    }

    SECTION("release nullptr is safe") {
        size_t before = pool.available();
        pool.release(nullptr);
        REQUIRE(pool.available() == before);
    }

    SECTION("stats tracking") {
        pool.reset_stats();
        auto* obj = pool.acquire();
        REQUIRE(pool.stats().acquires == 1);

        pool.release(obj);
        REQUIRE(pool.stats().releases == 1);
    }
}

TEST_CASE("ThreadLocalPool exhaustion", "[utils][thread_local_pool][regression]") {
    struct TestObj_Exhaust {
        int data{0};
    };

    auto& pool = ThreadLocalPool<TestObj_Exhaust, 4>::instance();

    // Drain all available
    std::vector<TestObj_Exhaust*> acquired;
    while (auto* obj = pool.acquire()) {
        acquired.push_back(obj);
    }

    SECTION("acquire returns nullptr when exhausted") {
        REQUIRE(pool.acquire() == nullptr);
        REQUIRE(pool.available() == 0);
    }

    // Release all back
    for (auto* obj : acquired) {
        pool.release(obj);
    }
}

// ============================================================================
// PooledPtr RAII Tests
// ============================================================================

TEST_CASE("PooledPtr RAII wrapper", "[utils][thread_local_pool][regression]") {
    struct TestObj_RAII {
        int val{42};
    };

    SECTION("acquire from pool") {
        auto ptr = PooledPtr<TestObj_RAII, 64>::acquire();
        REQUIRE(ptr);
        REQUIRE(ptr->val == 42);
    }

    SECTION("acquire_pooled_only") {
        auto ptr = PooledPtr<TestObj_RAII, 64>::acquire_pooled_only();
        REQUIRE(ptr);
        REQUIRE(ptr.is_from_pool());
    }

    SECTION("move semantics") {
        auto p1 = PooledPtr<TestObj_RAII, 64>::acquire();
        REQUIRE(p1);

        auto p2 = std::move(p1);
        REQUIRE(p2);
        REQUIRE_FALSE(p1);
    }

    SECTION("release returns to pool") {
        auto ptr = PooledPtr<TestObj_RAII, 64>::acquire();
        REQUIRE(ptr);
        ptr.release();
        REQUIRE_FALSE(ptr);
    }
}

// ============================================================================
// MessageBuffer Tests
// ============================================================================

TEST_CASE("MessageBuffer", "[utils][thread_local_pool][regression]") {
    MessageBuffer buf;

    SECTION("initial state") {
        REQUIRE(buf.empty());
        REQUIRE(buf.size() == 0);
        REQUIRE(MessageBuffer::MAX_SIZE == 4096);
    }

    SECTION("set and read") {
        const char* msg = "8=FIX.4.4\x01""35=D\x01";
        size_t len = std::strlen(msg);
        buf.set(msg, len);

        REQUIRE(buf.size() == len);
        REQUIRE_FALSE(buf.empty());
        REQUIRE(std::memcmp(buf.data, msg, len) == 0);
    }

    SECTION("clear") {
        buf.set("data", 4);
        REQUIRE_FALSE(buf.empty());
        buf.clear();
        REQUIRE(buf.empty());
    }

    SECTION("truncation at MAX_SIZE") {
        std::string large(5000, 'X');
        buf.set(large.c_str(), large.size());
        REQUIRE(buf.size() == MessageBuffer::MAX_SIZE);
        REQUIRE(buf.truncated());
    }

    SECTION("exactly at MAX_SIZE sets no truncation") {
        std::string exact(MessageBuffer::MAX_SIZE, 'B');
        buf.set(exact.c_str(), exact.size());
        REQUIRE(buf.size() == MessageBuffer::MAX_SIZE);
        REQUIRE_FALSE(buf.truncated());
        REQUIRE(std::memcmp(buf.data, exact.data(), MessageBuffer::MAX_SIZE) == 0);
    }

    SECTION("one byte over MAX_SIZE sets truncation") {
        std::string over(MessageBuffer::MAX_SIZE + 1, 'C');
        buf.set(over.c_str(), over.size());
        REQUIRE(buf.size() == MessageBuffer::MAX_SIZE);
        REQUIRE(buf.truncated());
        REQUIRE(std::memcmp(buf.data, over.data(), MessageBuffer::MAX_SIZE) == 0);
    }

    SECTION("small message clears truncation flag") {
        std::string large(5000, 'D');
        buf.set(large.c_str(), large.size());
        REQUIRE(buf.truncated());

        buf.set("hello", 5);
        REQUIRE_FALSE(buf.truncated());
        REQUIRE(buf.size() == 5);
    }

    SECTION("clear resets truncation flag") {
        std::string large(5000, 'E');
        buf.set(large.c_str(), large.size());
        REQUIRE(buf.truncated());

        buf.clear();
        REQUIRE_FALSE(buf.truncated());
    }

    SECTION("begin/end iterators") {
        buf.set("hello", 5);
        REQUIRE(buf.end() - buf.begin() == 5);
    }
}

// ============================================================================
// LargeBuffer Truncation Tests
// ============================================================================

TEST_CASE("LargeBuffer truncation", "[utils][thread_local_pool][regression]") {
    // Stack-allocating a 64KB LargeBuffer is fine for tests
    auto buf = std::make_unique<nfx::util::LargeBuffer>();

    SECTION("initial state") {
        REQUIRE(buf->size() == 0);
        REQUIRE_FALSE(buf->truncated());
        REQUIRE(nfx::util::LargeBuffer::MAX_SIZE == 65536);
    }

    SECTION("normal set does not truncate") {
        std::string msg(1000, 'A');
        buf->set(msg.c_str(), msg.size());
        REQUIRE(buf->size() == 1000);
        REQUIRE_FALSE(buf->truncated());
    }

    SECTION("exactly at MAX_SIZE sets no truncation") {
        std::string exact(nfx::util::LargeBuffer::MAX_SIZE, 'B');
        buf->set(exact.c_str(), exact.size());
        REQUIRE(buf->size() == nfx::util::LargeBuffer::MAX_SIZE);
        REQUIRE_FALSE(buf->truncated());
    }

    SECTION("one byte over MAX_SIZE sets truncation") {
        std::string over(nfx::util::LargeBuffer::MAX_SIZE + 1, 'C');
        buf->set(over.c_str(), over.size());
        REQUIRE(buf->size() == nfx::util::LargeBuffer::MAX_SIZE);
        REQUIRE(buf->truncated());
    }

    SECTION("small message clears truncation flag") {
        std::string large(nfx::util::LargeBuffer::MAX_SIZE + 100, 'D');
        buf->set(large.c_str(), large.size());
        REQUIRE(buf->truncated());

        buf->set("hello", 5);
        REQUIRE_FALSE(buf->truncated());
        REQUIRE(buf->size() == 5);
    }

    SECTION("clear resets truncation flag") {
        std::string large(nfx::util::LargeBuffer::MAX_SIZE + 100, 'E');
        buf->set(large.c_str(), large.size());
        REQUIRE(buf->truncated());

        buf->clear();
        REQUIRE_FALSE(buf->truncated());
        REQUIRE(buf->size() == 0);
    }
}

// ============================================================================
// DeferredMessageBuffer Tests
// ============================================================================

TEST_CASE("DeferredMessageBuffer", "[utils][deferred][regression]") {
    DeferredMessageBuffer<256> buf;

    SECTION("set and span") {
        std::string data = "test message";
        buf.set(std::span<const char>(data.data(), data.size()), 12345);

        REQUIRE(buf.timestamp == 12345);
        REQUIRE(buf.size == data.size());

        auto sp = buf.span();
        REQUIRE(sp.size() == data.size());
        REQUIRE(std::memcmp(sp.data(), data.data(), data.size()) == 0);
    }

    SECTION("truncation at MaxSize") {
        std::string large(500, 'A');
        buf.set(std::span<const char>(large.data(), large.size()), 0);
        REQUIRE(buf.size == 256);
        REQUIRE(buf.truncated());
    }

    SECTION("exactly at MaxSize sets no truncation") {
        std::string exact(256, 'B');
        buf.set(std::span<const char>(exact.data(), exact.size()), 42);
        REQUIRE(buf.size == 256);
        REQUIRE_FALSE(buf.truncated());
        REQUIRE(buf.timestamp == 42);
        REQUIRE(std::memcmp(buf.span().data(), exact.data(), 256) == 0);
    }

    SECTION("one byte over MaxSize sets truncation") {
        std::string over(257, 'C');
        buf.set(std::span<const char>(over.data(), over.size()), 99);
        REQUIRE(buf.size == 256);
        REQUIRE(buf.truncated());
        REQUIRE(buf.timestamp == 99);
        // Data contains first 256 bytes
        REQUIRE(std::memcmp(buf.span().data(), over.data(), 256) == 0);
    }

    SECTION("small message clears truncation flag") {
        // First set with oversized message
        std::string large(500, 'D');
        buf.set(std::span<const char>(large.data(), large.size()), 0);
        REQUIRE(buf.truncated());

        // Then set with small message - flag must clear
        std::string small = "hello";
        buf.set(std::span<const char>(small.data(), small.size()), 1);
        REQUIRE_FALSE(buf.truncated());
        REQUIRE(buf.size == small.size());
    }
}

// ============================================================================
// DeferredProcessor Truncation Rejection Tests
// ============================================================================

TEST_CASE("DeferredProcessor rejects oversized messages", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    std::atomic<int> processed{0};
    proc.start([&](const auto&) {
        processed.fetch_add(1);
    });

    SECTION("submit rejects message exceeding MaxSize") {
        std::string oversized(257, 'X');
        REQUIRE_FALSE(proc.submit(
            std::span<const char>(oversized.data(), oversized.size())));

        // Give background thread time to process anything that leaked through
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(processed.load() == 0);
    }

    SECTION("submit accepts message exactly at MaxSize") {
        std::string exact(256, 'Y');
        REQUIRE(proc.submit(
            std::span<const char>(exact.data(), exact.size())));

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (processed.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(processed.load() == 1);
    }

    SECTION("submit_blocking rejects oversized message") {
        std::string oversized(300, 'Z');
        REQUIRE_FALSE(proc.submit_blocking(
            std::span<const char>(oversized.data(), oversized.size())));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(processed.load() == 0);
    }

    SECTION("mixed: oversized rejected, valid delivered") {
        std::string oversized(500, 'A');
        std::string valid = "valid_message";

        REQUIRE_FALSE(proc.submit(
            std::span<const char>(oversized.data(), oversized.size())));
        REQUIRE(proc.submit(
            std::span<const char>(valid.data(), valid.size())));

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (processed.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Only the valid message was processed
        REQUIRE(processed.load() == 1);
    }

    proc.stop();
}

TEST_CASE("DeferredProcessor publish_slot rejects truncated buffer", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    std::atomic<int> processed{0};
    proc.start([&](const auto&) {
        processed.fetch_add(1);
    });

    SECTION("oversized via slot path is rejected") {
        auto* slot = proc.try_reserve_slot();
        REQUIRE(slot != nullptr);

        std::string oversized(300, 'W');
        slot->set(std::span<const char>(oversized.data(), oversized.size()), 1);
        REQUIRE_FALSE(proc.publish_slot());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(processed.load() == 0);
    }

    SECTION("valid via slot path is accepted") {
        auto* slot = proc.try_reserve_slot();
        REQUIRE(slot != nullptr);

        std::string valid = "slot_message";
        slot->set(std::span<const char>(valid.data(), valid.size()), 2);
        REQUIRE(proc.publish_slot());

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (processed.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(processed.load() == 1);
    }

    SECTION("slot is released after truncation rejection") {
        auto* slot = proc.try_reserve_slot();
        REQUIRE(slot != nullptr);

        std::string oversized(400, 'Q');
        slot->set(std::span<const char>(oversized.data(), oversized.size()), 3);
        REQUIRE_FALSE(proc.publish_slot());

        // Slot should be available again after rejection
        auto* slot2 = proc.try_reserve_slot();
        REQUIRE(slot2 != nullptr);

        std::string valid = "after_reject";
        slot2->set(std::span<const char>(valid.data(), valid.size()), 4);
        REQUIRE(proc.publish_slot());

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (processed.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(processed.load() == 1);
    }

    proc.stop();
}

// ============================================================================
// DeferredProcessor Tests
// ============================================================================

TEST_CASE("DeferredProcessor lifecycle", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    SECTION("initial state") {
        REQUIRE_FALSE(proc.is_running());
        REQUIRE(proc.queue_empty());
        REQUIRE(proc.queue_depth() == 0);
    }

    SECTION("start and stop") {
        std::atomic<int> count{0};
        REQUIRE(proc.start([&](const auto&) { count.fetch_add(1); }));
        REQUIRE(proc.is_running());

        // Double start returns false
        REQUIRE_FALSE(proc.start([](const auto&) {}));

        proc.stop();
        REQUIRE_FALSE(proc.is_running());
    }
}

TEST_CASE("DeferredProcessor submit and process", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    std::atomic<int> processed{0};
    std::vector<std::string> messages;
    std::mutex mtx;

    proc.start([&](const auto& buf) {
        auto sp = buf.span();
        std::lock_guard lock(mtx);
        messages.emplace_back(sp.data(), sp.size());
        processed.fetch_add(1);
    });

    // Submit messages
    std::string msg1 = "message_one";
    std::string msg2 = "message_two";
    REQUIRE(proc.submit(std::span<const char>(msg1.data(), msg1.size()), 100));
    REQUIRE(proc.submit(std::span<const char>(msg2.data(), msg2.size()), 200));

    // Wait for processing
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (processed.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    proc.stop();

    REQUIRE(processed.load() == 2);
    REQUIRE(messages.size() == 2);
    REQUIRE(messages[0] == "message_one");
    REQUIRE(messages[1] == "message_two");
}

TEST_CASE("DeferredProcessor stop with drain", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    std::atomic<int> count{0};
    proc.start([&](const auto&) {
        count.fetch_add(1);
    });

    // Submit several messages
    for (int i = 0; i < 10; ++i) {
        std::string msg = "msg" + std::to_string(i);
        while (!proc.submit(std::span<const char>(msg.data(), msg.size()))) {
            std::this_thread::yield();
        }
    }

    // Stop with drain=true (default)
    proc.stop(true);

    // All messages should have been processed
    REQUIRE(count.load() == 10);
}

TEST_CASE("DeferredProcessor queue full", "[utils][deferred][regression]") {
    // Small queue, no consumer
    using SmallQueue = DeferredProcessor<DeferredMessageBuffer<64>, 4>;
    SmallQueue proc;

    // Don't start the processor (no consumer)
    // Submit until full
    std::string msg = "data";
    int submitted = 0;
    for (int i = 0; i < 10; ++i) {
        if (proc.submit(std::span<const char>(msg.data(), msg.size()))) {
            ++submitted;
        }
    }
    // Queue capacity is 3 (SPSC reserves one slot from power-of-2 capacity 4)
    REQUIRE(submitted <= 3);
}

TEST_CASE("DeferredProcessor stats", "[utils][deferred][regression]") {
    using Processor = DeferredProcessor<DeferredMessageBuffer<256>, 64>;
    Processor proc;

    auto initial = proc.stats();
    REQUIRE(initial.messages_submitted == 0);
    REQUIRE(initial.messages_processed == 0);
}

// ============================================================================
// Specialized Processor Type Aliases
// ============================================================================

TEST_CASE("Specialized processor types compile", "[utils][deferred][regression]") {
    // Just verify the type aliases compile
    [[maybe_unused]] DeferredFIXProcessor* p1 = nullptr;
    [[maybe_unused]] HighThroughputProcessor* p2 = nullptr;
    [[maybe_unused]] CompactProcessor* p3 = nullptr;
    REQUIRE(true);
}
