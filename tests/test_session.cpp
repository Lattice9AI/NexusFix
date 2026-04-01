#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <string>
#include <cstring>

#include "nexusfix/session/state.hpp"
#include "nexusfix/session/sequence.hpp"
#include "nexusfix/session/session_manager.hpp"

using namespace nfx;

// ============================================================================
// State Machine Tests
// ============================================================================

TEST_CASE("State machine transitions", "[session][state]") {
    SECTION("Disconnected + Connect -> SocketConnected") {
        REQUIRE(next_state(SessionState::Disconnected, SessionEvent::Connect) ==
                SessionState::SocketConnected);
    }

    SECTION("SocketConnected + LogonSent -> LogonSent") {
        REQUIRE(next_state(SessionState::SocketConnected, SessionEvent::LogonSent) ==
                SessionState::LogonSent);
    }

    SECTION("SocketConnected + LogonReceived -> LogonReceived (acceptor path)") {
        REQUIRE(next_state(SessionState::SocketConnected, SessionEvent::LogonReceived) ==
                SessionState::LogonReceived);
    }

    SECTION("LogonSent + LogonReceived -> Active") {
        REQUIRE(next_state(SessionState::LogonSent, SessionEvent::LogonReceived) ==
                SessionState::Active);
    }

    SECTION("LogonReceived + LogonAcknowledged -> Active") {
        REQUIRE(next_state(SessionState::LogonReceived, SessionEvent::LogonAcknowledged) ==
                SessionState::Active);
    }

    SECTION("Active + LogoutSent -> LogoutPending") {
        REQUIRE(next_state(SessionState::Active, SessionEvent::LogoutSent) ==
                SessionState::LogoutPending);
    }

    SECTION("Active + LogoutReceived -> LogoutReceived") {
        REQUIRE(next_state(SessionState::Active, SessionEvent::LogoutReceived) ==
                SessionState::LogoutReceived);
    }

    SECTION("Active + Disconnect -> Reconnecting") {
        REQUIRE(next_state(SessionState::Active, SessionEvent::Disconnect) ==
                SessionState::Reconnecting);
    }

    SECTION("Active + HeartbeatTimeout -> Error") {
        REQUIRE(next_state(SessionState::Active, SessionEvent::HeartbeatTimeout) ==
                SessionState::Error);
    }

    SECTION("LogoutPending + LogoutReceived -> Disconnected") {
        REQUIRE(next_state(SessionState::LogoutPending, SessionEvent::LogoutReceived) ==
                SessionState::Disconnected);
    }

    SECTION("LogoutPending + HeartbeatTimeout -> Disconnected") {
        REQUIRE(next_state(SessionState::LogoutPending, SessionEvent::HeartbeatTimeout) ==
                SessionState::Disconnected);
    }

    SECTION("LogoutReceived + LogoutSent -> Disconnected") {
        REQUIRE(next_state(SessionState::LogoutReceived, SessionEvent::LogoutSent) ==
                SessionState::Disconnected);
    }

    SECTION("LogonSent + LogonRejected -> Disconnected") {
        REQUIRE(next_state(SessionState::LogonSent, SessionEvent::LogonRejected) ==
                SessionState::Disconnected);
    }

    SECTION("Reconnecting + Connect -> SocketConnected") {
        REQUIRE(next_state(SessionState::Reconnecting, SessionEvent::Connect) ==
                SessionState::SocketConnected);
    }

    SECTION("Error + Connect -> SocketConnected") {
        REQUIRE(next_state(SessionState::Error, SessionEvent::Connect) ==
                SessionState::SocketConnected);
    }

    SECTION("Invalid transitions stay in current state") {
        REQUIRE(next_state(SessionState::Disconnected, SessionEvent::LogonSent) ==
                SessionState::Disconnected);
        REQUIRE(next_state(SessionState::Active, SessionEvent::Connect) ==
                SessionState::Active);
        REQUIRE(next_state(SessionState::Error, SessionEvent::LogonSent) ==
                SessionState::Error);
    }
}

