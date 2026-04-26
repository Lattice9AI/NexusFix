#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <string>
#include <cstring>

#include "nexusfix/session/state.hpp"
#include "nexusfix/session/sequence.hpp"
#include "nexusfix/session/session_manager.hpp"
#include "nexusfix/session/session_handler.hpp"
#include "nexusfix/session/coroutine.hpp"
#include "nexusfix/store/i_message_store.hpp"

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

/// Spy message store that records store() calls for verification
class SpyMessageStore final : public store::IMessageStore {
public:
    struct StoreCall {
        uint32_t seq_num;
        std::vector<char> msg;
    };

    std::vector<StoreCall> store_calls;

    [[nodiscard]] bool store(uint32_t seq_num,
                             std::span<const char> msg) noexcept override {
        store_calls.push_back({seq_num, {msg.begin(), msg.end()}});
        return true;
    }

    [[nodiscard]] std::optional<std::vector<char>>
        retrieve(uint32_t) const noexcept override { return std::nullopt; }

    [[nodiscard]] std::vector<std::vector<char>>
        retrieve_range(uint32_t, uint32_t) const noexcept override { return {}; }

    void set_next_sender_seq_num(uint32_t) noexcept override {}
    void set_next_target_seq_num(uint32_t) noexcept override {}
    [[nodiscard]] uint32_t get_next_sender_seq_num() const noexcept override { return 1; }
    [[nodiscard]] uint32_t get_next_target_seq_num() const noexcept override { return 1; }
    void reset() noexcept override { store_calls.clear(); }
    void flush() noexcept override {}
    [[nodiscard]] std::string_view session_id() const noexcept override { return "SPY"; }
    [[nodiscard]] Stats stats() const noexcept override { return {}; }
};

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

