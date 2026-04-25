#include <catch2/catch_test_macros.hpp>

#include "nexusfix/transport/io_uring_transport.hpp"

using namespace nfx;

// ============================================================================
// NFX_IO_URING_AVAILABLE: Real io_uring tests (Linux with liburing)
// ============================================================================

#if NFX_IO_URING_AVAILABLE

#include "nexusfix/transport/batch_submitter.hpp"

// ============================================================================
// IoUringContext Tests
// ============================================================================

TEST_CASE("IoUringContext lifecycle", "[io_uring][regression]") {
    SECTION("default constructed context is not initialized") {
        IoUringContext ctx;
        REQUIRE_FALSE(ctx.is_initialized());
    }

    SECTION("init succeeds") {
        IoUringContext ctx;
        auto result = ctx.init(32);
        REQUIRE(result.has_value());
        REQUIRE(ctx.is_initialized());
    }

    SECTION("init with default queue depth") {
        IoUringContext ctx;
        auto result = ctx.init();
        REQUIRE(result.has_value());
        REQUIRE(ctx.is_initialized());
    }

    SECTION("get_sqe returns valid sqe after init") {
        IoUringContext ctx;
        REQUIRE(ctx.init().has_value());

        auto* sqe = ctx.get_sqe();
        REQUIRE(sqe != nullptr);
    }

    SECTION("peek on empty completion queue") {
        IoUringContext ctx;
        REQUIRE(ctx.init(32).has_value());

        struct io_uring_cqe* cqe = nullptr;
        int ret = ctx.peek(&cqe);
        // No completions pending, peek returns non-zero
        REQUIRE(ret != 0);
    }
}

TEST_CASE("IoUringContext NOP roundtrip", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(32).has_value());

    SECTION("submit and complete a NOP") {
        auto* sqe = ctx.get_sqe();
        REQUIRE(sqe != nullptr);

        io_uring_prep_nop(sqe);
        int tag_value = 42;
        io_uring_sqe_set_data(sqe, &tag_value);

        int submitted = ctx.submit();
        REQUIRE(submitted >= 1);

        struct io_uring_cqe* cqe = nullptr;
        int ret = ctx.wait(&cqe);
        REQUIRE(ret == 0);
        REQUIRE(cqe != nullptr);
        REQUIRE(cqe->res == 0);  // NOP always succeeds

        void* data = io_uring_cqe_get_data(cqe);
        REQUIRE(data == &tag_value);

        ctx.seen(cqe);
    }

    SECTION("multiple NOPs") {
        constexpr int COUNT = 8;
        for (int i = 0; i < COUNT; ++i) {
            auto* sqe = ctx.get_sqe();
            REQUIRE(sqe != nullptr);
            io_uring_prep_nop(sqe);
        }

        int submitted = ctx.submit();
        REQUIRE(submitted >= 1);

        for (int i = 0; i < COUNT; ++i) {
            struct io_uring_cqe* cqe = nullptr;
            int ret = ctx.wait(&cqe);
            REQUIRE(ret == 0);
            REQUIRE(cqe->res == 0);
            ctx.seen(cqe);
        }
    }
}

TEST_CASE("IoUringContext optimized mode", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init().has_value());

    // is_optimized() depends on kernel version, just verify it returns a bool
    [[maybe_unused]] bool optimized = ctx.is_optimized();
    // No assertion on value - kernel dependent
}

// ============================================================================
// RegisteredBufferPool Tests
// ============================================================================

