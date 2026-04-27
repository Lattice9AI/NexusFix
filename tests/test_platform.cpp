#include <catch2/catch_test_macros.hpp>

#include "nexusfix/platform/platform.hpp"

#include <cstring>
#include <string_view>

using namespace nfx::platform;

// ============================================================================
// Compile-time Verification (static_assert)
// ============================================================================

// Platform functions must be usable in constant expressions
static_assert(name() != nullptr, "name() must return non-null");
static_assert(compiler_name() != nullptr, "compiler_name() must return non-null");
static_assert(arch_name() != nullptr, "arch_name() must return non-null");
static_assert(async_io_backend() != nullptr, "async_io_backend() must return non-null");

// Exactly one platform must be active
static_assert(static_cast<int>(is_linux()) + static_cast<int>(is_windows()) +
              static_cast<int>(is_macos()) == 1,
              "exactly one platform must be detected");

// POSIX consistency: Linux and macOS are POSIX, Windows is not
static_assert(!is_linux() || is_posix(), "Linux must be POSIX");
static_assert(!is_macos() || is_posix(), "macOS must be POSIX");
static_assert(!is_windows() || !is_posix(), "Windows must not be POSIX");

// ============================================================================
// Platform Name Functions
// ============================================================================

TEST_CASE("Platform name returns known string", "[platform]") {
    std::string_view n = name();
    REQUIRE(!n.empty());
    bool known = (n == "Linux" || n == "Windows" || n == "macOS");
    REQUIRE(known);
}

TEST_CASE("Compiler name returns known string", "[platform]") {
    std::string_view n = compiler_name();
    REQUIRE(!n.empty());
    bool known = (n == "GCC" || n == "Clang" || n == "MSVC" || n == "Unknown");
    REQUIRE(known);
}

TEST_CASE("Architecture name returns known string", "[platform]") {
    std::string_view n = arch_name();
    REQUIRE(!n.empty());
    bool known = (n == "x86_64" || n == "x86" || n == "ARM64" || n == "Unknown");
    REQUIRE(known);
}

TEST_CASE("Async I/O backend returns known string", "[platform]") {
    std::string_view n = async_io_backend();
    REQUIRE(!n.empty());
    bool known = (n == "io_uring" || n == "IOCP" || n == "kqueue" || n == "none");
    REQUIRE(known);
}

// ============================================================================
// Platform Boolean Consistency
// ============================================================================

TEST_CASE("Platform boolean functions are mutually exclusive", "[platform]") {
    int count = static_cast<int>(is_linux()) +
                static_cast<int>(is_windows()) +
                static_cast<int>(is_macos());
    REQUIRE(count == 1);
}

TEST_CASE("Platform name matches boolean queries", "[platform]") {
    std::string_view n = name();
    if (is_linux()) {
        REQUIRE(n == "Linux");
        REQUIRE(is_posix());
    } else if (is_windows()) {
        REQUIRE(n == "Windows");
        REQUIRE(!is_posix());
    } else if (is_macos()) {
        REQUIRE(n == "macOS");
        REQUIRE(is_posix());
    }
}

// ============================================================================
// Async I/O Backend Consistency
// ============================================================================

TEST_CASE("Async I/O backend consistency", "[platform]") {
    if (has_async_io()) {
        bool has_backend = has_io_uring() || has_iocp() || has_kqueue();
        REQUIRE(has_backend);

        std::string_view backend = async_io_backend();
        REQUIRE(backend != "none");
    } else {
        REQUIRE(!has_io_uring());
        REQUIRE(!has_iocp());
        REQUIRE(!has_kqueue());
        REQUIRE(std::string_view(async_io_backend()) == "none");
    }
}

// ============================================================================
// SIMD / Cache Line Macros
// ============================================================================

TEST_CASE("Cache line size is power of two", "[platform]") {
    constexpr auto cls = NFX_CACHE_LINE_SIZE;
    REQUIRE(cls >= 32);
    REQUIRE(cls <= 256);
    REQUIRE((cls & (cls - 1)) == 0);  // power of two
}

TEST_CASE("SIMD detection macros are 0 or 1", "[platform]") {
    REQUIRE((NFX_HAS_SSE42 == 0 || NFX_HAS_SSE42 == 1));
    REQUIRE((NFX_HAS_AVX2 == 0 || NFX_HAS_AVX2 == 1));
    REQUIRE((NFX_HAS_AVX512 == 0 || NFX_HAS_AVX512 == 1));
    REQUIRE((NFX_HAS_NEON == 0 || NFX_HAS_NEON == 1));
}
