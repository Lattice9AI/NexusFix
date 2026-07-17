// test_branch_coverage_499_pools.cpp
//
// TICKET_499 WI3 (batch 9): memory-pool boundary branches. The pools have
// exhaustion (allocate returns nullptr), null-deallocate, out-of-bounds /
// not-owned pointer, and full/empty predicate arms that the steady-state pool
// tests never take.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "nexusfix/memory/buffer_pool.hpp"
#include "nexusfix/memory/object_pool.hpp"

using namespace nfx;
using namespace nfx::memory;

TEST_CASE("ObjectPool exhaustion and boundary arms", "[memory][object_pool][branch499][regression]") {
    ObjectPool<int64_t, 3> pool;

    REQUIRE(pool.full());              // full arm (all available)
    REQUIRE(pool.available() == 3);

    int64_t* a = pool.allocate(1);
    int64_t* b = pool.allocate(2);
    int64_t* c = pool.allocate(3);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    REQUIRE_FALSE(pool.full());
    REQUIRE(pool.empty());            // empty arm (none available)

    // Exhausted: allocate returns nullptr.
    REQUIRE(pool.allocate(4) == nullptr);

    pool.deallocate(b);
    REQUIRE(pool.available() == 1);

    // null-ptr deallocate: early return, no crash.
    pool.deallocate(nullptr);
    REQUIRE(pool.available() == 1);

    // out-of-bounds pointer: not from this pool, ignored.
    int64_t stray = 99;
    pool.deallocate(&stray);
    REQUIRE(pool.available() == 1);

    pool.deallocate(a);
    pool.deallocate(c);
    REQUIRE(pool.full());
}

TEST_CASE("ObjectPool allocate_raw / deallocate_raw arms", "[memory][object_pool][branch499][regression]") {
    ObjectPool<long, 2> pool;
    void* p1 = pool.allocate_raw();
    void* p2 = pool.allocate_raw();
    REQUIRE(p1);
    REQUIRE(p2);
    REQUIRE(pool.allocate_raw() == nullptr);   // exhausted

    pool.deallocate_raw(nullptr);              // null arm
    int stray = 0;
    pool.deallocate_raw(&stray);               // oob arm
    pool.deallocate_raw(p1);
    REQUIRE(pool.available() == 1);
    pool.deallocate_raw(p2);
    REQUIRE(pool.full());
}

TEST_CASE("ObjectPool reset restores full state", "[memory][object_pool][branch499][regression]") {
    ObjectPool<int64_t, 4> pool;
    (void)pool.allocate(1);
    (void)pool.allocate(2);
    REQUIRE_FALSE(pool.full());
    pool.reset();
    REQUIRE(pool.full());
    REQUIRE(pool.available() == 4);
}

TEST_CASE("FixedPool exhaustion, ownership and null-deallocate", "[memory][buffer_pool][branch499][regression]") {
    FixedPool<64, 3> pool;
    REQUIRE(pool.available() == 3);

    std::vector<void*> blocks;
    for (int i = 0; i < 3; ++i) {
        void* p = pool.allocate();
        REQUIRE(p);
        REQUIRE(pool.owns(p));      // owns true arm
        blocks.push_back(p);
    }
    REQUIRE(pool.allocate() == nullptr);   // exhausted arm

    // ownership false arm: an address outside the pool storage.
    int stray = 0;
    REQUIRE_FALSE(pool.owns(&stray));

    pool.deallocate(nullptr);              // null-ptr early return
    REQUIRE(pool.available() == 0);

    for (void* p : blocks) { pool.deallocate(p); }
    REQUIRE(pool.available() == 3);
}