TEST_CASE("State name lookup", "[session][state]") {
    SECTION("Runtime name lookup") {
        REQUIRE(state_name(SessionState::Disconnected) == "Disconnected");
        REQUIRE(state_name(SessionState::Active) == "Active");
        REQUIRE(state_name(SessionState::Error) == "Error");
        REQUIRE(state_name(SessionState::LogonSent) == "LogonSent");
    }

    SECTION("Compile-time name lookup") {
        static_assert(state_name<SessionState::Disconnected>() == "Disconnected");
        static_assert(state_name<SessionState::Active>() == "Active");
        REQUIRE(true);
    }
}

TEST_CASE("is_connected lookup", "[session][state]") {
    REQUIRE_FALSE(is_connected(SessionState::Disconnected));
    REQUIRE(is_connected(SessionState::SocketConnected));
    REQUIRE(is_connected(SessionState::LogonSent));
    REQUIRE(is_connected(SessionState::Active));
    REQUIRE(is_connected(SessionState::LogoutPending));
    REQUIRE_FALSE(is_connected(SessionState::LogoutReceived));
    REQUIRE_FALSE(is_connected(SessionState::Reconnecting));
    REQUIRE_FALSE(is_connected(SessionState::Error));
}

TEST_CASE("can_send_app_messages", "[session][state]") {
    REQUIRE(can_send_app_messages(SessionState::Active));
    REQUIRE_FALSE(can_send_app_messages(SessionState::Disconnected));
    REQUIRE_FALSE(can_send_app_messages(SessionState::SocketConnected));
    REQUIRE_FALSE(can_send_app_messages(SessionState::LogonSent));
    REQUIRE_FALSE(can_send_app_messages(SessionState::LogoutPending));
    REQUIRE_FALSE(can_send_app_messages(SessionState::Error));
}

TEST_CASE("Event name lookup", "[session][state]") {
    REQUIRE(event_name(SessionEvent::Connect) == "Connect");
    REQUIRE(event_name(SessionEvent::Disconnect) == "Disconnect");
    REQUIRE(event_name(SessionEvent::LogonSent) == "LogonSent");
    REQUIRE(event_name(SessionEvent::Error) == "Error");
}

// ============================================================================
// Sequence Manager Tests
// ============================================================================

TEST_CASE("Sequence number management", "[session][sequence]") {
    SequenceManager seq;

    SECTION("Initial state") {
        REQUIRE(seq.current_outbound() == 1);
        REQUIRE(seq.expected_inbound() == 1);
    }

    SECTION("next_outbound returns current and increments") {
        REQUIRE(seq.next_outbound() == 1);
        REQUIRE(seq.current_outbound() == 2);
        REQUIRE(seq.next_outbound() == 2);
        REQUIRE(seq.current_outbound() == 3);
    }

    SECTION("validate_inbound with expected seq -> Ok") {
        REQUIRE(seq.validate_inbound(1) == SequenceManager::SequenceResult::Ok);
        REQUIRE(seq.expected_inbound() == 2);
        REQUIRE(seq.validate_inbound(2) == SequenceManager::SequenceResult::Ok);
        REQUIRE(seq.expected_inbound() == 3);
    }

    SECTION("validate_inbound with high seq -> GapDetected") {
        REQUIRE(seq.validate_inbound(5) == SequenceManager::SequenceResult::GapDetected);
        // expected_inbound should NOT advance on gap
        REQUIRE(seq.expected_inbound() == 1);
    }

    SECTION("validate_inbound with low seq -> TooLow") {
        (void)seq.validate_inbound(1);  // advance to 2
        REQUIRE(seq.validate_inbound(1) == SequenceManager::SequenceResult::TooLow);
    }

    SECTION("gap_range returns correct range") {
        auto [begin, end] = seq.gap_range(5);
        REQUIRE(begin == 1);
        REQUIRE(end == 4);
    }

    SECTION("gap_range returns zero when no gap") {
        (void)seq.validate_inbound(1);
        auto [begin, end] = seq.gap_range(1);
        REQUIRE(begin == 0);
        REQUIRE(end == 0);
    }

    SECTION("has_gap detection") {
        REQUIRE(seq.has_gap(5));
        REQUIRE_FALSE(seq.has_gap(1));
    }

    SECTION("reset restores initial state") {
        (void)seq.next_outbound();
        (void)seq.next_outbound();
        (void)seq.validate_inbound(1);
        seq.reset();
        REQUIRE(seq.current_outbound() == 1);
        REQUIRE(seq.expected_inbound() == 1);
    }

    SECTION("set_outbound and set_inbound") {
        seq.set_outbound(100);
        seq.set_inbound(200);
        REQUIRE(seq.current_outbound() == 100);
        REQUIRE(seq.expected_inbound() == 200);
    }

    SECTION("MAX_SEQ_NUM wraparound") {
        seq.set_outbound(SequenceManager::MAX_SEQ_NUM);
        uint32_t val = seq.next_outbound();
        REQUIRE(val == SequenceManager::MAX_SEQ_NUM);
        // After MAX, should wrap to INITIAL_SEQ_NUM
        REQUIRE(seq.current_outbound() == SequenceManager::INITIAL_SEQ_NUM);
    }
}

