#include <catch2/catch_test_macros.hpp>

#include "nexusfix/memory/seqlock.hpp"
#include "nexusfix/memory/object_pool.hpp"
#include "nexusfix/memory/wait_strategy.hpp"

#include <thread>
#include <vector>
#include <atomic>

using namespace nfx::memory;

// ============================================================================
// Seqlock Tests
// ============================================================================

TEST_CASE("Seqlock basic operations", "[seqlock][regression]") {
    SECTION("default constructed seqlock") {
        Seqlock<int> lock;
        REQUIRE(lock.sequence() == 0);
        REQUIRE_FALSE(lock.is_writing());
        REQUIRE(lock.read() == 0);
    }

    SECTION("constructed with initial value") {
        Seqlock<int> lock(42);
        REQUIRE(lock.read() == 42);
    }

    SECTION("write and read") {
        Seqlock<int> lock;
        lock.write(100);
        REQUIRE(lock.read() == 100);
        REQUIRE(lock.sequence() == 2);  // Two increments per write
    }

    SECTION("multiple writes") {
        Seqlock<int> lock;
        lock.write(10);
        lock.write(20);
        lock.write(30);
        REQUIRE(lock.read() == 30);
        REQUIRE(lock.sequence() == 6);  // 3 writes * 2
    }
}

TEST_CASE("Seqlock try_read", "[seqlock][regression]") {
    Seqlock<int> lock(99);

    SECTION("try_read succeeds when no write in progress") {
        int val{};
        REQUIRE(lock.try_read(val));
        REQUIRE(val == 99);
    }

    SECTION("try_read fails during write") {
        lock.begin_write();
        REQUIRE(lock.is_writing());

        int val{};
        REQUIRE_FALSE(lock.try_read(val));

        lock.end_write();
        REQUIRE_FALSE(lock.is_writing());
        REQUIRE(lock.try_read(val));
    }
}

TEST_CASE("Seqlock read_with callback", "[seqlock][regression]") {
    struct MarketData {
        double bid;
        double ask;
        uint64_t seq;
    };

    Seqlock<MarketData> lock(MarketData{100.5, 101.0, 1});

    SECTION("read_with extracts field without full copy") {
        double spread = lock.read_with([](const MarketData& md) {
            return md.ask - md.bid;
        });
        REQUIRE(spread == 0.5);
    }

    SECTION("read_with after write") {
        lock.write(MarketData{200.0, 201.5, 2});
        uint64_t seq = lock.read_with([](const MarketData& md) {
            return md.seq;
        });
        REQUIRE(seq == 2);
    }
}

TEST_CASE("Seqlock begin_write/end_write", "[seqlock][regression]") {
    struct Point { int x; int y; };
    Seqlock<Point> lock(Point{0, 0});

    SECTION("multi-field update via begin/end write") {
        lock.begin_write();
        lock.data().x = 10;
        lock.data().y = 20;
        lock.end_write();

        auto result = lock.read();
        REQUIRE(result.x == 10);
        REQUIRE(result.y == 20);
    }
}

// ============================================================================
// SeqlockWriteGuard Tests
// ============================================================================

TEST_CASE("SeqlockWriteGuard RAII", "[seqlock][regression]") {
    struct Config { int a; int b; };
    Seqlock<Config> lock(Config{1, 2});

    SECTION("guard provides write access and auto-ends") {
        {
            SeqlockWriteGuard guard(lock);
            guard.data().a = 10;
            guard->b = 20;
            REQUIRE(lock.is_writing());
        }
        // Guard destroyed, write ended
        REQUIRE_FALSE(lock.is_writing());

        auto val = lock.read();
        REQUIRE(val.a == 10);
        REQUIRE(val.b == 20);
    }
}

// ============================================================================
// VersionedValue Tests
// ============================================================================

TEST_CASE("VersionedValue change detection", "[seqlock][regression]") {
    VersionedValue<int> vv(100);

    SECTION("initial read returns version 0") {
        auto snap = vv.read();
        REQUIRE(snap.value == 100);
        REQUIRE(snap.version == 0);
    }

    SECTION("write increments version") {
        vv.write(200);
        auto snap = vv.read();
        REQUIRE(snap.value == 200);
        REQUIRE(snap.version == 1);
    }

    SECTION("changed_since detects changes") {
        REQUIRE_FALSE(vv.changed_since(0));
        vv.write(300);
        REQUIRE(vv.changed_since(0));
    }

    SECTION("read_if_changed returns true only on change") {
        int val{};
        uint64_t ver = 0;

        // No change yet
        REQUIRE_FALSE(vv.read_if_changed(val, ver));

        vv.write(400);
        REQUIRE(vv.read_if_changed(val, ver));
        REQUIRE(val == 400);
        REQUIRE(ver == 1);

        // No new change
        REQUIRE_FALSE(vv.read_if_changed(val, ver));
    }
}