/// Build a valid FIX ResendRequest message (35=2)
std::string build_resend_request(std::string_view sender, std::string_view target,
                                 uint32_t seq_num, uint32_t begin_seq, uint32_t end_seq) {
    MessageAssembler asm_;
    auto msg = asm_.start()
        .field(tag::MsgType::value, msg_type::ResendRequest)
        .field(tag::SenderCompID::value, sender)
        .field(tag::TargetCompID::value, target)
        .field(tag::MsgSeqNum::value, static_cast<int64_t>(seq_num))
        .field(tag::SendingTime::value, "20260401-12:00:00.000")
        .field(7, static_cast<int64_t>(begin_seq))   // BeginSeqNo
        .field(16, static_cast<int64_t>(end_seq))     // EndSeqNo
        .finish();
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

// ============================================================================
// Session Handler Tests
// ============================================================================

TEST_CASE("NullSessionHandler satisfies concept", "[session][handler][regression]") {
    static_assert(SessionHandler<NullSessionHandler>,
        "NullSessionHandler must satisfy SessionHandler concept");

    SECTION("All methods are no-op") {
        NullSessionHandler handler;
        ParsedMessage dummy;
        handler.on_app_message(dummy);
        handler.on_state_change(SessionState::Disconnected, SessionState::SocketConnected);
        handler.on_error(SessionError{});
        handler.on_logon();
        handler.on_logout("reason");
    }

    SECTION("on_send returns true") {
        NullSessionHandler handler;
        char buf[] = "test";
        REQUIRE(handler.on_send(std::span<const char>{buf, 4}) == true);
    }
}

TEST_CASE("FunctionPtrHandler callbacks", "[session][handler][regression]") {
    static_assert(SessionHandler<FunctionPtrHandler>,
        "FunctionPtrHandler must satisfy SessionHandler concept");

    SECTION("Callbacks invoked with context") {
        struct Context {
            bool logon_called{false};
            bool logout_called{false};
            std::string_view logout_reason;
        };
        Context ctx;

        FunctionPtrHandler handler;
        handler.context = &ctx;
        handler.logon_fn = [](void* c) noexcept {
            static_cast<Context*>(c)->logon_called = true;
        };
        handler.logout_fn = [](void* c, std::string_view reason) noexcept {
            auto* p = static_cast<Context*>(c);
            p->logout_called = true;
            p->logout_reason = reason;
        };

        handler.on_logon();
        REQUIRE(ctx.logon_called);

        handler.on_logout("done");
        REQUIRE(ctx.logout_called);
        REQUIRE(ctx.logout_reason == "done");
    }

    SECTION("Null pointers safe") {
        FunctionPtrHandler handler;
        // All function pointers null - should not crash
        ParsedMessage dummy;
        handler.on_app_message(dummy);
        handler.on_state_change(SessionState::Disconnected, SessionState::Active);
        handler.on_error(SessionError{});
        handler.on_logon();
        handler.on_logout("test");
    }

    SECTION("on_send with null returns false") {
        FunctionPtrHandler handler;
        char buf[] = "data";
        REQUIRE(handler.on_send(std::span<const char>{buf, 4}) == false);
    }

    SECTION("State change callback") {
        struct Ctx {
            SessionState from{SessionState::Disconnected};
            SessionState to{SessionState::Disconnected};
        };
        Ctx ctx;

        FunctionPtrHandler handler;
        handler.context = &ctx;
        handler.state_change_fn = [](void* c, SessionState f, SessionState t) noexcept {
            auto* p = static_cast<Ctx*>(c);
            p->from = f;
            p->to = t;
        };

        handler.on_state_change(SessionState::Active, SessionState::LogoutPending);
        REQUIRE(ctx.from == SessionState::Active);
        REQUIRE(ctx.to == SessionState::LogoutPending);
    }
}

// ============================================================================
// Coroutine Tests
// ============================================================================

TEST_CASE("Task<int> create and get result", "[session][coroutine][regression]") {
    auto coro = []() -> Task<int> {
        co_return 42;
    };

    auto task = coro();
    REQUIRE_FALSE(task.done());
    int result = task.get();
    REQUIRE(result == 42);
    REQUIRE(task.done());
}

TEST_CASE("Task<int> lazy evaluation", "[session][coroutine][regression]") {
    bool started = false;
    auto coro = [&]() -> Task<int> {
        started = true;
        co_return 99;
    };

    auto task = coro();
    // Lazy: should not have started yet
    REQUIRE_FALSE(started);
    REQUIRE_FALSE(task.done());

    task.resume();
    REQUIRE(started);
    REQUIRE(task.done());
}

TEST_CASE("Task<void> side effect", "[session][coroutine][regression]") {
    int counter = 0;
    auto coro = [&]() -> Task<void> {
        counter = 10;
        co_return;
    };

    auto task = coro();
    REQUIRE(counter == 0);
    task.get();
    REQUIRE(counter == 10);
}

TEST_CASE("Task<void> move semantics", "[session][coroutine][regression]") {
    auto coro = []() -> Task<void> {
        co_return;
    };

    auto task1 = coro();
    REQUIRE(static_cast<bool>(task1));

    auto task2 = std::move(task1);
    REQUIRE(static_cast<bool>(task2));
    REQUIRE_FALSE(static_cast<bool>(task1));

    task2.get();
    REQUIRE(task2.done());
}

TEST_CASE("Task exception propagation", "[session][coroutine][regression]") {
    auto coro = []() -> Task<int> {
        throw std::runtime_error("coroutine error");
        co_return 0;
    };

    auto task = coro();
    REQUIRE_THROWS_AS(task.get(), std::runtime_error);
}

TEST_CASE("Generator<int> sequence via next()", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    };

    auto gen = gen_fn();
    auto v1 = gen.next();
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == 1);

    auto v2 = gen.next();
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == 2);

    auto v3 = gen.next();
    REQUIRE(v3.has_value());
    REQUIRE(*v3 == 3);

    auto v4 = gen.next();
    REQUIRE_FALSE(v4.has_value());
}

TEST_CASE("Generator<int> full sequence via next()", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        for (int i = 0; i < 5; ++i) {
            co_yield i;
        }
    };

    auto gen = gen_fn();
    std::vector<int> values;
    while (auto v = gen.next()) {
        values.push_back(*v);
    }
    REQUIRE(values.size() == 5);
    REQUIRE(values[0] == 0);
    REQUIRE(values[4] == 4);
}

