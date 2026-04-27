#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>
#include <string>

#include "nexusfix/util/rdtsc_timestamp.hpp"
#include "nexusfix/util/fast_timestamp.hpp"

using namespace nfx::util;

// ============================================================================
// Cross-platform RDTSC Tests
// ============================================================================

TEST_CASE("RDTSC timestamp basic functionality", "[rdtsc][platform]") {
    RdtscClock::initialize();

    auto ts = rdtsc_timestamp();

    SECTION("Correct length") {
        REQUIRE(ts.size() == 21);
    }

    SECTION("Correct format separators") {
        // Format: YYYYMMDD-HH:MM:SS.mmm
        CHECK(ts[8] == '-');   // Date-time separator
        CHECK(ts[11] == ':');  // Hour-minute separator
        CHECK(ts[14] == ':');  // Minute-second separator
        CHECK(ts[17] == '.');  // Second-millisecond separator
    }

    SECTION("All digits are numeric") {
        auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

        // Year (0-3)
        for (int i = 0; i < 4; ++i) {
            CHECK(is_digit(ts[i]));
        }

        // Month (4-5)
        CHECK(is_digit(ts[4]));
        CHECK(is_digit(ts[5]));

        // Day (6-7)
        CHECK(is_digit(ts[6]));
        CHECK(is_digit(ts[7]));

        // Hour (9-10)
        CHECK(is_digit(ts[9]));
        CHECK(is_digit(ts[10]));

        // Minute (12-13)
        CHECK(is_digit(ts[12]));
        CHECK(is_digit(ts[13]));

        // Second (15-16)
        CHECK(is_digit(ts[15]));
        CHECK(is_digit(ts[16]));

        // Milliseconds (18-20)
        CHECK(is_digit(ts[18]));
        CHECK(is_digit(ts[19]));
        CHECK(is_digit(ts[20]));
    }

    SECTION("Reasonable year value") {
        int year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 + (ts[2] - '0') * 10 + (ts[3] - '0');
        CHECK(year >= 2024);
        CHECK(year <= 2100);
    }

    SECTION("Valid month range") {
        int month = (ts[4] - '0') * 10 + (ts[5] - '0');
        CHECK(month >= 1);
        CHECK(month <= 12);
    }

    SECTION("Valid day range") {
        int day = (ts[6] - '0') * 10 + (ts[7] - '0');
        CHECK(day >= 1);
        CHECK(day <= 31);
    }

    SECTION("Valid hour range") {
        int hour = (ts[9] - '0') * 10 + (ts[10] - '0');
        CHECK(hour >= 0);
        CHECK(hour < 24);
    }

    SECTION("Valid minute range") {
        int minute = (ts[12] - '0') * 10 + (ts[13] - '0');
        CHECK(minute >= 0);
        CHECK(minute < 60);
    }

    SECTION("Valid second range") {
        int second = (ts[15] - '0') * 10 + (ts[16] - '0');
        CHECK(second >= 0);
        CHECK(second < 60);
    }

    SECTION("Valid millisecond range") {
        int millis = (ts[18] - '0') * 100 + (ts[19] - '0') * 10 + (ts[20] - '0');
        CHECK(millis >= 0);
        CHECK(millis < 1000);
    }
}

TEST_CASE("RDTSC timestamp monotonicity", "[rdtsc][performance]") {
    RdtscClock::initialize();

    SECTION("Consecutive now_ns calls are monotonically non-decreasing") {
        // RDTSC clock guarantees hot-path monotonicity: consecutive calls
        // without sleep return non-decreasing values. We do NOT test
        // wall-time advancement across sleep_for() because CNTVCT_EL0
        // may not advance during vCPU descheduling on virtualized ARM64
        // (e.g., macOS GitHub Actions runners).
        constexpr int iterations = 10'000;
        uint64_t prev = RdtscClock::now_ns();
        int violations = 0;

        for (int i = 0; i < iterations; ++i) {
            uint64_t curr = RdtscClock::now_ns();
            if (curr < prev) {
                ++violations;
            }
            prev = curr;
        }

        CHECK(violations == 0);
    }
}