// ============================================================================
// Seqlock Multi-threaded Test
// ============================================================================

TEST_CASE("Seqlock writer-reader consistency", "[seqlock][regression]") {
    struct Payload {
        int64_t a;
        int64_t b;
        int64_t sum;  // invariant: sum == a + b
    };

    Seqlock<Payload> lock(Payload{0, 0, 0});
    std::atomic<bool> done{false};
    std::atomic<size_t> violations{0};

    // Writer thread
    std::thread writer([&]() {
        for (int64_t i = 1; i <= 10000; ++i) {
            lock.write(Payload{i, i * 2, i + i * 2});
        }
        done.store(true, std::memory_order_release);
    });

    // Reader thread verifies invariant
    std::thread reader([&]() {
        while (!done.load(std::memory_order_acquire)) {
            auto val = lock.read();
            if (val.sum != val.a + val.b) {
                violations.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    writer.join();
    reader.join();

    REQUIRE(violations.load() == 0);
}

// ============================================================================
// ObjectPool Tests
// ============================================================================

TEST_CASE("ObjectPool basic operations", "[memory][object_pool][regression]") {
    struct Item {
        int value;
        int padding;  // Ensure sizeof(Item) >= sizeof(void*)
        Item() : value(0), padding(0) {}
        Item(int v) : value(v), padding(0) {}
    };

    ObjectPool<Item, 4> pool;

    SECTION("initial state") {
        REQUIRE(pool.available() == 4);
        REQUIRE(pool.allocated() == 0);
        REQUIRE(ObjectPool<Item, 4>::capacity() == 4);
        REQUIRE(pool.full());
        REQUIRE_FALSE(pool.empty());
    }

    SECTION("allocate and deallocate") {
        Item* p = pool.allocate(42);
        REQUIRE(p != nullptr);
        REQUIRE(p->value == 42);
        REQUIRE(pool.available() == 3);
        REQUIRE(pool.allocated() == 1);

        pool.deallocate(p);
        REQUIRE(pool.available() == 4);
        REQUIRE(pool.allocated() == 0);
    }

    SECTION("exhaustion returns nullptr") {
        Item* ptrs[4];
        for (int i = 0; i < 4; ++i) {
            ptrs[i] = pool.allocate(i);
            REQUIRE(ptrs[i] != nullptr);
        }
        REQUIRE(pool.empty());
        REQUIRE(pool.allocate() == nullptr);

        // Free one and allocate again
        pool.deallocate(ptrs[0]);
        Item* p = pool.allocate(99);
        REQUIRE(p != nullptr);
        REQUIRE(p->value == 99);

        // Clean up
        pool.deallocate(p);
        for (int i = 1; i < 4; ++i) {
            pool.deallocate(ptrs[i]);
        }
    }

    SECTION("owns checks pointer bounds") {
        Item* p = pool.allocate(1);
        REQUIRE(pool.owns(p));

        Item stack_item(2);
        REQUIRE_FALSE(pool.owns(&stack_item));

        pool.deallocate(p);
    }

    SECTION("deallocate nullptr is safe") {
        pool.deallocate(nullptr);
        REQUIRE(pool.available() == 4);
    }

    SECTION("deallocate foreign pointer is ignored") {
        Item foreign(0);
        pool.deallocate(&foreign);
        REQUIRE(pool.available() == 4);
    }

    SECTION("allocate_raw and deallocate_raw") {
        void* raw = pool.allocate_raw();
        REQUIRE(raw != nullptr);
        REQUIRE(pool.available() == 3);

        pool.deallocate_raw(raw);
        REQUIRE(pool.available() == 4);
    }

    SECTION("reset reclaims all slots") {
        [[maybe_unused]] auto* p1 = pool.allocate(1);
        [[maybe_unused]] auto* p2 = pool.allocate(2);
        REQUIRE(pool.available() == 2);

        pool.reset();
        REQUIRE(pool.available() == 4);
        REQUIRE(pool.full());
    }
}

// ============================================================================
// PoolPtr RAII Tests
// ============================================================================

TEST_CASE("PoolPtr RAII", "[memory][object_pool][regression]") {
    struct Widget {
        int val;
        int padding;  // Ensure sizeof >= sizeof(void*)
        Widget() : val(0), padding(0) {}
        Widget(int v) : val(v), padding(0) {}
    };

    ObjectPool<Widget, 4> pool;

    SECTION("make_pooled creates and auto-releases") {
        {
            auto ptr = make_pooled(pool, 42);
            REQUIRE(ptr);
            REQUIRE(ptr->val == 42);
            REQUIRE(pool.allocated() == 1);
        }
        // ptr destroyed, returned to pool
        REQUIRE(pool.allocated() == 0);
    }

    SECTION("PoolPtr move semantics") {
        auto p1 = make_pooled(pool, 10);
        REQUIRE(pool.allocated() == 1);

        auto p2 = std::move(p1);
        REQUIRE(pool.allocated() == 1);
        REQUIRE_FALSE(p1);
        REQUIRE(p2);
        REQUIRE(p2->val == 10);
    }

    SECTION("PoolPtr release") {
        auto ptr = make_pooled(pool, 5);
        Widget* raw = ptr.release();
        REQUIRE(raw != nullptr);
        REQUIRE(pool.allocated() == 1);

        // Manual cleanup
        pool.deallocate(raw);
        REQUIRE(pool.allocated() == 0);
    }

    SECTION("PoolPtr reset") {
        auto ptr = make_pooled(pool, 7);
        REQUIRE(pool.allocated() == 1);
        ptr.reset();
        REQUIRE(pool.allocated() == 0);
        REQUIRE_FALSE(ptr);
    }
}

// ============================================================================
// GrowingObjectPool Tests
// ============================================================================

TEST_CASE("GrowingObjectPool", "[memory][object_pool][regression]") {
    struct Node {
        int data;
        int padding;  // Ensure sizeof >= sizeof(void*)
        Node() : data(0), padding(0) {}
        Node(int d) : data(d), padding(0) {}
    };

    GrowingObjectPool<Node, 4> pool;

    SECTION("initial state") {
        REQUIRE(pool.allocated() == 0);
        REQUIRE(pool.chunks() == 0);
    }

    SECTION("first allocation triggers chunk growth") {
        Node* p = pool.allocate(42);
        REQUIRE(p != nullptr);
        REQUIRE(p->data == 42);
        REQUIRE(pool.allocated() == 1);
        REQUIRE(pool.chunks() == 1);
        REQUIRE(pool.capacity() == 4);

        pool.deallocate(p);
    }

    SECTION("grows when exhausted") {
        std::vector<Node*> ptrs;
        // Allocate more than one chunk
        for (int i = 0; i < 6; ++i) {
            ptrs.push_back(pool.allocate(i));
            REQUIRE(ptrs.back() != nullptr);
        }
        REQUIRE(pool.chunks() == 2);
        REQUIRE(pool.capacity() == 8);
        REQUIRE(pool.allocated() == 6);

        for (auto* p : ptrs) {
            pool.deallocate(p);
        }
        REQUIRE(pool.allocated() == 0);
    }

    SECTION("deallocated objects are reused") {
        Node* p1 = pool.allocate(1);
        pool.deallocate(p1);
        Node* p2 = pool.allocate(2);
        // Should reuse same slot
        REQUIRE(p2 == p1);
        REQUIRE(p2->data == 2);
        pool.deallocate(p2);
    }
}

// ============================================================================
// WaitStrategy Concept Tests
// ============================================================================

TEST_CASE("WaitStrategy concept", "[queue][wait_strategy][regression]") {
    SECTION("all strategies satisfy concept") {
        STATIC_REQUIRE(WaitStrategy<BusySpinWait>);
        STATIC_REQUIRE(WaitStrategy<YieldingWait>);
        STATIC_REQUIRE(WaitStrategy<SleepingWait<>>);
        STATIC_REQUIRE(WaitStrategy<SleepingWait<10>>);
        STATIC_REQUIRE(WaitStrategy<BackoffWait<>>);
    }

    SECTION("strategy names") {
        REQUIRE(std::string_view(BusySpinWait::name()) == "BusySpinWait");
        REQUIRE(std::string_view(YieldingWait::name()) == "YieldingWait");
        REQUIRE(std::string_view(SleepingWait<>::name()) == "SleepingWait");
        REQUIRE(std::string_view(BackoffWait<>::name()) == "BackoffWait");
    }

    SECTION("wait_until with immediate true predicate") {
        BusySpinWait::wait_until([]{ return true; });
        YieldingWait::wait_until([]{ return true; });
    }

    SECTION("wait_for_sequence with already-reached target") {
        std::atomic<size_t> seq{10};
        BusySpinWait::wait_for_sequence(seq, 5);
        YieldingWait::wait_for_sequence(seq, 10);
    }

    SECTION("BackoffWait state transitions") {
        BackoffWait<>::State state{};
        REQUIRE(state.spin_count == 0);
        REQUIRE(state.yield_count == 0);
        REQUIRE(state.sleep_us == 1);

        BackoffWait<>::wait(state);
        REQUIRE(state.spin_count == 1);

        BackoffWait<>::reset(state);
        REQUIRE(state.spin_count == 0);
    }
}
