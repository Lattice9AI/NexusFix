/// @file test_error_mapping.cpp
/// @brief Tests for error_mapping.hpp (TICKET_479 Phase 1B)

#include <catch2/catch_test_macros.hpp>

#include "nexusfix/platform/error_mapping.hpp"

using namespace nfx;

// ============================================================================
// map_socket_error POSIX
// ============================================================================

#if NFX_PLATFORM_POSIX

TEST_CASE("map_socket_error POSIX connection errors", "[platform][error][regression]") {
    REQUIRE(map_socket_error(0) == TransportErrorCode::None);
    REQUIRE(map_socket_error(ECONNREFUSED) == TransportErrorCode::ConnectionRefused);
    REQUIRE(map_socket_error(ECONNRESET) == TransportErrorCode::ConnectionReset);
    REQUIRE(map_socket_error(ECONNABORTED) == TransportErrorCode::ConnectionAborted);
    REQUIRE(map_socket_error(ENOTCONN) == TransportErrorCode::NotConnected);
    REQUIRE(map_socket_error(EPIPE) == TransportErrorCode::ConnectionClosed);
    REQUIRE(map_socket_error(ESHUTDOWN) == TransportErrorCode::ConnectionClosed);
}

TEST_CASE("map_socket_error POSIX network errors", "[platform][error][regression]") {
    REQUIRE(map_socket_error(ENETUNREACH) == TransportErrorCode::NetworkUnreachable);
    REQUIRE(map_socket_error(EHOSTUNREACH) == TransportErrorCode::HostUnreachable);
    REQUIRE(map_socket_error(ENETDOWN) == TransportErrorCode::NetworkUnreachable);
#ifdef EHOSTDOWN
    REQUIRE(map_socket_error(EHOSTDOWN) == TransportErrorCode::HostUnreachable);
#endif
}

TEST_CASE("map_socket_error POSIX timeout and async", "[platform][error][regression]") {
    REQUIRE(map_socket_error(ETIMEDOUT) == TransportErrorCode::Timeout);
    REQUIRE(map_socket_error(EAGAIN) == TransportErrorCode::WouldBlock);
    REQUIRE(map_socket_error(EINPROGRESS) == TransportErrorCode::InProgress);
    REQUIRE(map_socket_error(EALREADY) == TransportErrorCode::InProgress);
}

TEST_CASE("map_socket_error POSIX buffer and address", "[platform][error][regression]") {
    REQUIRE(map_socket_error(ENOBUFS) == TransportErrorCode::NoBufferSpace);
    REQUIRE(map_socket_error(ENOMEM) == TransportErrorCode::NoBufferSpace);
    REQUIRE(map_socket_error(EADDRNOTAVAIL) == TransportErrorCode::ConnectionFailed);
    REQUIRE(map_socket_error(EAFNOSUPPORT) == TransportErrorCode::ConnectionFailed);
}

TEST_CASE("map_socket_error POSIX socket errors", "[platform][error][regression]") {
    REQUIRE(map_socket_error(EBADF) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(ENOTSOCK) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EINVAL) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EMFILE) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(ENFILE) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EACCES) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EFAULT) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EPROTONOSUPPORT) == TransportErrorCode::SocketError);
    REQUIRE(map_socket_error(EINTR) == TransportErrorCode::SocketError);
}

TEST_CASE("map_socket_error unknown errno maps to SocketError", "[platform][error][regression]") {
    // Use a high errno unlikely to be a real code
    REQUIRE(map_socket_error(99999) == TransportErrorCode::SocketError);
}

#endif // NFX_PLATFORM_POSIX

// ============================================================================
// map_gai_error
// ============================================================================

#if NFX_PLATFORM_POSIX

TEST_CASE("map_gai_error POSIX", "[platform][error][regression]") {
    REQUIRE(map_gai_error(0) == TransportErrorCode::None);
    REQUIRE(map_gai_error(EAI_NONAME) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_AGAIN) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_FAIL) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_FAMILY) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_MEMORY) == TransportErrorCode::NoBufferSpace);
    REQUIRE(map_gai_error(EAI_SERVICE) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_SOCKTYPE) == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(map_gai_error(EAI_BADFLAGS) == TransportErrorCode::AddressResolutionFailed);