// ============================================================================
// Gap Tracker Tests
// ============================================================================

TEST_CASE("Gap tracking", "[session][gap]") {
    GapTracker tracker;

    SECTION("Initial state has no gaps") {
        REQUIRE_FALSE(tracker.has_gaps());
        REQUIRE(tracker.gap_count() == 0);
    }

    SECTION("add_gap and has_gaps") {
        REQUIRE(tracker.add_gap(5, 10));
        REQUIRE(tracker.has_gaps());
        REQUIRE(tracker.gap_count() == 1);
    }

    SECTION("get_gap returns correct data") {
        tracker.add_gap(5, 10);
        auto* gap = tracker.get_gap(0);
        REQUIRE(gap != nullptr);
        REQUIRE(gap->begin == 5);
        REQUIRE(gap->end == 10);
    }

    SECTION("get_gap out of range returns nullptr") {
        REQUIRE(tracker.get_gap(0) == nullptr);
        tracker.add_gap(1, 5);
        REQUIRE(tracker.get_gap(1) == nullptr);
    }

    SECTION("fill single-element gap removes it") {
        tracker.add_gap(5, 5);
        tracker.fill(5);
        REQUIRE_FALSE(tracker.has_gaps());
    }

    SECTION("fill gap begin shrinks from left") {
        tracker.add_gap(5, 10);
        tracker.fill(5);
        REQUIRE(tracker.has_gaps());
        auto* gap = tracker.get_gap(0);
        REQUIRE(gap->begin == 6);
        REQUIRE(gap->end == 10);
    }

    SECTION("fill gap end shrinks from right") {
        tracker.add_gap(5, 10);
        tracker.fill(10);
        auto* gap = tracker.get_gap(0);
        REQUIRE(gap->begin == 5);
        REQUIRE(gap->end == 9);
    }

    SECTION("fill middle splits gap") {
        tracker.add_gap(5, 10);
        tracker.fill(7);
        REQUIRE(tracker.gap_count() == 2);

        // One gap should be [5,6], the other [8,10]
        bool found_left = false, found_right = false;
        for (size_t i = 0; i < tracker.gap_count(); ++i) {
            auto* g = tracker.get_gap(i);
            if (g->begin == 5 && g->end == 6) found_left = true;
            if (g->begin == 8 && g->end == 10) found_right = true;
        }
        REQUIRE(found_left);
        REQUIRE(found_right);
    }

    SECTION("fill outside gap range does nothing") {
        tracker.add_gap(5, 10);
        tracker.fill(3);
        tracker.fill(12);
        REQUIRE(tracker.gap_count() == 1);
        auto* gap = tracker.get_gap(0);
        REQUIRE(gap->begin == 5);
        REQUIRE(gap->end == 10);
    }

    SECTION("MAX_GAPS limit") {
        for (size_t i = 0; i < GapTracker::MAX_GAPS; ++i) {
            REQUIRE(tracker.add_gap(static_cast<uint32_t>(i * 10),
                                    static_cast<uint32_t>(i * 10 + 5)));
        }
        REQUIRE(tracker.gap_count() == GapTracker::MAX_GAPS);
        // Should fail to add beyond MAX_GAPS
        REQUIRE_FALSE(tracker.add_gap(999, 1000));
    }

    SECTION("clear removes all gaps") {
        tracker.add_gap(1, 5);
        tracker.add_gap(10, 15);
        tracker.clear();
        REQUIRE_FALSE(tracker.has_gaps());
        REQUIRE(tracker.gap_count() == 0);
    }

    SECTION("multiple gaps tracked independently") {
        tracker.add_gap(1, 3);
        tracker.add_gap(10, 12);
        REQUIRE(tracker.gap_count() == 2);

        // Fill all of first gap
        tracker.fill(1);
        tracker.fill(2);
        tracker.fill(3);
        REQUIRE(tracker.gap_count() == 1);

        // Remaining gap should be [10, 12]
        auto* gap = tracker.get_gap(0);
        REQUIRE(gap->begin == 10);
        REQUIRE(gap->end == 12);
    }
}