TEST_CASE("RegisteredBufferPool", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(64).has_value());

    SECTION("init and acquire/release") {
        RegisteredBufferPool pool;
        REQUIRE_FALSE(pool.is_initialized());

        bool ok = pool.init(ctx, 4096, 8);
        REQUIRE(ok);
        REQUIRE(pool.is_initialized());
        REQUIRE(pool.num_buffers() == 8);
        REQUIRE(pool.buffer_size() == 4096);
        REQUIRE(pool.available() == 8);
        REQUIRE(ctx.has_registered_buffers());
        REQUIRE(ctx.nr_registered_buffers() == 8);
    }

    SECTION("acquire returns valid indices") {
        RegisteredBufferPool pool;
        REQUIRE(pool.init(ctx, 1024, 4));

        int idx0 = pool.acquire();
        REQUIRE(idx0 >= 0);
        REQUIRE(pool.available() == 3);

        int idx1 = pool.acquire();
        REQUIRE(idx1 >= 0);
        REQUIRE(idx0 != idx1);
        REQUIRE(pool.available() == 2);

        pool.release(idx0);
        REQUIRE(pool.available() == 3);

        pool.release(idx1);
        REQUIRE(pool.available() == 4);
    }

    SECTION("exhaustion returns -1") {
        RegisteredBufferPool pool;
        REQUIRE(pool.init(ctx, 1024, 2));

        int idx0 = pool.acquire();
        int idx1 = pool.acquire();
        REQUIRE(idx0 >= 0);
        REQUIRE(idx1 >= 0);
        REQUIRE(pool.available() == 0);

        int idx2 = pool.acquire();
        REQUIRE(idx2 == -1);

        pool.release(idx0);
        pool.release(idx1);
    }

    SECTION("buffer pointer by index") {
        RegisteredBufferPool pool;
        REQUIRE(pool.init(ctx, 1024, 4));

        int idx = pool.acquire();
        REQUIRE(idx >= 0);

        char* buf = pool.buffer(idx);
        REQUIRE(buf != nullptr);

        // Write to buffer to verify it's writable
        buf[0] = 'X';
        REQUIRE(buf[0] == 'X');

        // Out of range returns nullptr
        REQUIRE(pool.buffer(-1) == nullptr);
        REQUIRE(pool.buffer(100) == nullptr);

        pool.release(idx);
    }

    SECTION("release out of range is safe") {
        RegisteredBufferPool pool;
        REQUIRE(pool.init(ctx, 1024, 4));

        pool.release(-1);
        pool.release(100);
        REQUIRE(pool.available() == 4);
    }
}

// ============================================================================
// IoUringSocket Tests
// ============================================================================

TEST_CASE("IoUringSocket lifecycle", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(32).has_value());

    SECTION("initial state is Disconnected") {
        IoUringSocket sock(ctx);
        REQUIRE_FALSE(sock.is_connected());
        REQUIRE(sock.state() == ConnectionState::Disconnected);
        REQUIRE(sock.fd() == -1);
    }

    SECTION("create socket succeeds") {
        IoUringSocket sock(ctx);
        auto result = sock.create();
        REQUIRE(result.has_value());
        REQUIRE(sock.fd() >= 0);
        REQUIRE(sock.state() == ConnectionState::Disconnected);

        // close_sync resets state
        sock.close_sync();
        REQUIRE(sock.fd() == -1);
        REQUIRE(sock.state() == ConnectionState::Disconnected);
    }

    SECTION("close_sync on unconnected socket is safe") {
        IoUringSocket sock(ctx);
        sock.close_sync();  // Should not crash
        REQUIRE(sock.fd() == -1);
    }

    SECTION("socket options on valid fd") {
        IoUringSocket sock(ctx);
        REQUIRE(sock.create().has_value());

        // These should not crash/fail on a valid socket
        sock.set_nodelay(true);
        sock.set_keepalive(true);

        sock.close_sync();
    }

    SECTION("multishot not active initially") {
        IoUringSocket sock(ctx);
        REQUIRE_FALSE(sock.is_multishot_active());
    }

    SECTION("on_connect_complete with failure") {
        IoUringSocket sock(ctx);
        REQUIRE(sock.create().has_value());

        sock.on_connect_complete(-1);
        REQUIRE(sock.state() == ConnectionState::Error);
        REQUIRE_FALSE(sock.is_connected());

        sock.close_sync();
    }

    SECTION("on_close_complete resets state") {
        IoUringSocket sock(ctx);
        REQUIRE(sock.create().has_value());

        sock.on_close_complete();
        REQUIRE(sock.fd() == -1);
        REQUIRE(sock.state() == ConnectionState::Disconnected);
    }
}

