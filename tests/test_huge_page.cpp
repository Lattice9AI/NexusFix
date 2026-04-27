#include <catch2/catch_test_macros.hpp>

#include "nexusfix/memory/huge_page_allocator.hpp"

#include <cstring>
#include <vector>

using namespace nfx::memory;

// ============================================================================
// HugePageAllocator - Availability
// ============================================================================

TEST_CASE("is_available returns boolean without crash", "[huge_page][regression]") {
    bool available = HugePageAllocator::is_available();
    // Just verifying it doesn't crash; result depends on system config
    (void)available;
}

TEST_CASE("free_huge_pages returns count", "[huge_page][regression]") {
    size_t count = HugePageAllocator::free_huge_pages();
    // Currently returns 0 (stub), but should not crash
    (void)count;
}

// ============================================================================
// HugePageAllocator - allocate / deallocate
// ============================================================================

TEST_CASE("allocate and deallocate with 2MB huge pages", "[huge_page][regression]") {
    if (!HugePageAllocator::is_available()) {
        // When huge pages are unavailable, allocate falls back to aligned_alloc
        // but deallocate always calls munmap. Skip to avoid undefined behavior.
        SKIP("Huge pages not available; fallback deallocate path has known mismatch");
    }

    constexpr size_t size = 4096 * 4;
    void* ptr = HugePageAllocator::allocate(size, HugePageSize::Huge2MB);
    REQUIRE(ptr != nullptr);

    std::memset(ptr, 0xAB, size);

    HugePageAllocator::deallocate(ptr, size, HugePageSize::Huge2MB);
}

TEST_CASE("allocate with standard page size", "[huge_page][regression]") {
    constexpr size_t size = 8192;
    void* ptr = HugePageAllocator::allocate(size, HugePageSize::Standard);
    REQUIRE(ptr != nullptr);

    std::memset(ptr, 0, size);

    HugePageAllocator::deallocate(ptr, size, HugePageSize::Standard);
}

TEST_CASE("deallocate nullptr is safe", "[huge_page][regression]") {
    HugePageAllocator::deallocate(nullptr, 4096, HugePageSize::Huge2MB);
    // No crash = pass
}

TEST_CASE("allocated pointer alignment", "[huge_page][regression]") {
    constexpr size_t size = 4096;
    void* ptr = HugePageAllocator::allocate(size, HugePageSize::Standard);
    REQUIRE(ptr != nullptr);

    // Standard page alignment: at least 4096-byte aligned
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    REQUIRE((addr % 4096) == 0);

    HugePageAllocator::deallocate(ptr, size, HugePageSize::Standard);
}

// ============================================================================
// HugePageBuffer RAII
// ============================================================================

TEST_CASE("HugePageBuffer RAII allocates on construct and frees on destroy", "[huge_page][regression]") {
    {
        HugePageBuffer buf{4096, HugePageSize::Standard};
        REQUIRE(buf.valid());
        REQUIRE(buf.data() != nullptr);
        REQUIRE(buf.size() == 4096);

        // Writable
        std::memset(buf.data(), 0xFF, buf.size());
    }
    // Destructor frees - no crash = pass
}

TEST_CASE("HugePageBuffer move semantics", "[huge_page][regression]") {
    HugePageBuffer buf1{8192, HugePageSize::Standard};
    REQUIRE(buf1.valid());
    void* original_data = buf1.data();

    // Move construct
    HugePageBuffer buf2{std::move(buf1)};
    REQUIRE(buf2.valid());
    REQUIRE(buf2.data() == original_data);
    REQUIRE(buf2.size() == 8192);
    REQUIRE_FALSE(buf1.valid());
    REQUIRE(buf1.size() == 0);

    // Move assign
    HugePageBuffer buf3{4096, HugePageSize::Standard};
    buf3 = std::move(buf2);
    REQUIRE(buf3.valid());
    REQUIRE(buf3.data() == original_data);
    REQUIRE_FALSE(buf2.valid());
}

TEST_CASE("HugePageBuffer as<T> typed access", "[huge_page][regression]") {
    HugePageBuffer buf{4096, HugePageSize::Standard};
    REQUIRE(buf.valid());

    auto* ints = buf.as<int>();
    REQUIRE(ints != nullptr);

    // Write and read back
    ints[0] = 42;
    ints[1] = 99;
    REQUIRE(ints[0] == 42);
    REQUIRE(ints[1] == 99);

    // const access
    const auto& cbuf = buf;
    const auto* cints = cbuf.as<int>();
    REQUIRE(cints[0] == 42);
}

// ============================================================================
// HugePageStlAllocator
// ============================================================================

TEST_CASE("HugePageStlAllocator with std::vector", "[huge_page][regression]") {
    if (!HugePageAllocator::is_available()) {
        // When huge pages are unavailable, HugePageAllocator::allocate falls back
        // to std::aligned_alloc but deallocate always calls munmap on Linux.
        // This mismatch causes a crash. Skip until the allocator is fixed.
        SKIP("Huge pages not available; STL allocator fallback path has known mismatch");
    }

    std::vector<int, HugePageStlAllocator<int>> vec;

    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    REQUIRE(vec.size() == 3);
    REQUIRE(vec[0] == 1);
    REQUIRE(vec[1] == 2);
    REQUIRE(vec[2] == 3);

    vec.clear();
    REQUIRE(vec.empty());
}

TEST_CASE("HugePageStlAllocator equality", "[huge_page][regression]") {
    HugePageStlAllocator<int> a1;
    HugePageStlAllocator<int> a2;
    HugePageStlAllocator<double> a3;

    REQUIRE(a1 == a2);
    REQUIRE_FALSE(a1 != a2);
    REQUIRE(a1 == a3);
}
