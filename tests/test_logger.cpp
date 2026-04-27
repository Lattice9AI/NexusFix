#include <catch2/catch_test_macros.hpp>

#include "nexusfix/util/logger.hpp"

// ============================================================================
// Logger Tests
//
// When NFX_HAS_LOGGING is defined, these exercise the Quill-backed logger.
// When disabled, all functions are no-ops and macros expand to ((void)0).
// Both paths must compile and run without crashing.
// ============================================================================

TEST_CASE("Logger init with default config does not crash", "[logger][regression]") {
    nfx::logging::init();
    CHECK(true);
}

TEST_CASE("Logger get returns non-null after init", "[logger][regression]") {
    nfx::logging::init();
    auto* logger = nfx::logging::get();

#ifdef NFX_HAS_LOGGING
    REQUIRE(logger != nullptr);
#else
    CHECK(logger == nullptr);  // no-op stub returns nullptr
#endif
}

TEST_CASE("Logger shutdown after init is clean", "[logger][regression]") {
    nfx::logging::init();
    nfx::logging::shutdown();
    CHECK(true);
}

TEST_CASE("Logger double init is safe", "[logger][regression]") {
    nfx::logging::init();
    nfx::logging::init();  // Should not crash (Quill create_or_get semantics)
    nfx::logging::shutdown();
    CHECK(true);
}

TEST_CASE("NFX_LOG_INFO compiles and runs without crash", "[logger][regression]") {
    nfx::logging::init();
    NFX_LOG_INFO("test log message from unit test");
    nfx::logging::flush();
    nfx::logging::shutdown();
    CHECK(true);
}

TEST_CASE("Conditional logging macros compile and run", "[logger][regression]") {
    nfx::logging::init();

    NFX_LOG_INFO_IF(true, "should log");
    NFX_LOG_INFO_IF(false, "should not log");
    NFX_LOG_WARN_IF(true, "warn conditional");
    NFX_LOG_ERROR_IF(false, "error conditional");

    nfx::logging::flush();
    nfx::logging::shutdown();
    CHECK(true);
}