TEST_CASE("RDTSC clock calibration", "[rdtsc][calibration]") {
    RdtscClock::initialize();

    SECTION("Frequency estimation") {
        double freq = RdtscClock::frequency_ghz();
#if defined(__aarch64__) || defined(_M_ARM64)
        // ARM64 CNTVCT_EL0: typically 24-54 MHz = 0.024-0.054 ticks/ns
        CHECK(freq > 0.001);
        CHECK(freq < 1.0);
#else
        // x86 TSC: modern CPUs 1-5 GHz range
        CHECK(freq > 0.5);
        CHECK(freq < 10.0);
#endif
    }

    SECTION("Manual recalibration doesn't crash") {
        RdtscClock::calibrate();
        auto ts = rdtsc_timestamp();
        CHECK(ts.size() == 21);
    }

    SECTION("now_ns returns reasonable value") {
        uint64_t ns = RdtscClock::now_ns();

        // Should be Unix epoch time in nanoseconds
        // 2024-01-01 = ~1.7e18 nanoseconds since epoch
        // 2100-01-01 = ~4.1e18 nanoseconds since epoch
        CHECK(ns > 1'700'000'000'000'000'000ULL);  // After 2024
        CHECK(ns < 5'000'000'000'000'000'000ULL);  // Before 2100
    }
}

TEST_CASE("RDTSC thread-local timestamp generator", "[rdtsc][threading]") {
    // Each thread should have its own timestamp generator
    std::string ts_main = std::string(rdtsc_timestamp());

    std::string ts_thread;
    std::thread t([&ts_thread]() {
        ts_thread = rdtsc_timestamp();
    });
    t.join();

    // Both should be valid timestamps
    CHECK(ts_main.size() == 21);
    CHECK(ts_thread.size() == 21);

    // Both should have valid format
    CHECK(ts_main[8] == '-');
    CHECK(ts_thread[8] == '-');
}

// ============================================================================
// Performance Characteristics
// ============================================================================

TEST_CASE("RDTSC timestamp performance characteristics", "[rdtsc][benchmark][!benchmark]") {
    RdtscClock::initialize();

    SECTION("Fast path latency (warm)") {
        // Warm up
        for (int i = 0; i < 1000; ++i) {
            [[maybe_unused]] auto ts = rdtsc_timestamp();
        }

        // Measure
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 100'000; ++i) {
            [[maybe_unused]] auto ts = rdtsc_timestamp();
        }
        auto end = std::chrono::steady_clock::now();

        auto avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 100'000;

        // Should be < 50ns per call (target: ~10ns)
        INFO("Average latency: " << avg_ns << " ns/call");
        CHECK(avg_ns < 100);  // Generous threshold for CI
    }
}

// ============================================================================
// FastTimestamp Tests (util/fast_timestamp.hpp)
// ============================================================================

