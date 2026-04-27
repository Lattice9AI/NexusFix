#include <catch2/catch_test_macros.hpp>

#include "nexusfix/util/memory_lock.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
    #include <malloc.h>
#endif

namespace {
inline void* portable_aligned_alloc(size_t alignment, size_t size) {
#ifdef _MSC_VER
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}
inline void portable_aligned_free(void* ptr) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
} // namespace

using namespace nfx::util;

// ============================================================================
// MemoryLockError
// ============================================================================

TEST_CASE("MemoryLockError default is no error", "[memory_lock][regression]") {
    MemoryLockError err;
    REQUIRE(err.ok());
    REQUIRE(err.code == MemoryLockErrorCode::None);
    REQUIRE(err.system_errno == 0);
    REQUIRE(err.message() == "No error");
}

TEST_CASE("MemoryLockError messages are non-empty for all codes", "[memory_lock][regression]") {
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::None}.message().empty());
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::InsufficientPrivileges}.message().empty());
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::InsufficientMemory}.message().empty());
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::NotSupported}.message().empty());
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::SystemError}.message().empty());

    // Non-None codes are not ok
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::InsufficientPrivileges}.ok());
    REQUIRE_FALSE(MemoryLockError{MemoryLockErrorCode::SystemError}.ok());
}

// ============================================================================
// get_memlock_limit
// ============================================================================

#ifndef _WIN32
TEST_CASE("get_memlock_limit returns a value", "[memory_lock][regression]") {
    std::size_t limit = get_memlock_limit();
    // On most systems the limit is non-zero (at least 64KB default)
    // but we just check it doesn't crash and returns something
    // Just verify the call succeeds without crash
    (void)limit;
}
#endif

// ============================================================================
// lock_memory / unlock_memory on small buffer
// ============================================================================

TEST_CASE("lock_memory on small buffer", "[memory_lock][regression]") {
    // Allocate a page-aligned buffer
    constexpr std::size_t buf_size = 4096;
    void* buf = portable_aligned_alloc(4096, buf_size);
    REQUIRE(buf != nullptr);
    std::memset(buf, 0, buf_size);

    auto result = lock_memory(buf, buf_size);

    if (result.has_value()) {
        // Lock succeeded, unlock should be safe
        unlock_memory(buf, buf_size);
    } else {
        // May fail due to insufficient privileges (EPERM) or limit (ENOMEM)
        auto err = result.error();
        REQUIRE((err.code == MemoryLockErrorCode::InsufficientPrivileges ||
                 err.code == MemoryLockErrorCode::InsufficientMemory ||
                 err.code == MemoryLockErrorCode::SystemError));
    }

    portable_aligned_free(buf);
}

// ============================================================================
// prefault_memory
// ============================================================================

TEST_CASE("prefault_memory touches all pages without crash", "[memory_lock][regression]") {
    constexpr std::size_t buf_size = 16384;  // 4 pages
    std::vector<char> buf(buf_size, 0);

    // Should not crash
    prefault_memory(buf.data(), buf.size());

    // Verify buffer is still intact
    REQUIRE(buf[0] == 0);
    REQUIRE(buf[buf_size - 1] == 0);
}

TEST_CASE("prefault_memory_write touches all pages with write", "[memory_lock][regression]") {
    constexpr std::size_t buf_size = 16384;
    std::vector<char> buf(buf_size, 'A');

    // Should not crash, read-modify-write pattern
    prefault_memory_write(buf.data(), buf.size());

    // Buffer values preserved (read-modify-write reads then writes same value)
    REQUIRE(buf[0] == 'A');
    REQUIRE(buf[4096] == 'A');
    REQUIRE(buf[buf_size - 1] == 'A');
}

TEST_CASE("prefault_memory with zero length is safe", "[memory_lock][regression]") {
    char buf[1] = {0};
    prefault_memory(buf, 0);
    prefault_memory_write(buf, 0);
    // No crash = pass
}

// ============================================================================
// advise_memory
// ============================================================================

#ifndef _WIN32
TEST_CASE("advise_memory with Sequential advice", "[memory_lock][regression]") {
    // madvise requires page-aligned address
    constexpr std::size_t buf_size = 4096 * 4;
    void* buf = portable_aligned_alloc(4096, buf_size);
    REQUIRE(buf != nullptr);
    std::memset(buf, 0, buf_size);

    auto result = advise_memory(buf, buf_size, MemoryAdvice::Sequential);
    REQUIRE(result.has_value());

    portable_aligned_free(buf);
}

TEST_CASE("advise_memory with Random advice", "[memory_lock][regression]") {
    constexpr std::size_t buf_size = 4096 * 4;
    void* buf = portable_aligned_alloc(4096, buf_size);
    REQUIRE(buf != nullptr);
    std::memset(buf, 0, buf_size);

    auto result = advise_memory(buf, buf_size, MemoryAdvice::Random);
    REQUIRE(result.has_value());

    portable_aligned_free(buf);
}
#endif

// ============================================================================
// ScopedMemoryLock RAII
// ============================================================================

TEST_CASE("ScopedMemoryLock RAII construction and destruction", "[memory_lock][regression]") {
    // May or may not lock depending on privileges
    ScopedMemoryLock lock;
    // is_locked() reflects whether mlockall succeeded
    // On unprivileged systems this will be false - that's expected
    (void)lock.is_locked();

    // Destructor unlocks - no crash = pass
}

// ============================================================================
// ScopedRangeLock RAII
// ============================================================================

TEST_CASE("ScopedRangeLock RAII locks and unlocks range", "[memory_lock][regression]") {
    constexpr std::size_t buf_size = 4096;
    void* buf = portable_aligned_alloc(4096, buf_size);
    REQUIRE(buf != nullptr);
    std::memset(buf, 0, buf_size);

    {
        ScopedRangeLock lock{buf, buf_size};
        // May or may not lock depending on privileges
        (void)lock.is_locked();
    }
    // Destructor unlocks - no crash = pass

    portable_aligned_free(buf);
}