TEST_CASE("Generator range-based for loop", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        co_yield 10;
        co_yield 20;
        co_yield 30;
    };

    auto gen = gen_fn();
    std::vector<int> values;
    for (auto v : gen) {
        values.push_back(v);
    }
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == 10);
    REQUIRE(values[1] == 20);
    REQUIRE(values[2] == 30);
}

TEST_CASE("Generator empty range-based for loop", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        co_return;
    };

    auto gen = gen_fn();
    std::vector<int> values;
    for (auto v : gen) {
        values.push_back(v);
    }
    REQUIRE(values.empty());
}

TEST_CASE("Generator single element range-based for loop", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        co_yield 42;
    };

    auto gen = gen_fn();
    std::vector<int> values;
    for (auto v : gen) {
        values.push_back(v);
    }
    REQUIRE(values.size() == 1);
    REQUIRE(values[0] == 42);
}

TEST_CASE("Generator empty", "[session][coroutine][regression]") {
    auto gen_fn = []() -> Generator<int> {
        co_return;
    };

    auto gen = gen_fn();
    auto v = gen.next();
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Yield suspend and resume", "[session][coroutine][regression]") {
    int step = 0;
    auto coro = [&]() -> Task<void> {
        step = 1;
        co_await Yield{};
        step = 2;
        co_return;
    };

    auto task = coro();
    REQUIRE(step == 0);

    task.resume();
    REQUIRE(step == 1);

    task.resume();
    REQUIRE(step == 2);
    REQUIRE(task.done());
}

TEST_CASE("ReadyAwaitable immediate value", "[session][coroutine][regression]") {
    auto coro = []() -> Task<int> {
        int val = co_await ready(42);
        co_return val;
    };

    auto task = coro();
    int result = task.get();
    REQUIRE(result == 42);
}

TEST_CASE("SuspendIf predicate", "[session][coroutine][regression]") {
    SECTION("Predicate true suspends") {
        int step = 0;
        auto coro = [&]() -> Task<void> {
            step = 1;
            co_await suspend_if([&]() { return true; });
            step = 2;
            co_return;
        };

        auto task = coro();
        task.resume();
        REQUIRE(step == 1);
        REQUIRE_FALSE(task.done());

        task.resume();
        REQUIRE(step == 2);
    }

    SECTION("Predicate false continues") {
        int step = 0;
        auto coro = [&]() -> Task<void> {
            step = 1;
            co_await suspend_if([&]() { return false; });
            step = 2;
            co_return;
        };

        auto task = coro();
        task.resume();
        // Should pass through without suspending
        REQUIRE(step == 2);
    }
}

// ============================================================================
// Message Store Tests
// ============================================================================

TEST_CASE("NullMessageStore operations", "[session][store][regression]") {
    using namespace nfx::store;

    NullMessageStore store("TEST-SESSION");

    SECTION("session_id") {
        REQUIRE(store.session_id() == "TEST-SESSION");
    }

    SECTION("store returns true") {
        char data[] = "test message";
        REQUIRE(store.store(1, std::span<const char>{data, sizeof(data) - 1}));
    }

    SECTION("retrieve returns nullopt") {
        REQUIRE_FALSE(store.retrieve(1).has_value());
    }

    SECTION("retrieve_range returns empty") {
        auto range = store.retrieve_range(1, 10);
        REQUIRE(range.empty());
    }

    SECTION("Sequence number tracking") {
        REQUIRE(store.get_next_sender_seq_num() == 1);
        REQUIRE(store.get_next_target_seq_num() == 1);

        store.set_next_sender_seq_num(42);
        store.set_next_target_seq_num(99);

        REQUIRE(store.get_next_sender_seq_num() == 42);
        REQUIRE(store.get_next_target_seq_num() == 99);
    }

    SECTION("Reset restores defaults") {
        store.set_next_sender_seq_num(100);
        store.set_next_target_seq_num(200);
        store.reset();

        REQUIRE(store.get_next_sender_seq_num() == 1);
        REQUIRE(store.get_next_target_seq_num() == 1);
    }

    SECTION("Flush is no-op") {
        store.flush();  // Should not throw or crash
    }

    SECTION("Stats are zero") {
        auto s = store.stats();
        REQUIRE(s.messages_stored == 0);
        REQUIRE(s.messages_retrieved == 0);
        REQUIRE(s.bytes_stored == 0);
        REQUIRE(s.store_failures == 0);
    }
}

