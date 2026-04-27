/// @file test_transport_factory.cpp
/// @brief Tests for TransportFactory (TICKET_479 Phase 1C)

#include <catch2/catch_test_macros.hpp>

#include "nexusfix/transport/transport_factory.hpp"

using namespace nfx;

// ============================================================================
// Factory Creation
// ============================================================================

TEST_CASE("TransportFactory create_simple", "[transport][factory][regression]") {
    auto transport = TransportFactory::create_simple();
    REQUIRE(transport != nullptr);
    REQUIRE_FALSE(transport->is_connected());
}

TEST_CASE("TransportFactory create default", "[transport][factory][regression]") {
    auto transport = TransportFactory::create();
    REQUIRE(transport != nullptr);
    REQUIRE_FALSE(transport->is_connected());
}

TEST_CASE("TransportFactory create_best", "[transport][factory][regression]") {
    auto transport = TransportFactory::create_best();
    REQUIRE(transport != nullptr);
    REQUIRE_FALSE(transport->is_connected());
}

TEST_CASE("TransportFactory create with preferences", "[transport][factory][regression]") {
    SECTION("Simple preference") {
        auto t = TransportFactory::create(TransportPreference::Simple);
        REQUIRE(t != nullptr);
    }

    SECTION("HighPerf preference") {
        auto t = TransportFactory::create(TransportPreference::HighPerf);
        REQUIRE(t != nullptr);
    }

    SECTION("TcpPosix preference") {
        auto t = TransportFactory::create(TransportPreference::TcpPosix);
        REQUIRE(t != nullptr);
    }

    SECTION("IoUring preference falls back gracefully") {
        auto t = TransportFactory::create(TransportPreference::IoUring);
        REQUIRE(t != nullptr);
    }

    SECTION("Iocp preference falls back gracefully") {
        auto t = TransportFactory::create(TransportPreference::Iocp);
        REQUIRE(t != nullptr);
    }

    SECTION("Kqueue preference falls back gracefully") {
        auto t = TransportFactory::create(TransportPreference::Kqueue);
        REQUIRE(t != nullptr);
    }

    SECTION("Winsock preference maps to simple") {
        auto t = TransportFactory::create(TransportPreference::Winsock);
        REQUIRE(t != nullptr);
    }
}

// ============================================================================
// Platform Information
// ============================================================================

TEST_CASE("TransportFactory platform_name", "[transport][factory][regression]") {
    const char* name = TransportFactory::platform_name();
    REQUIRE(name != nullptr);
    REQUIRE(std::string_view(name).size() > 0);
}

TEST_CASE("TransportFactory async_backend_name", "[transport][factory][regression]") {
    const char* name = TransportFactory::async_backend_name();
    REQUIRE(name != nullptr);
    // May be "none" on platforms without async I/O
}

TEST_CASE("TransportFactory default_transport_name", "[transport][factory][regression]") {
    const char* name = TransportFactory::default_transport_name();
    REQUIRE(name != nullptr);
    REQUIRE(std::string_view(name).size() > 0);
}

// ============================================================================
// Feature Detection
// ============================================================================

TEST_CASE("TransportFactory feature detection", "[transport][factory][regression]") {
    // These are compile-time constants; just verify they compile and are consistent
    [[maybe_unused]] bool async = TransportFactory::has_async_io();
    [[maybe_unused]] bool uring = TransportFactory::has_io_uring();
    [[maybe_unused]] bool iocp = TransportFactory::has_iocp();
    [[maybe_unused]] bool kq = TransportFactory::has_kqueue();

    // If io_uring is available, async_io should be true
    if (uring) {
        REQUIRE(async);
    }
    // If IOCP is available, async_io should be true
    if (iocp) {
        REQUIRE(async);
    }
}

// ============================================================================
// Convenience Functions
// ============================================================================

TEST_CASE("Convenience transport creation functions", "[transport][factory][regression]") {
    SECTION("make_transport") {
        auto t = make_transport();
        REQUIRE(t != nullptr);
    }

    SECTION("make_simple_transport") {
        auto t = make_simple_transport();
        REQUIRE(t != nullptr);
    }

    SECTION("make_fast_transport") {
        auto t = make_fast_transport();
        REQUIRE(t != nullptr);
    }
}

// ============================================================================
// Platform Type Aliases
// ============================================================================

TEST_CASE("Platform type aliases compile and construct", "[transport][factory][regression]") {
    PlatformSocket sock;
    REQUIRE_FALSE(sock.is_connected());

    PlatformTransport pt;
    REQUIRE_FALSE(pt.is_connected());
}