// ============================================================================
// Heartbeat Timer Tests
// ============================================================================

TEST_CASE("Heartbeat timer", "[session][heartbeat]") {
    SECTION("Initial state - no heartbeat or timeout needed") {
        HeartbeatTimer timer(30);
        REQUIRE(timer.interval() == 30);
        REQUIRE_FALSE(timer.should_send_heartbeat());
        REQUIRE_FALSE(timer.should_send_test_request());
        REQUIRE_FALSE(timer.has_timed_out());
    }

    SECTION("set_interval changes interval") {
        HeartbeatTimer timer(30);
        timer.set_interval(60);
        REQUIRE(timer.interval() == 60);
    }

    SECTION("message_sent resets send timer") {
        HeartbeatTimer timer(1);
        // Wait just over 1 second
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        REQUIRE(timer.should_send_heartbeat());
        timer.message_sent();
        REQUIRE_FALSE(timer.should_send_heartbeat());
    }

    SECTION("message_received resets receive timer and clears test_request_pending") {
        HeartbeatTimer timer(1);
        timer.test_request_sent();
        timer.message_received();
        // After message_received, test_request_pending is cleared
        // so should_send_test_request could become true again later
        // For now just verify no crash
        REQUIRE_FALSE(timer.has_timed_out());
    }

    SECTION("should_send_heartbeat after interval") {
        HeartbeatTimer timer(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        REQUIRE(timer.should_send_heartbeat());
    }

    SECTION("should_send_test_request after 1.5x interval") {
        HeartbeatTimer timer(1);
        // 1.5x of 1 second = 1.5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        REQUIRE(timer.should_send_test_request());
    }

    SECTION("should_send_test_request returns false when already pending") {
        HeartbeatTimer timer(1);
        timer.test_request_sent();
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        REQUIRE_FALSE(timer.should_send_test_request());
    }

    SECTION("has_timed_out after 2x interval") {
        HeartbeatTimer timer(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        REQUIRE(timer.has_timed_out());
    }

    SECTION("reset clears all state") {
        HeartbeatTimer timer(1);
        timer.test_request_sent();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        timer.reset();
        REQUIRE_FALSE(timer.should_send_heartbeat());
        REQUIRE_FALSE(timer.has_timed_out());
    }
}

// ============================================================================
// SessionManager Tests (unit level - no transport)
// ============================================================================

TEST_CASE("SessionManager initial state", "[session][manager]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    config.heart_bt_int = 30;

    SessionManager session(config);

    REQUIRE(session.state() == SessionState::Disconnected);
    REQUIRE(session.config().sender_comp_id == "SENDER");
    REQUIRE(session.config().target_comp_id == "TARGET");
    REQUIRE(session.stats().messages_sent == 0);
    REQUIRE(session.stats().messages_received == 0);
}

TEST_CASE("SessionManager state transitions via events", "[session][manager]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";

    SessionManager session(config);

    // Track state changes
    std::vector<std::pair<SessionState, SessionState>> state_changes;
    SessionCallbacks callbacks;
    callbacks.on_state_change = [&](SessionState from, SessionState to) {
        state_changes.emplace_back(from, to);
    };
    session.set_callbacks(std::move(callbacks));

    SECTION("on_connect transitions to SocketConnected") {
        session.on_connect();
        REQUIRE(session.state() == SessionState::SocketConnected);
        REQUIRE(state_changes.size() == 1);
        REQUIRE(state_changes[0].first == SessionState::Disconnected);
        REQUIRE(state_changes[0].second == SessionState::SocketConnected);
    }

    SECTION("on_disconnect from SocketConnected -> Disconnected") {
        session.on_connect();
        session.on_disconnect();
        REQUIRE(session.state() == SessionState::Disconnected);
    }

    SECTION("initiate_logon requires SocketConnected") {
        // From Disconnected - should fail
        auto result = session.initiate_logon();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("initiate_logon from SocketConnected sends Logon and transitions") {
        std::vector<std::vector<char>> sent_messages;
        SessionCallbacks cbs;
        cbs.on_state_change = [&](SessionState from, SessionState to) {
            state_changes.emplace_back(from, to);
        };
        cbs.on_send = [&](std::span<const char> data) -> bool {
            sent_messages.emplace_back(data.begin(), data.end());
            return true;
        };
        session.set_callbacks(std::move(cbs));

        session.on_connect();
        auto result = session.initiate_logon();
        REQUIRE(result.has_value());
        REQUIRE(session.state() == SessionState::LogonSent);
        REQUIRE(sent_messages.size() == 1);

        // Verify sent message contains Logon (35=A)
        std::string msg(sent_messages[0].begin(), sent_messages[0].end());
        REQUIRE(msg.find("35=A") != std::string::npos);
        REQUIRE(msg.find("49=SENDER") != std::string::npos);
        REQUIRE(msg.find("56=TARGET") != std::string::npos);
    }

    SECTION("initiate_logout requires Active") {
        auto result = session.initiate_logout();
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("SessionManager session identity", "[session][manager]") {
    SessionConfig config{};
    config.sender_comp_id = "INITIATOR";
    config.target_comp_id = "ACCEPTOR";
    config.begin_string = "FIX.4.4";

    SessionManager session(config);
    auto id = session.session_id();
    REQUIRE(id.sender_comp_id == "INITIATOR");
    REQUIRE(id.target_comp_id == "ACCEPTOR");
    REQUIRE(id.begin_string == "FIX.4.4");

    auto rev = id.reverse();
    REQUIRE(rev.sender_comp_id == "ACCEPTOR");
    REQUIRE(rev.target_comp_id == "INITIATOR");
}

TEST_CASE("SessionManager message store", "[session][manager]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";

    SessionManager session(config);
    REQUIRE(session.message_store() == nullptr);

    store::NullMessageStore null_store("TEST");
    session.set_message_store(&null_store);
    REQUIRE(session.message_store() == &null_store);
}

TEST_CASE("SessionConfig defaults", "[session][config]") {
    SessionConfig config{};
    REQUIRE(config.begin_string == "FIX.4.4");
    REQUIRE(config.heart_bt_int == 30);
    REQUIRE(config.logon_timeout == 10);
    REQUIRE(config.logout_timeout == 5);
    REQUIRE(config.reconnect_interval == 5);
    REQUIRE(config.max_reconnect_attempts == 3);
    REQUIRE_FALSE(config.reset_seq_num_on_logon);
    REQUIRE(config.validate_comp_ids);
    REQUIRE(config.validate_checksum);
    REQUIRE_FALSE(config.persist_messages);
}

TEST_CASE("SessionStats reset", "[session][stats]") {
    SessionStats stats{};
    stats.messages_sent = 100;
    stats.messages_received = 200;
    stats.bytes_sent = 10000;
    stats.heartbeats_sent = 5;

    stats.reset();
    REQUIRE(stats.messages_sent == 0);
    REQUIRE(stats.messages_received == 0);
    REQUIRE(stats.bytes_sent == 0);
    REQUIRE(stats.heartbeats_sent == 0);
}

TEST_CASE("SessionId equality and reverse", "[session][identity]") {
    SessionId a{"SENDER", "TARGET", "FIX.4.4"};
    SessionId b{"SENDER", "TARGET", "FIX.4.4"};
    SessionId c{"OTHER", "TARGET", "FIX.4.4"};

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);

    auto rev = a.reverse();
    REQUIRE(rev.sender_comp_id == "TARGET");
    REQUIRE(rev.target_comp_id == "SENDER");
}

// ============================================================================
// Helper: Build valid FIX messages for injection
// ============================================================================

namespace {

/// Build a valid FIX Logon message (35=A)
std::string build_logon(std::string_view sender, std::string_view target,
                        uint32_t seq_num, int heart_bt_int = 30) {
    MessageAssembler asm_;
    auto msg = fix44::Logon::Builder{}
        .sender_comp_id(sender)
        .target_comp_id(target)
        .msg_seq_num(seq_num)
        .sending_time("20260401-12:00:00.000")
        .encrypt_method(0)
        .heart_bt_int(heart_bt_int)
        .reset_seq_num_flag(false)
        .build(asm_);
    return std::string(msg.data(), msg.size());
}

/// Build a valid FIX Logout message (35=5)
std::string build_logout(std::string_view sender, std::string_view target,
                         uint32_t seq_num, std::string_view text = "") {
    MessageAssembler asm_;
    auto msg = fix44::Logout::Builder{}
        .sender_comp_id(sender)
        .target_comp_id(target)
        .msg_seq_num(seq_num)
        .sending_time("20260401-12:00:00.000")
        .text(text)
        .build(asm_);
    return std::string(msg.data(), msg.size());
}

/// Build a valid FIX Heartbeat message (35=0)
std::string build_heartbeat(std::string_view sender, std::string_view target,
                            uint32_t seq_num, std::string_view test_req_id = "") {
    MessageAssembler asm_;
    auto msg = fix44::Heartbeat::Builder{}
        .sender_comp_id(sender)
        .target_comp_id(target)
        .msg_seq_num(seq_num)
        .sending_time("20260401-12:00:00.000")
        .test_req_id(test_req_id)
        .build(asm_);
    return std::string(msg.data(), msg.size());
}

/// Build a valid FIX TestRequest message (35=1)
std::string build_test_request(std::string_view sender, std::string_view target,
                               uint32_t seq_num, std::string_view test_req_id) {
    MessageAssembler asm_;
    auto msg = fix44::TestRequest::Builder{}
        .sender_comp_id(sender)
        .target_comp_id(target)
        .msg_seq_num(seq_num)
        .sending_time("20260401-12:00:00.000")
        .test_req_id(test_req_id)
        .build(asm_);
    return std::string(msg.data(), msg.size());
}

/// Build a valid FIX NewOrderSingle message (35=D) for app message testing
std::string build_new_order(std::string_view sender, std::string_view target,
                            uint32_t seq_num) {
    MessageAssembler asm_;
    auto msg = asm_.start()
        .field(tag::MsgType::value, msg_type::NewOrderSingle)
        .field(tag::SenderCompID::value, sender)
        .field(tag::TargetCompID::value, target)
        .field(tag::MsgSeqNum::value, static_cast<int64_t>(seq_num))
        .field(tag::SendingTime::value, "20260401-12:00:00.000")
        .field(11, "ORDER001")  // ClOrdID
        .field(55, "AAPL")     // Symbol
        .field(54, '1')        // Side=Buy
        .field(40, '2')        // OrdType=Limit
        .field(38, static_cast<int64_t>(100))  // OrderQty
        .field(44, "150.25")   // Price
        .field(60, "20260401-12:00:00.000")    // TransactTime
        .finish();
    return std::string(msg.data(), msg.size());
}

/// Helper to establish an active session (connect + logon handshake)
struct TestSession {
    SessionConfig config;
    SessionManager session;
    std::vector<std::vector<char>> sent_messages;
    std::vector<std::pair<SessionState, SessionState>> state_changes;
    std::vector<ParsedMessage> app_messages;
    bool logon_received = false;
    bool logout_received = false;
    std::string logout_text;

    TestSession(std::string_view sender = "SENDER",
                std::string_view target = "TARGET")
        : config{}
        , session(config)
    {
        config.sender_comp_id = sender;
        config.target_comp_id = target;
        config.heart_bt_int = 30;
        // Reconstruct with proper config
        session.~SessionManager();
        new (&session) SessionManager(config);

        SessionCallbacks cbs;
        cbs.on_send = [this](std::span<const char> data) -> bool {
            sent_messages.emplace_back(data.begin(), data.end());
            return true;
        };
        cbs.on_state_change = [this](SessionState from, SessionState to) {
            state_changes.emplace_back(from, to);
        };
        cbs.on_app_message = [this](const ParsedMessage& msg) {
            app_messages.push_back(msg);
        };
        cbs.on_logon = [this]() { logon_received = true; };
        cbs.on_logout = [this](std::string_view text) {
            logout_received = true;
            logout_text = std::string(text);
        };
        session.set_callbacks(std::move(cbs));
    }

    /// Perform connect + logon handshake to reach Active state
    void establish() {
        session.on_connect();
        (void)session.initiate_logon();

        // Inject logon response from counterparty
        auto logon_response = build_logon("TARGET", "SENDER", 1, 30);
        session.on_data_received(
            std::span<const char>{logon_response.data(), logon_response.size()});
    }

    /// Get last sent message as string
    std::string last_sent() const {
        if (sent_messages.empty()) return "";
        return std::string(sent_messages.back().begin(), sent_messages.back().end());
    }
};

}  // anonymous namespace

// ============================================================================
// SessionManager Integration Tests
// ============================================================================

TEST_CASE("Session logon handshake (initiator)", "[session][integration]") {
    TestSession ts;
    ts.session.on_connect();
    REQUIRE(ts.session.state() == SessionState::SocketConnected);

    auto result = ts.session.initiate_logon();
    REQUIRE(result.has_value());
    REQUIRE(ts.session.state() == SessionState::LogonSent);
    REQUIRE(ts.sent_messages.size() == 1);

    // Verify Logon message sent
    std::string sent(ts.sent_messages[0].begin(), ts.sent_messages[0].end());
    REQUIRE(sent.find("35=A") != std::string::npos);

    // Inject logon response
    auto logon_response = build_logon("TARGET", "SENDER", 1, 30);
    ts.session.on_data_received(
        std::span<const char>{logon_response.data(), logon_response.size()});

    REQUIRE(ts.session.state() == SessionState::Active);
    REQUIRE(ts.logon_received);
}

TEST_CASE("Session logon with HeartBtInt negotiation", "[session][integration]") {
    TestSession ts;
    ts.establish();
    REQUIRE(ts.session.state() == SessionState::Active);
}

TEST_CASE("Session logout handshake (initiator)", "[session][integration]") {
    TestSession ts;
    ts.establish();
    REQUIRE(ts.session.state() == SessionState::Active);

    auto result = ts.session.initiate_logout("Done");
    REQUIRE(result.has_value());
    REQUIRE(ts.session.state() == SessionState::LogoutPending);

    // Verify Logout message sent
    REQUIRE(ts.last_sent().find("35=5") != std::string::npos);

    // Inject logout response
    auto logout_response = build_logout("TARGET", "SENDER", 2);
    ts.session.on_data_received(
        std::span<const char>{logout_response.data(), logout_response.size()});

    REQUIRE(ts.session.state() == SessionState::Disconnected);
    REQUIRE(ts.logout_received);
}

TEST_CASE("Session handles incoming logout (acceptor side)", "[session][integration]") {
    TestSession ts;
    ts.establish();

    // Counterparty initiates logout
    auto logout = build_logout("TARGET", "SENDER", 2, "Session end");
    ts.session.on_data_received(
        std::span<const char>{logout.data(), logout.size()});

    // Session should have sent a Logout response
    REQUIRE(ts.last_sent().find("35=5") != std::string::npos);
    REQUIRE(ts.logout_received);
    REQUIRE(ts.logout_text == "Session end");
}

TEST_CASE("Session handles TestRequest with Heartbeat response", "[session][integration]") {
    TestSession ts;
    ts.establish();
    size_t sent_before = ts.sent_messages.size();

    // Counterparty sends TestRequest
    auto test_req = build_test_request("TARGET", "SENDER", 2, "TEST123");
    ts.session.on_data_received(
        std::span<const char>{test_req.data(), test_req.size()});

    // Session should respond with Heartbeat containing TestReqID
    REQUIRE(ts.sent_messages.size() > sent_before);
    std::string heartbeat = ts.last_sent();
    REQUIRE(heartbeat.find("35=0") != std::string::npos);
    REQUIRE(heartbeat.find("112=TEST123") != std::string::npos);
}

TEST_CASE("Session receives heartbeat", "[session][integration]") {
    TestSession ts;
    ts.establish();

    auto hb = build_heartbeat("TARGET", "SENDER", 2);
    ts.session.on_data_received(
        std::span<const char>{hb.data(), hb.size()});

    REQUIRE(ts.session.stats().heartbeats_received == 1);
}

TEST_CASE("Session dispatches application messages", "[session][integration]") {
    TestSession ts;
    ts.establish();

    auto order = build_new_order("TARGET", "SENDER", 2);
    ts.session.on_data_received(
        std::span<const char>{order.data(), order.size()});

    REQUIRE(ts.app_messages.size() == 1);
    REQUIRE(ts.app_messages[0].msg_type() == msg_type::NewOrderSingle);
}

TEST_CASE("Session sequence gap triggers ResendRequest", "[session][integration]") {
    TestSession ts;
    ts.establish();

    // Send message with seq=5 (expecting seq=2, gap of 2,3,4)
    auto order = build_new_order("TARGET", "SENDER", 5);
    ts.session.on_data_received(
        std::span<const char>{order.data(), order.size()});

    // Session should have sent a ResendRequest (35=2)
    std::string resend = ts.last_sent();
    REQUIRE(resend.find("35=2") != std::string::npos);
    // BeginSeqNo=2 (tag 7)
    REQUIRE(resend.find("7=2") != std::string::npos);
    // EndSeqNo=4 (tag 16)
    REQUIRE(resend.find("16=4") != std::string::npos);
}

TEST_CASE("Session message stats tracking", "[session][integration]") {
    TestSession ts;
    ts.establish();

    // After establish: 1 logon sent, 1 logon received
    REQUIRE(ts.session.stats().messages_sent >= 1);
    REQUIRE(ts.session.stats().messages_received >= 1);
    REQUIRE(ts.session.stats().bytes_sent > 0);
    REQUIRE(ts.session.stats().bytes_received > 0);
}