// ============================================================================
// Send Failure: Sequence Rollback & State Machine Guard Tests
// ============================================================================

TEST_CASE("Sequence preserved on transport failure", "[session][regression]") {
    TestSession ts;
    // Override on_send to return false (transport failure)
    SessionCallbacks cbs;
    cbs.on_send = [](std::span<const char>) -> bool { return false; };
    cbs.on_state_change = [&](SessionState from, SessionState to) {
        ts.state_changes.emplace_back(from, to);
    };
    ts.session.set_callbacks(std::move(cbs));

    ts.session.on_connect();
    uint32_t seq_before = ts.session.sequences().current_outbound();

    auto result = ts.session.initiate_logon();
    REQUIRE_FALSE(result.has_value());

    // Sequence number must not have advanced
    REQUIRE(ts.session.sequences().current_outbound() == seq_before);
    // State must not have advanced to LogonSent
    REQUIRE(ts.session.state() == SessionState::SocketConnected);
}

TEST_CASE("Sequence preserved when on_send callback is null", "[session][regression]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    SessionManager session(config);
    // No callbacks set (on_send is null)

    session.on_connect();
    uint32_t seq_before = session.sequences().current_outbound();

    auto result = session.initiate_logon();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(session.sequences().current_outbound() == seq_before);
}

TEST_CASE("Acceptor logon: send failure prevents Active state", "[session][regression]") {
    TestSession ts;
    // Override on_send to fail
    bool logon_called = false;
    SessionCallbacks cbs;
    cbs.on_send = [](std::span<const char>) -> bool { return false; };
    cbs.on_logon = [&]() { logon_called = true; };
    cbs.on_state_change = [&](SessionState from, SessionState to) {
        ts.state_changes.emplace_back(from, to);
    };
    ts.session.set_callbacks(std::move(cbs));

    ts.session.on_connect();
    REQUIRE(ts.session.state() == SessionState::SocketConnected);

    // Inject logon from counterparty (acceptor path)
    auto logon = build_logon("TARGET", "SENDER", 1, 30);
    ts.session.on_data_received(
        std::span<const char>{logon.data(), logon.size()});

    // Should have transitioned to LogonReceived but NOT to Active
    REQUIRE(ts.session.state() == SessionState::LogonReceived);
    REQUIRE_FALSE(logon_called);
}

TEST_CASE("Logout response: send failure prevents LogoutSent transition", "[session][regression]") {
    TestSession ts;
    ts.establish();
    REQUIRE(ts.session.state() == SessionState::Active);

    // Now override on_send to fail for the logout response
    SessionCallbacks cbs;
    bool logout_called = false;
    cbs.on_send = [](std::span<const char>) -> bool { return false; };
    cbs.on_logout = [&](std::string_view) { logout_called = true; };
    ts.session.set_callbacks(std::move(cbs));

    uint32_t seq_before = ts.session.sequences().current_outbound();

    // Counterparty initiates logout
    auto logout = build_logout("TARGET", "SENDER", 2);
    ts.session.on_data_received(
        std::span<const char>{logout.data(), logout.size()});

    // Should have reached LogoutReceived but NOT Disconnected (LogoutSent failed)
    REQUIRE(ts.session.state() == SessionState::LogoutReceived);
    // on_logout still fires since we did receive a logout
    REQUIRE(logout_called);
    // Sequence should be rolled back
    REQUIRE(ts.session.sequences().current_outbound() == seq_before);
}

