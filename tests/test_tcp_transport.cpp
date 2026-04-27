#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstring>
#include <thread>
#include <chrono>

#include "nexusfix/transport/tcp_transport.hpp"

using namespace nfx;

// ============================================================================
// TcpSocket Unit Tests
// ============================================================================

TEST_CASE("TcpSocket: default state", "[transport][tcp][regression]") {
    TcpSocket sock;

    CHECK(sock.state() == ConnectionState::Disconnected);
    CHECK(sock.fd() == INVALID_SOCKET_HANDLE);
    CHECK_FALSE(sock.is_connected());
}

TEST_CASE("TcpSocket: create returns valid socket", "[transport][tcp][regression]") {
    TcpSocket sock;
    auto result = sock.create();

    REQUIRE(result.has_value());
    CHECK(sock.fd() != INVALID_SOCKET_HANDLE);

    sock.close();
}

TEST_CASE("TcpSocket: close on valid socket transitions to Disconnected", "[transport][tcp][regression]") {
    TcpSocket sock;
    REQUIRE(sock.create().has_value());
    CHECK(sock.fd() != INVALID_SOCKET_HANDLE);

    sock.close();

    CHECK(sock.state() == ConnectionState::Disconnected);
    CHECK(sock.fd() == INVALID_SOCKET_HANDLE);
}

TEST_CASE("TcpSocket: close on already-closed socket is idempotent", "[transport][tcp][regression]") {
    TcpSocket sock;

    // Close without ever creating - should not crash
    sock.close();
    CHECK(sock.state() == ConnectionState::Disconnected);

    // Create, close, close again
    REQUIRE(sock.create().has_value());
    sock.close();
    sock.close();  // second close must be safe
    CHECK(sock.state() == ConnectionState::Disconnected);
}

TEST_CASE("TcpSocket: set_nodelay on created socket", "[transport][tcp][regression]") {
    TcpSocket sock;
    REQUIRE(sock.create().has_value());

    CHECK(sock.set_nodelay(true));
    CHECK(sock.set_nodelay(false));

    sock.close();
}

TEST_CASE("TcpSocket: set_keepalive on created socket", "[transport][tcp][regression]") {
    TcpSocket sock;
    REQUIRE(sock.create().has_value());

    CHECK(sock.set_keepalive(true));
    CHECK(sock.set_keepalive(false));

    sock.close();
}

TEST_CASE("TcpSocket: is_connected returns false initially", "[transport][tcp][regression]") {
    TcpSocket sock;
    CHECK_FALSE(sock.is_connected());

    REQUIRE(sock.create().has_value());
    // Created but not connected
    CHECK_FALSE(sock.is_connected());

    sock.close();
}

// ============================================================================
// TcpAcceptor Unit Tests
// ============================================================================

TEST_CASE("TcpAcceptor: listen on ephemeral port", "[transport][tcp][regression]") {
    TcpAcceptor acceptor;
    auto result = acceptor.listen(0);

    REQUIRE(result.has_value());
    CHECK(acceptor.is_listening());
    CHECK(acceptor.local_port() > 0);

    acceptor.close();
    CHECK_FALSE(acceptor.is_listening());
}

// ============================================================================
// Loopback Integration Tests
// ============================================================================

TEST_CASE("TcpSocket: loopback connect, send, receive", "[transport][tcp][regression]") {
    TcpAcceptor acceptor;
    REQUIRE(acceptor.listen(0).has_value());
    uint16_t port = acceptor.local_port();

    // Connect initiator
    TcpSocket initiator;
    REQUIRE(initiator.connect("127.0.0.1", port).has_value());
    CHECK(initiator.is_connected());

    // Accept on server side
    auto accept_result = acceptor.accept();
    REQUIRE(accept_result.has_value());
    TcpSocket server_sock{*accept_result};
    CHECK(server_sock.is_connected());

    // Send from initiator
    const char msg[] = "hello loopback";
    auto send_result = initiator.send({msg, sizeof(msg) - 1});
    REQUIRE(send_result.has_value());
    CHECK(*send_result == sizeof(msg) - 1);

    // Receive on server
    std::array<char, 128> buf{};
    // Brief wait for data to arrive
    REQUIRE(server_sock.poll_read(500));
    auto recv_result = server_sock.receive({buf.data(), buf.size()});
    REQUIRE(recv_result.has_value());
    CHECK(*recv_result == sizeof(msg) - 1);
    CHECK(std::string_view(buf.data(), *recv_result) == "hello loopback");

    initiator.close();
    server_sock.close();
    acceptor.close();
}

TEST_CASE("TcpSocket: connect to refused port returns error", "[transport][tcp][regression]") {
    TcpSocket sock;
    // Port 1 should not be listening on localhost
    auto result = sock.connect("127.0.0.1", 1);
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(sock.is_connected());

    auto& err = result.error();
    // Should be a connection-related error code
    CHECK(err.code != TransportErrorCode::None);
}
