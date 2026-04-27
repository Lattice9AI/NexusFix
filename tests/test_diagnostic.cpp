#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "nexusfix/util/diagnostic.hpp"

using namespace nfx::util;

// ============================================================================
// SourceLoc Tests
// ============================================================================

TEST_CASE("SourceLoc::current captures file and line", "[diagnostic][regression]") {
    auto loc = SourceLoc::current();

    CHECK(loc.file != nullptr);
    CHECK(loc.function != nullptr);
    CHECK(loc.line > 0);
    // file should contain this test file name
    CHECK(std::strstr(loc.file, "test_diagnostic.cpp") != nullptr);
}

TEST_CASE("SourceLoc::file_basename extracts filename from path", "[diagnostic][regression]") {
    SECTION("Unix path") {
        SourceLoc loc{"/home/user/src/test_diagnostic.cpp", "func", 42, 1};
        CHECK(std::strcmp(loc.file_basename(), "test_diagnostic.cpp") == 0);
    }

    SECTION("Windows path") {
        SourceLoc loc{"C:\\Users\\dev\\src\\test_diagnostic.cpp", "func", 42, 1};
        CHECK(std::strcmp(loc.file_basename(), "test_diagnostic.cpp") == 0);
    }

    SECTION("No path separator") {
        SourceLoc loc{"test_diagnostic.cpp", "func", 42, 1};
        CHECK(std::strcmp(loc.file_basename(), "test_diagnostic.cpp") == 0);
    }

    SECTION("Trailing separator") {
        SourceLoc loc{"/path/to/", "func", 42, 1};
        // After last '/', empty string
        CHECK(std::strcmp(loc.file_basename(), "") == 0);
    }
}

// ============================================================================
// Macro Tests (no-op when NFX_DEBUG_DIAGNOSTICS is not defined)
// ============================================================================

TEST_CASE("NFX_TRACE_POINT compiles and does not crash", "[diagnostic][regression]") {
    // When NFX_DEBUG_DIAGNOSTICS is not defined, these are no-ops
    NFX_TRACE_POINT("test trace point");
    // If we get here without crash, the test passes
    CHECK(true);
}

TEST_CASE("NFX_TRACE_VALUE compiles for integral types", "[diagnostic][regression]") {
    [[maybe_unused]] int x = 42;
    NFX_TRACE_VALUE(x);
    CHECK(true);
}

TEST_CASE("NFX_TRACE_VALUE compiles for pointer types", "[diagnostic][regression]") {
    [[maybe_unused]] int val = 0;
    [[maybe_unused]] int* ptr = &val;
    NFX_TRACE_VALUE(ptr);
    CHECK(true);
}

TEST_CASE("NFX_DEBUG_ASSERT compiles as no-op when disabled", "[diagnostic][regression]") {
    // When NFX_DEBUG_DIAGNOSTICS is not defined, this is ((void)0)
    NFX_DEBUG_ASSERT(true, "should be no-op");
    NFX_DEBUG_ASSERT(false, "should also be no-op when disabled");
    CHECK(true);
}

TEST_CASE("NFX_SCOPED_TRACE compiles as no-op when disabled", "[diagnostic][regression]") {
    NFX_SCOPED_TRACE("test scope");
    CHECK(true);
}

// ============================================================================
// NFX_INVARIANT (always enabled)
// ============================================================================

TEST_CASE("NFX_INVARIANT passes on true condition", "[diagnostic][regression]") {
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4127)  // conditional expression is constant
#endif
    NFX_INVARIANT(1 == 1, "basic truth");
    NFX_INVARIANT(true, "boolean true");
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
    CHECK(true);
}

// Note: NFX_INVARIANT(false, ...) calls abort() so we cannot test failure
// without a subprocess death test, which Catch2 does not natively support.

// ============================================================================
// Direct function tests (always compiled regardless of NFX_DEBUG_DIAGNOSTICS)
// ============================================================================

TEST_CASE("trace_value compiles for all type branches", "[diagnostic][regression]") {
    // These functions exist regardless of the macro flag.
    // We call them directly to verify template instantiation.
    auto loc = std::source_location::current();

    // Integral branch
    trace_value("int_val", 42, loc);

    // Floating-point branch
    trace_value("double_val", 3.14, loc);

    // Pointer branch
    int x = 0;
    trace_value("ptr_val", &x, loc);

    CHECK(true);
}