TEST_CASE("Heartbeat send failure: stats not incremented", "[session][regression]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    config.heart_bt_int = 1;
    SessionManager session2(config);

    bool send_fail = true;
    SessionCallbacks cbs2;
    cbs2.on_send = [&](std::span<const char>) -> bool { return !send_fail; };
    session2.set_callbacks(std::move(cbs2));

    // Establish the session
    session2.on_connect();
    send_fail = false;
    (void)session2.initiate_logon();
    auto logon_resp = build_logon("TARGET", "SENDER", 1, 1);
    session2.on_data_received(
        std::span<const char>{logon_resp.data(), logon_resp.size()});
    REQUIRE(session2.state() == SessionState::Active);

    // Now make sends fail
    send_fail = true;
    uint32_t seq_active = session2.sequences().current_outbound();

    // Wait for heartbeat interval to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    session2.on_timer_tick();

    REQUIRE(session2.stats().heartbeats_sent == 0);
    REQUIRE(session2.sequences().current_outbound() == seq_active);
}

TEST_CASE("Test request send failure: pending flag not set", "[session][regression]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    config.heart_bt_int = 1;
    SessionManager session(config);

    bool send_fail = true;
    SessionCallbacks cbs;
    cbs.on_send = [&](std::span<const char>) -> bool { return !send_fail; };
    session.set_callbacks(std::move(cbs));

    // Establish the session
    session.on_connect();
    send_fail = false;
    (void)session.initiate_logon();
    auto logon_resp = build_logon("TARGET", "SENDER", 1, 1);
    session.on_data_received(
        std::span<const char>{logon_resp.data(), logon_resp.size()});
    REQUIRE(session.state() == SessionState::Active);

    // Now make sends fail
    send_fail = true;
    uint32_t seq_active = session.sequences().current_outbound();

    // Wait for test request interval (1.5x heartbeat = 1.5s)
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    session.on_timer_tick();

    REQUIRE(session.stats().test_requests_sent == 0);
    REQUIRE(session.sequences().current_outbound() == seq_active);

    // Second tick should still attempt test request (pending flag was not set)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    session.on_timer_tick();

    // Still 0 because send keeps failing, but importantly not crashing
    REQUIRE(session.stats().test_requests_sent == 0);
}

TEST_CASE("rollback_outbound restores previous sequence number", "[session][sequence][regression]") {
    SequenceManager seq;

    SECTION("Normal rollback") {
        REQUIRE(seq.next_outbound() == 1);
        REQUIRE(seq.current_outbound() == 2);
        seq.rollback_outbound();
        REQUIRE(seq.current_outbound() == 1);
    }

    SECTION("Rollback at INITIAL_SEQ_NUM wraps to MAX_SEQ_NUM") {
        REQUIRE(seq.current_outbound() == 1);
        seq.rollback_outbound();
        REQUIRE(seq.current_outbound() == SequenceManager::MAX_SEQ_NUM);
    }

    SECTION("Rollback after multiple advances") {
        (void)seq.next_outbound();  // 1 -> 2
        (void)seq.next_outbound();  // 2 -> 3
        (void)seq.next_outbound();  // 3 -> 4
        REQUIRE(seq.current_outbound() == 4);
        seq.rollback_outbound();
        REQUIRE(seq.current_outbound() == 3);
    }
}

TEST_CASE("Send failure: message not written to store", "[session][store][regression]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    SessionManager session(config);

    SpyMessageStore spy;
    session.set_message_store(&spy);

    SessionCallbacks cbs;
    cbs.on_send = [](std::span<const char>) -> bool { return false; };
    session.set_callbacks(std::move(cbs));

    session.on_connect();
    (void)session.initiate_logon();

    // on_send returned false, so nothing should have been stored
    REQUIRE(spy.store_calls.empty());
}

TEST_CASE("Successful send: message stored with correct sequence number", "[session][store][regression]") {
    SessionConfig config{};
    config.sender_comp_id = "SENDER";
    config.target_comp_id = "TARGET";
    SessionManager session(config);

    SpyMessageStore spy;
    session.set_message_store(&spy);

    SessionCallbacks cbs;
    cbs.on_send = [](std::span<const char>) -> bool { return true; };
    session.set_callbacks(std::move(cbs));

    session.on_connect();
    REQUIRE(session.sequences().current_outbound() == 1);

    (void)session.initiate_logon();

    // Logon consumed seq 1, so store should have key=1
    REQUIRE(spy.store_calls.size() == 1);
    REQUIRE(spy.store_calls[0].seq_num == 1);

    // Verify stored message contains MsgSeqNum=1
    std::string stored(spy.store_calls[0].msg.begin(), spy.store_calls[0].msg.end());
    REQUIRE(stored.find("34=1") != std::string::npos);
}

