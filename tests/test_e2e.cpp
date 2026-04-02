#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>

#include "nexusfix/session/session_manager.hpp"
#include "nexusfix/transport/tcp_transport.hpp"
#include "nexusfix/messages/fix44/execution_report.hpp"
#include "nexusfix/messages/fix44/new_order_single.hpp"
#include "nexusfix/parser/runtime_parser.hpp"

using namespace nfx;

// ============================================================================
// Helper: SocketBridge
// Bridges TCP byte stream to SessionManager::on_data_received()
// Uses StreamParser for FIX message framing
// ============================================================================

namespace {

class SocketBridge {
public:
    explicit SocketBridge(TcpSocket& sock, SessionManager& session) noexcept
        : socket_{sock}, session_{session} {}

    /// Poll socket once, feed complete messages to session
    /// Returns true if data was received
    bool poll_once(int timeout_ms = 10) noexcept {
        if (!socket_.is_connected()) return false;
        if (!socket_.poll_read(timeout_ms)) return false;

        // Read into buffer after any unconsumed data
        auto space = std::span<char>{
            buffer_.data() + buffered_,
            buffer_.size() - buffered_
        };
        if (space.empty()) return false;

        auto result = socket_.receive(space);
        if (!result.has_value()) return false;

        size_t received = *result;
        if (received == 0) return false;

        buffered_ += received;

        // Feed to StreamParser for message framing
        auto data = std::span<const char>{buffer_.data(), buffered_};
        size_t consumed = parser_.feed(data);

        // Dispatch complete messages to session
        while (parser_.has_message()) {
            auto [start, end] = parser_.next_message();
            auto msg_span = data.subspan(start, end - start);
            session_.on_data_received(msg_span);
        }

        // Compact buffer: move unconsumed data to front
        if (consumed > 0) {
            size_t remaining = buffered_ - consumed;
            if (remaining > 0) {
                std::memmove(buffer_.data(), buffer_.data() + consumed, remaining);
            }
            buffered_ = remaining;
        }

        return true;
    }

private:
    TcpSocket& socket_;
    SessionManager& session_;
    StreamParser parser_;
    std::array<char, 8192> buffer_{};
    size_t buffered_{0};
};

// ============================================================================
// Helper: AcceptorEndpoint
// Runs a FIX acceptor in a separate thread
// ============================================================================

class AcceptorEndpoint {
public:
    AcceptorEndpoint() {
        config_.sender_comp_id = "ACCEPTOR";
        config_.target_comp_id = "INITIATOR";
        config_.heart_bt_int = 30;
        config_.validate_comp_ids = false;
    }

    ~AcceptorEndpoint() {
        stop();
    }

    AcceptorEndpoint(const AcceptorEndpoint&) = delete;
    AcceptorEndpoint& operator=(const AcceptorEndpoint&) = delete;