// ============================================================================
// BatchSubmitter Tests
// ============================================================================

TEST_CASE("BatchSubmitter NOP operations", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(64).has_value());

    SECTION("initial state") {
        BatchSubmitter<8> batch(ctx);
        REQUIRE(batch.is_empty());
        REQUIRE_FALSE(batch.is_full());
        REQUIRE(batch.queued() == 0);
        REQUIRE(BatchSubmitter<8>::max_size() == 8);
    }

    SECTION("queue and submit NOPs") {
        BatchSubmitter<8> batch(ctx);

        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queued() == 1);
        REQUIRE_FALSE(batch.is_empty());

        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queued() == 3);

        int submitted = batch.submit();
        REQUIRE(submitted == 3);
        REQUIRE(batch.is_empty());
        REQUIRE(batch.queued() == 0);

        // Drain completions
        for (int i = 0; i < 3; ++i) {
            struct io_uring_cqe* cqe = nullptr;
            REQUIRE(ctx.wait(&cqe) == 0);
            REQUIRE(cqe->res == 0);
            ctx.seen(cqe);
        }
    }

    SECTION("full batch rejects further queuing") {
        BatchSubmitter<4> batch(ctx);

        for (int i = 0; i < 4; ++i) {
            REQUIRE(batch.queue_nop());
        }
        REQUIRE(batch.is_full());
        REQUIRE_FALSE(batch.queue_nop());

        // Clean up: submit and drain
        [[maybe_unused]] int submitted = batch.submit();
        for (int i = 0; i < 4; ++i) {
            struct io_uring_cqe* cqe = nullptr;
            ctx.wait(&cqe);
            ctx.seen(cqe);
        }
    }

    SECTION("submit empty batch returns 0") {
        BatchSubmitter<8> batch(ctx);
        REQUIRE(batch.submit() == 0);
    }

    SECTION("clear resets without submitting") {
        BatchSubmitter<8> batch(ctx);
        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queued() == 2);

        batch.clear();
        REQUIRE(batch.is_empty());
        REQUIRE(batch.queued() == 0);
    }

    SECTION("submit_and_wait receives completions") {
        BatchSubmitter<8> batch(ctx);

        int tag1 = 1, tag2 = 2, tag3 = 3;
        REQUIRE(batch.queue_nop(&tag1));
        REQUIRE(batch.queue_nop(&tag2));
        REQUIRE(batch.queue_nop(&tag3));

        std::array<IoEvent, 8> events{};
        int completed = batch.submit_and_wait(std::span<IoEvent>(events.data(), events.size()));
        REQUIRE(completed == 3);

        for (int i = 0; i < completed; ++i) {
            REQUIRE(events[i].result == 0);  // NOP succeeds
        }
    }
}

// ============================================================================
// AutoFlushBatchSubmitter Tests
// ============================================================================

TEST_CASE("AutoFlushBatchSubmitter", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(64).has_value());

    SECTION("auto-flush when full") {
        AutoFlushBatchSubmitter<4> batch(ctx);

        // Queue 4 NOPs (fills batch)
        for (int i = 0; i < 4; ++i) {
            REQUIRE(batch.queue_nop());
        }
        // Not yet flushed
        REQUIRE(batch.total_flushes() == 0);

        // 5th queue triggers auto-flush of first 4
        REQUIRE(batch.queue_nop());
        REQUIRE(batch.total_flushes() == 1);
        REQUIRE(batch.total_submitted() == 4);

        // Manual flush for the remaining 1
        int flushed = batch.flush();
        REQUIRE(flushed > 0);
        REQUIRE(batch.total_flushes() == 2);
        REQUIRE(batch.total_submitted() == 5);

        // Drain all completions
        for (int i = 0; i < 5; ++i) {
            struct io_uring_cqe* cqe = nullptr;
            ctx.wait(&cqe);
            ctx.seen(cqe);
        }
    }

    SECTION("statistics tracking") {
        AutoFlushBatchSubmitter<4> batch(ctx);

        REQUIRE(batch.total_submitted() == 0);
        REQUIRE(batch.total_flushes() == 0);
        REQUIRE(batch.avg_batch_size() == 0.0);

        REQUIRE(batch.queue_nop());
        REQUIRE(batch.queue_nop());
        batch.flush();

        REQUIRE(batch.total_submitted() == 2);
        REQUIRE(batch.total_flushes() == 1);
        REQUIRE(batch.avg_batch_size() == 2.0);

        // Drain
        for (int i = 0; i < 2; ++i) {
            struct io_uring_cqe* cqe = nullptr;
            ctx.wait(&cqe);
            ctx.seen(cqe);
        }
    }

    SECTION("flush empty is no-op") {
        AutoFlushBatchSubmitter<4> batch(ctx);
        int ret = batch.flush();
        REQUIRE(ret == 0);
        REQUIRE(batch.total_flushes() == 0);
    }
}