#ifdef EAI_OVERFLOW
    REQUIRE(map_gai_error(EAI_OVERFLOW) == TransportErrorCode::AddressResolutionFailed);
#endif
#ifdef EAI_NODATA
    REQUIRE(map_gai_error(EAI_NODATA) == TransportErrorCode::AddressResolutionFailed);
#endif
#ifdef EAI_ADDRFAMILY
    REQUIRE(map_gai_error(EAI_ADDRFAMILY) == TransportErrorCode::AddressResolutionFailed);
#endif
}

TEST_CASE("map_gai_error unknown code", "[platform][error][regression]") {
    // Unknown GAI code defaults to AddressResolutionFailed
    REQUIRE(map_gai_error(-99999) == TransportErrorCode::AddressResolutionFailed);
}

#endif // NFX_PLATFORM_POSIX

// ============================================================================
// Factory Functions
// ============================================================================

TEST_CASE("make_socket_error from specific errno", "[platform][error][regression]") {
#if NFX_PLATFORM_WINDOWS
    auto err = make_socket_error(WSAECONNREFUSED);
    REQUIRE(err.code == TransportErrorCode::ConnectionRefused);
    REQUIRE(err.system_errno == WSAECONNREFUSED);
#else
    auto err = make_socket_error(ECONNREFUSED);
    REQUIRE(err.code == TransportErrorCode::ConnectionRefused);
    REQUIRE(err.system_errno == ECONNREFUSED);
#endif
}

TEST_CASE("make_gai_error factory", "[platform][error][regression]") {
    auto err = make_gai_error(EAI_NONAME);
    REQUIRE(err.code == TransportErrorCode::AddressResolutionFailed);
    REQUIRE(err.system_errno == EAI_NONAME);
}

TEST_CASE("make_transport_error explicit construction", "[platform][error][regression]") {
    auto err = make_transport_error(TransportErrorCode::Timeout, 42);
    REQUIRE(err.code == TransportErrorCode::Timeout);
    REQUIRE(err.system_errno == 42);
}

TEST_CASE("make_transport_error default errno", "[platform][error][regression]") {
    auto err = make_transport_error(TransportErrorCode::ConnectionFailed);
    REQUIRE(err.code == TransportErrorCode::ConnectionFailed);
    REQUIRE(err.system_errno == 0);
}

// ============================================================================
// socket_error_string
// ============================================================================

TEST_CASE("socket_error_string returns non-empty", "[platform][error][regression]") {
#if NFX_PLATFORM_WINDOWS
    const char* desc = socket_error_string(WSAECONNREFUSED);
#else
    const char* desc = socket_error_string(ECONNREFUSED);
#endif
    REQUIRE(desc != nullptr);
    REQUIRE(std::string_view(desc).size() > 0);
}

TEST_CASE("socket_error_string for zero", "[platform][error][regression]") {
    const char* desc = socket_error_string(0);
    REQUIRE(desc != nullptr);
    // errno 0 = "Success" on most POSIX systems
}

// ============================================================================
// make_socket_error captures current errno
// ============================================================================

TEST_CASE("make_socket_error captures current errno", "[platform][error][regression]") {
#if NFX_PLATFORM_WINDOWS
    WSASetLastError(WSAETIMEDOUT);
    auto err = make_socket_error();
    REQUIRE(err.code == TransportErrorCode::Timeout);
    REQUIRE(err.system_errno == WSAETIMEDOUT);
    WSASetLastError(0);
#else
    errno = ETIMEDOUT;
    auto err = make_socket_error();
    REQUIRE(err.code == TransportErrorCode::Timeout);
    REQUIRE(err.system_errno == ETIMEDOUT);
    errno = 0;  // Reset
#endif
}