    /// Start listening on ephemeral port, return the port
    uint16_t start() {
        auto result = acceptor_.listen(0);
        REQUIRE(result.has_value());
        port_ = acceptor_.local_port();
        REQUIRE(port_ != 0);

        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] { run(); });
        return port_;
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        acceptor_.close();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// Wait for logon to complete (with timeout)
    bool wait_for_logon(int timeout_ms = 2000) const {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (logon_complete.load(std::memory_order_acquire)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    /// Wait for an app message to be received
    bool wait_for_app_message(int timeout_ms = 2000) const {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (app_message_received.load(std::memory_order_acquire)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    /// Wait for logout to complete
    bool wait_for_logout(int timeout_ms = 2000) const {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (logout_complete.load(std::memory_order_acquire)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    std::atomic<bool> logon_complete{false};
    std::atomic<bool> app_message_received{false};
    std::atomic<bool> logout_complete{false};
    std::atomic<bool> er_sent{false};

private:
    void run() {
        // Accept one connection
        auto accept_result = acceptor_.accept();
        if (!accept_result.has_value()) return;

        TcpSocket client_sock{*accept_result};
        SessionManager session{config_};

        // Wire callbacks
        SessionCallbacks cbs;
        cbs.on_send = [&client_sock](std::span<const char> data) -> bool {
            auto result = client_sock.send(data);
            return result.has_value() && *result > 0;
        };
        cbs.on_logon = [this]() {
            logon_complete.store(true, std::memory_order_release);
        };
        cbs.on_logout = [this]([[maybe_unused]] std::string_view text) {
            logout_complete.store(true, std::memory_order_release);
        };
        cbs.on_app_message = [this, &session](const ParsedMessage& msg) {
            app_message_received.store(true, std::memory_order_release);

            // If NOS received, respond with ExecutionReport
            if (msg.msg_type() == msg_type::NewOrderSingle) {
                auto er_builder = fix44::ExecutionReport::Builder{}
                    .order_id("ORD001")
                    .exec_id("EXEC001")
                    .exec_type(ExecType::New)
                    .ord_status(OrdStatus::New)
                    .symbol(msg.get_string(tag::Symbol::value))
                    .side(Side::Buy)
                    .leaves_qty(Qty::from_int(100))
                    .cum_qty(Qty::from_int(0))
                    .avg_px(FixedPrice::from_double(0.0))
                    .cl_ord_id(msg.get_string(tag::ClOrdID::value))
                    .transact_time("20260401-12:00:00.000");
                (void)session.send_app_message(er_builder);
                er_sent.store(true, std::memory_order_release);
            }
        };
        session.set_callbacks(std::move(cbs));

        // Acceptor: on_connect -> wait for incoming Logon
        session.on_connect();

        SocketBridge bridge{client_sock, session};

        // Poll loop
        while (running_.load(std::memory_order_acquire) && client_sock.is_connected()) {
            bridge.poll_once(20);
        }

        // If session is still active, do graceful cleanup
        if (client_sock.is_connected()) {
            session.on_disconnect();
        }
    }

    SessionConfig config_;
    TcpAcceptor acceptor_;
    uint16_t port_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// ============================================================================
// Helper: InitiatorEndpoint
// Client-side FIX session over TCP
// ============================================================================

class InitiatorEndpoint {
public:
    InitiatorEndpoint() {
        config_.sender_comp_id = "INITIATOR";
        config_.target_comp_id = "ACCEPTOR";
        config_.heart_bt_int = 30;
        config_.validate_comp_ids = false;

        session_ = std::make_unique<SessionManager>(config_);

        SessionCallbacks cbs;
        cbs.on_send = [this](std::span<const char> data) -> bool {
            auto result = socket_.send(data);
            return result.has_value() && *result > 0;
        };
        cbs.on_state_change = [this](SessionState from, SessionState to) {
            state_changes_.emplace_back(from, to);
        };
        cbs.on_app_message = [this](const ParsedMessage& msg) {
            // Store raw data since ParsedMessage holds string_views into the buffer
            app_msg_types_.push_back(msg.msg_type());
            app_message_count_.fetch_add(1, std::memory_order_release);
        };
        cbs.on_logon = [this]() {
            logon_received_ = true;
        };
        cbs.on_logout = [this](std::string_view text) {
            logout_received_ = true;
            logout_text_ = std::string(text);
        };
        cbs.on_error = [this]([[maybe_unused]] const SessionError& err) {
            error_count_++;
        };
        session_->set_callbacks(std::move(cbs));
    }

    /// Connect to acceptor
    bool connect(uint16_t port) {
        auto result = socket_.connect("127.0.0.1", port);
        if (!result.has_value()) return false;
        session_->on_connect();
        bridge_ = std::make_unique<SocketBridge>(socket_, *session_);
        return true;
    }

    /// Poll once for incoming data
    bool poll_once(int timeout_ms = 10) {
        if (!bridge_) return false;
        return bridge_->poll_once(timeout_ms);
    }

    /// Poll until predicate is true or timeout
    bool poll_until(std::function<bool()> pred, int timeout_ms = 2000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            poll_once(10);
            if (pred()) return true;
        }
        return false;
    }

    SessionManager& session() { return *session_; }
    TcpSocket& socket() { return socket_; }
    bool logon_received() const { return logon_received_; }
    bool logout_received() const { return logout_received_; }
    const std::string& logout_text() const { return logout_text_; }
    size_t app_message_count() const { return app_message_count_.load(std::memory_order_acquire); }
    const std::vector<char>& app_msg_types() const { return app_msg_types_; }
    int error_count() const { return error_count_; }

private:
    SessionConfig config_;
    TcpSocket socket_;
    std::unique_ptr<SessionManager> session_;
    std::unique_ptr<SocketBridge> bridge_;
    std::vector<std::pair<SessionState, SessionState>> state_changes_;
    std::vector<char> app_msg_types_;
    std::atomic<size_t> app_message_count_{0};
    bool logon_received_{false};
    bool logout_received_{false};
    std::string logout_text_;
    int error_count_{0};
};

}  // anonymous namespace

// ============================================================================
// End-to-End Tests
// ============================================================================

TEST_CASE("E2E: Logon handshake over TCP", "[e2e]") {
    AcceptorEndpoint acceptor;
    uint16_t port = acceptor.start();

    InitiatorEndpoint initiator;
    REQUIRE(initiator.connect(port));

    // Initiate logon
    auto result = initiator.session().initiate_logon();
    REQUIRE(result.has_value());

    // Poll initiator until logon completes
    bool logon_ok = initiator.poll_until([&] {
        return initiator.logon_received();
    });

    REQUIRE(logon_ok);
    REQUIRE(initiator.session().state() == SessionState::Active);
    REQUIRE(acceptor.wait_for_logon());

    acceptor.stop();
}

TEST_CASE("E2E: Application message roundtrip", "[e2e]") {
    AcceptorEndpoint acceptor;
    uint16_t port = acceptor.start();

    InitiatorEndpoint initiator;
    REQUIRE(initiator.connect(port));

    // Logon
    (void)initiator.session().initiate_logon();
    REQUIRE(initiator.poll_until([&] { return initiator.logon_received(); }));
    REQUIRE(acceptor.wait_for_logon());

    // Send NOS from initiator via session layer (proper sequence tracking)
    auto nos_builder = fix44::NewOrderSingle::Builder{}
        .cl_ord_id("ORDER001")
        .symbol("AAPL")
        .side(Side::Buy)
        .transact_time("20260401-12:00:00.000")
        .order_qty(Qty::from_int(100))
        .ord_type(OrdType::Limit)
        .price(FixedPrice::from_double(150.25));
    auto nos_result = initiator.session().send_app_message(nos_builder);
    REQUIRE(nos_result.has_value());

    // Wait for acceptor to receive NOS and send ER back
    REQUIRE(acceptor.wait_for_app_message());
    REQUIRE(acceptor.er_sent.load(std::memory_order_acquire));

    // Poll initiator to receive the ExecutionReport
    bool er_received = initiator.poll_until([&] {
        return initiator.app_message_count() > 0;
    });

    REQUIRE(er_received);
    REQUIRE(initiator.app_msg_types()[0] == msg_type::ExecutionReport);

    acceptor.stop();
}

TEST_CASE("E2E: Graceful logout", "[e2e]") {
    AcceptorEndpoint acceptor;
    uint16_t port = acceptor.start();

    InitiatorEndpoint initiator;
    REQUIRE(initiator.connect(port));

    // Logon
    (void)initiator.session().initiate_logon();
    REQUIRE(initiator.poll_until([&] { return initiator.logon_received(); }));
    REQUIRE(acceptor.wait_for_logon());

    // Initiate logout from initiator
    auto result = initiator.session().initiate_logout("Test done");
    REQUIRE(result.has_value());
    REQUIRE(initiator.session().state() == SessionState::LogoutPending);

    // Poll initiator to receive logout response
    bool logout_ok = initiator.poll_until([&] {
        return initiator.logout_received();
    });

    REQUIRE(logout_ok);
    REQUIRE(initiator.session().state() == SessionState::Disconnected);
    REQUIRE(acceptor.wait_for_logout());

    acceptor.stop();
}

TEST_CASE("E2E: Connection refused", "[e2e]") {
    // Try to connect to a port where nothing is listening
    TcpSocket sock;
    auto result = sock.connect("127.0.0.1", 1);  // Port 1 should not be listening
    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(sock.is_connected());
}

TEST_CASE("E2E: Acceptor drops connection mid-session", "[e2e]") {
    AcceptorEndpoint acceptor;
    uint16_t port = acceptor.start();

    InitiatorEndpoint initiator;
    REQUIRE(initiator.connect(port));

    // Logon
    (void)initiator.session().initiate_logon();
    REQUIRE(initiator.poll_until([&] { return initiator.logon_received(); }));
    REQUIRE(acceptor.wait_for_logon());

    // Kill the acceptor (closes its socket)
    acceptor.stop();

    // Give the OS a moment to propagate the close
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Initiator should detect disconnect on next poll/send
    // Try reading - should get connection closed
    initiator.poll_once(50);
    // The socket state should reflect the disconnect
    // (recv returns 0 which sets state to Disconnected)
    REQUIRE_FALSE(initiator.socket().is_connected());
}

TEST_CASE("E2E: Full session lifecycle", "[e2e]") {
    AcceptorEndpoint acceptor;
    uint16_t port = acceptor.start();

    InitiatorEndpoint initiator;

    // Phase 1: Connect
    REQUIRE(initiator.connect(port));
    REQUIRE(initiator.session().state() == SessionState::SocketConnected);

    // Phase 2: Logon
    (void)initiator.session().initiate_logon();
    REQUIRE(initiator.poll_until([&] { return initiator.logon_received(); }));
    REQUIRE(initiator.session().state() == SessionState::Active);
    REQUIRE(acceptor.wait_for_logon());

    // Phase 3: Send order via session layer
    auto nos_builder = fix44::NewOrderSingle::Builder{}
        .cl_ord_id("ORDER001")
        .symbol("AAPL")
        .side(Side::Buy)
        .transact_time("20260401-12:00:00.000")
        .order_qty(Qty::from_int(100))
        .ord_type(OrdType::Limit)
        .price(FixedPrice::from_double(150.25));
    auto nos_result = initiator.session().send_app_message(nos_builder);
    REQUIRE(nos_result.has_value());

    // Phase 4: Receive ExecutionReport
    REQUIRE(acceptor.wait_for_app_message());
    bool er_received = initiator.poll_until([&] {
        return initiator.app_message_count() > 0;
    });
    REQUIRE(er_received);
    REQUIRE(initiator.app_msg_types()[0] == msg_type::ExecutionReport);

    // Phase 5: Logout
    auto logout_result = initiator.session().initiate_logout();
    REQUIRE(logout_result.has_value());

    bool logout_ok = initiator.poll_until([&] {
        return initiator.logout_received();
    });
    REQUIRE(logout_ok);
    REQUIRE(initiator.session().state() == SessionState::Disconnected);
    REQUIRE(acceptor.wait_for_logout());

    acceptor.stop();
}