// ============================================================================
// ScatterGatherSend Tests
// ============================================================================

TEST_CASE("ScatterGatherSend", "[io_uring][regression]") {
    SECTION("add buffers and total_bytes") {
        ScatterGatherSend sg;
        REQUIRE(sg.buffer_count() == 0);
        REQUIRE(sg.total_bytes() == 0);

        char buf1[] = "hello";
        char buf2[] = "world";

        REQUIRE(sg.add(std::span<const char>(buf1, 5)));
        REQUIRE(sg.buffer_count() == 1);
        REQUIRE(sg.total_bytes() == 5);

        REQUIRE(sg.add(std::span<const char>(buf2, 5)));
        REQUIRE(sg.buffer_count() == 2);
        REQUIRE(sg.total_bytes() == 10);
    }

    SECTION("MAX_IOVECS boundary") {
        ScatterGatherSend sg;
        char buf[1] = {'x'};

        for (size_t i = 0; i < ScatterGatherSend::MAX_IOVECS; ++i) {
            REQUIRE(sg.add(std::span<const char>(buf, 1)));
        }
        REQUIRE(sg.buffer_count() == ScatterGatherSend::MAX_IOVECS);

        // Exceeding limit
        REQUIRE_FALSE(sg.add(std::span<const char>(buf, 1)));
    }

    SECTION("reset clears state") {
        ScatterGatherSend sg;
        char buf[] = "data";
        REQUIRE(sg.add(std::span<const char>(buf, 4)));
        REQUIRE(sg.buffer_count() == 1);

        sg.reset();
        REQUIRE(sg.buffer_count() == 0);
        REQUIRE(sg.total_bytes() == 0);
    }
}

// ============================================================================
// LinkedOperations Tests
// ============================================================================

TEST_CASE("LinkedOperations count tracking", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(32).has_value());

    LinkedOperations linked(ctx);
    REQUIRE(linked.count() == 0);

    // We can't easily test chain_send/chain_recv without a connected socket,
    // but we can verify submit on empty chain
    SECTION("submit empty chain returns 0") {
        REQUIRE(linked.submit() == 0);
    }
}

// ============================================================================
// IoUringTransport Stub State Tests
// ============================================================================

TEST_CASE("IoUringTransport initial state", "[io_uring][regression]") {
    IoUringContext ctx;
    REQUIRE(ctx.init(32).has_value());

    SECTION("default config") {
        IoUringTransport transport(ctx);
        REQUIRE_FALSE(transport.is_connected());
    }

    SECTION("custom config") {
        IoUringTransportConfig config;
        config.use_registered_buffers = false;
        config.use_multishot_recv = false;
        config.num_registered_buffers = 32;
        config.registered_buffer_size = 4096;

        IoUringTransport transport(ctx, config);
        REQUIRE_FALSE(transport.is_connected());
        REQUIRE(transport.config().use_registered_buffers == false);
        REQUIRE(transport.config().num_registered_buffers == 32);
    }

    SECTION("send/receive on disconnected transport fails") {
        IoUringTransport transport(ctx);

        char send_buf[] = "test";
        auto send_result = transport.send(std::span<const char>(send_buf, 4));
        REQUIRE_FALSE(send_result.has_value());

        char recv_buf[64];
        auto recv_result = transport.receive(std::span<char>(recv_buf, 64));
        REQUIRE_FALSE(recv_result.has_value());
    }

    SECTION("socket options on disconnected transport") {
        IoUringTransport transport(ctx);
        // These should not crash
        REQUIRE(transport.set_nodelay(true));
        REQUIRE(transport.set_keepalive(true));
        REQUIRE(transport.set_receive_timeout(1000));
        REQUIRE(transport.set_send_timeout(1000));
    }
}