TEST_CASE("Multiple sends: store keys match actual MsgSeqNum", "[session][store][regression]") {
    TestSession ts;
    SpyMessageStore spy;
    ts.session.set_message_store(&spy);

    ts.establish();

    // establish() sends logon (seq=1), receives logon response
    // spy should have captured the logon with seq=1
    REQUIRE_FALSE(spy.store_calls.empty());
    REQUIRE(spy.store_calls[0].seq_num == 1);

    // Now send a logout (seq=2)
    (void)ts.session.initiate_logout("done");
    REQUIRE(spy.store_calls.size() == 2);
    REQUIRE(spy.store_calls[1].seq_num == 2);

    std::string logout_msg(spy.store_calls[1].msg.begin(), spy.store_calls[1].msg.end());
    REQUIRE(logout_msg.find("34=2") != std::string::npos);
}

TEST_CASE("SequenceReset gap-fill does not corrupt outbound sequence", "[session][store][regression]") {
    TestSession ts;
    SpyMessageStore spy;
    ts.session.set_message_store(&spy);

    ts.establish();
    REQUIRE(ts.session.state() == SessionState::Active);

    size_t store_calls_before = spy.store_calls.size();
    uint32_t seq_before = ts.session.sequences().current_outbound();

    // Counterparty sends ResendRequest asking for seq 1..1
    // Our store (SpyMessageStore) has no messages, so the fallback
    // SequenceReset gap-fill path runs with msg_seq_num(begin=1)
    auto resend_req = build_resend_request("TARGET", "SENDER", 2, 1, 1);
    ts.session.on_data_received(
        std::span<const char>{resend_req.data(), resend_req.size()});

    // Outbound sequence must NOT have changed (gap-fill doesn't consume it)
    REQUIRE(ts.session.sequences().current_outbound() == seq_before);

    // The SequenceReset should NOT have been written to the message store
    // (it goes through transmit(), not send_message())
    REQUIRE(spy.store_calls.size() == store_calls_before);

    // Verify a SequenceReset (35=4) was actually sent
    std::string last = ts.last_sent();
    REQUIRE(last.find("35=4") != std::string::npos);
}

TEST_CASE("SequenceReset gap-fill send failure does not rollback outbound", "[session][regression]") {
    TestSession ts;
    ts.establish();
    REQUIRE(ts.session.state() == SessionState::Active);

    // Now make sends fail
    SessionCallbacks cbs;
    cbs.on_send = [](std::span<const char>) -> bool { return false; };
    ts.session.set_callbacks(std::move(cbs));

    uint32_t seq_before = ts.session.sequences().current_outbound();

    // Counterparty sends ResendRequest
    auto resend_req = build_resend_request("TARGET", "SENDER", 2, 1, 1);
    ts.session.on_data_received(
        std::span<const char>{resend_req.data(), resend_req.size()});

    // Outbound sequence must NOT have been rolled back
    // (transmit() doesn't touch sequences)
    REQUIRE(ts.session.sequences().current_outbound() == seq_before);
}

TEST_CASE("IMessageStore polymorphic access", "[session][store][regression]") {
    using namespace nfx::store;

    SECTION("Virtual destructor via base pointer") {
        std::unique_ptr<IMessageStore> store =
            std::make_unique<NullMessageStore>("POLY");
        REQUIRE(store->session_id() == "POLY");
        REQUIRE(store->get_next_sender_seq_num() == 1);

        store->set_next_sender_seq_num(5);
        REQUIRE(store->get_next_sender_seq_num() == 5);
        // Destructor runs via unique_ptr - should not leak
    }
}