TEST_CASE("FastTimestamp format YYYYMMDD-HH:MM:SS.mmm", "[rdtsc][timestamp][regression]") {
    nfx::util::FastTimestamp ts;
    auto sv = ts.get();

    SECTION("Correct length") {
        REQUIRE(sv.size() == nfx::util::FastTimestamp::TIMESTAMP_LEN);
    }

    SECTION("Format separators") {
        CHECK(sv[8] == '-');
        CHECK(sv[11] == ':');
        CHECK(sv[14] == ':');
        CHECK(sv[17] == '.');
    }

    SECTION("All digit positions are numeric") {
        auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

        // Year
        for (int i = 0; i < 4; ++i) CHECK(is_digit(sv[static_cast<size_t>(i)]));
        // Month
        CHECK(is_digit(sv[4]));
        CHECK(is_digit(sv[5]));
        // Day
        CHECK(is_digit(sv[6]));
        CHECK(is_digit(sv[7]));
        // Hour
        CHECK(is_digit(sv[9]));
        CHECK(is_digit(sv[10]));
        // Minute
        CHECK(is_digit(sv[12]));
        CHECK(is_digit(sv[13]));
        // Second
        CHECK(is_digit(sv[15]));
        CHECK(is_digit(sv[16]));
        // Milliseconds
        CHECK(is_digit(sv[18]));
        CHECK(is_digit(sv[19]));
        CHECK(is_digit(sv[20]));
    }

    SECTION("Reasonable field ranges") {
        int year = (sv[0] - '0') * 1000 + (sv[1] - '0') * 100 + (sv[2] - '0') * 10 + (sv[3] - '0');
        int month = (sv[4] - '0') * 10 + (sv[5] - '0');
        int day = (sv[6] - '0') * 10 + (sv[7] - '0');
        int hour = (sv[9] - '0') * 10 + (sv[10] - '0');
        int minute = (sv[12] - '0') * 10 + (sv[13] - '0');
        int second = (sv[15] - '0') * 10 + (sv[16] - '0');
        int millis = (sv[18] - '0') * 100 + (sv[19] - '0') * 10 + (sv[20] - '0');

        CHECK(year >= 2024);
        CHECK(year <= 2100);
        CHECK(month >= 1);
        CHECK(month <= 12);
        CHECK(day >= 1);
        CHECK(day <= 31);
        CHECK(hour >= 0);
        CHECK(hour < 24);
        CHECK(minute >= 0);
        CHECK(minute < 60);
        CHECK(second >= 0);
        CHECK(second < 60);
        CHECK(millis >= 0);
        CHECK(millis < 1000);
    }
}

TEST_CASE("FastTimestamp fast path: same second shares date prefix", "[rdtsc][timestamp][regression]") {
    nfx::util::FastTimestamp ts;

    // Two calls within same second should share date+time prefix
    auto sv1 = ts.get();
    std::string prefix1(sv1.substr(0, 17));  // "YYYYMMDD-HH:MM:SS"

    auto sv2 = ts.get();
    std::string prefix2(sv2.substr(0, 17));

    CHECK(prefix1 == prefix2);
}

TEST_CASE("FastTimestamp slow path: second boundary update", "[rdtsc][timestamp][regression]") {
    using namespace std::chrono;
    nfx::util::FastTimestamp ts;

    // Use get(time_point) to force specific times
    auto base = system_clock::now();
    auto sec1 = floor<seconds>(base);
    auto sec2 = sec1 + seconds{1};

    auto sv1 = ts.get(sec1 + milliseconds{500});
    std::string s1(sv1);

    auto sv2 = ts.get(sec2 + milliseconds{100});
    std::string s2(sv2);

    // Second field (positions 15-16) should differ
    CHECK(s1.substr(0, 17) != s2.substr(0, 17));
}

TEST_CASE("FastTimestamp monotonicity: sequential calls non-decreasing", "[rdtsc][timestamp][regression]") {
    nfx::util::FastTimestamp ts;

    std::string prev(ts.get());
    int violations = 0;

    for (int i = 0; i < 10'000; ++i) {
        std::string curr(ts.get());
        if (curr < prev) {
            ++violations;
        }
        prev = curr;
    }

    CHECK(violations == 0);
}

TEST_CASE("FastTimestamp thread-local: independent instances", "[rdtsc][timestamp][regression]") {
    // Main thread timestamp
    auto sv_main = nfx::util::fast_timestamp();
    std::string ts_main(sv_main);

    std::string ts_thread;
    std::thread t([&ts_thread]() {
        auto sv = nfx::util::fast_timestamp();
        ts_thread = std::string(sv);
    });
    t.join();

    // Both should be valid timestamps
    CHECK(ts_main.size() == 21);
    CHECK(ts_thread.size() == 21);
    CHECK(ts_main[8] == '-');
    CHECK(ts_thread[8] == '-');
}