// ============================================================================
// ProvidedBufferGroup Tests (static helpers)
// ============================================================================

TEST_CASE("ProvidedBufferGroup static helpers", "[io_uring][regression]") {
    SECTION("buffer_id_from_cqe") {
#if defined(IORING_CQE_BUFFER_SHIFT)
        // Construct flags with a known buffer ID
        uint16_t expected_id = 5;
        uint32_t flags = static_cast<uint32_t>(expected_id) << IORING_CQE_BUFFER_SHIFT;
        REQUIRE(ProvidedBufferGroup::buffer_id_from_cqe(flags) == expected_id);
#else
        REQUIRE(ProvidedBufferGroup::buffer_id_from_cqe(0) == 0);
#endif
    }

    SECTION("has_more checks IORING_CQE_F_MORE") {
#if defined(IORING_CQE_F_MORE)
        REQUIRE(ProvidedBufferGroup::has_more(IORING_CQE_F_MORE));
        REQUIRE_FALSE(ProvidedBufferGroup::has_more(0));
#else
        REQUIRE_FALSE(ProvidedBufferGroup::has_more(0));
#endif
    }

    SECTION("has_buffer checks IORING_CQE_F_BUFFER") {
#if defined(IORING_CQE_F_BUFFER)
        REQUIRE(ProvidedBufferGroup::has_buffer(IORING_CQE_F_BUFFER));
        REQUIRE_FALSE(ProvidedBufferGroup::has_buffer(0));
#else
        REQUIRE_FALSE(ProvidedBufferGroup::has_buffer(0));
#endif
    }
}

TEST_CASE("ProvidedBufferGroup default state", "[io_uring][regression]") {
    ProvidedBufferGroup group;
    REQUIRE_FALSE(group.is_initialized());
    REQUIRE(group.buffer(0) == nullptr);
}

#else  // !NFX_IO_URING_AVAILABLE

// ============================================================================
// Stub Behavior Tests (non-Linux or io_uring disabled)
// ============================================================================

TEST_CASE("IoUringContext stub", "[io_uring][regression]") {
    SECTION("init returns error") {
        IoUringContext ctx;
        auto result = ctx.init();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("is_initialized returns false") {
        IoUringContext ctx;
        REQUIRE_FALSE(ctx.is_initialized());
    }
}

TEST_CASE("IoUringTransport stub", "[io_uring][regression]") {
    IoUringContext ctx;

    SECTION("connect returns error") {
        IoUringTransport transport(ctx);
        auto result = transport.connect("localhost", 9876);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("is_connected returns false") {
        IoUringTransport transport(ctx);
        REQUIRE_FALSE(transport.is_connected());
    }

    SECTION("send returns error") {
        IoUringTransport transport(ctx);
        char buf[] = "test";
        auto result = transport.send(std::span<const char>(buf, 4));
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("receive returns error") {
        IoUringTransport transport(ctx);
        char buf[64];
        auto result = transport.receive(std::span<char>(buf, 64));
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("socket options succeed (no-op)") {
        IoUringTransport transport(ctx);
        REQUIRE(transport.set_nodelay(true));
        REQUIRE(transport.set_keepalive(true));
        REQUIRE(transport.set_receive_timeout(1000));
        REQUIRE(transport.set_send_timeout(1000));
    }

    SECTION("disconnect is safe") {
        IoUringTransport transport(ctx);
        transport.disconnect();  // Should not crash
    }
}

#endif  // NFX_IO_URING_AVAILABLE
