#include <catch2/catch_test_macros.hpp>

#include "nexusfix/util/cpu_affinity.hpp"

#include <thread>

using namespace nfx::util;

// ============================================================================
// CpuAffinity - Core Information
// ============================================================================

TEST_CASE("core_count returns positive value", "[cpu_affinity][regression]") {
    int count = CpuAffinity::core_count();
    REQUIRE(count > 0);
    REQUIRE(count == static_cast<int>(std::thread::hardware_concurrency()));
}

TEST_CASE("current_core returns value in valid range", "[cpu_affinity][regression]") {
    int core = CpuAffinity::current_core();
#ifdef __linux__
    REQUIRE(core >= 0);
    REQUIRE(core < CpuAffinity::core_count());
#else
    REQUIRE(core == -1);
#endif
}

TEST_CASE("get_affinity returns non-empty on Linux", "[cpu_affinity][regression]") {
    auto affinity = CpuAffinity::get_affinity();
#ifdef __linux__
    REQUIRE_FALSE(affinity.empty());
    for (int core : affinity) {
        REQUIRE(core >= 0);
        REQUIRE(core < CpuAffinity::core_count());
    }
#else
    // Non-Linux returns empty
    REQUIRE(affinity.empty());
#endif
}

// ============================================================================
// CpuAffinity - Core Pinning
// ============================================================================

TEST_CASE("pin_to_core(0) succeeds on Linux", "[cpu_affinity][regression]") {
    auto original = CpuAffinity::get_affinity();

    auto result = CpuAffinity::pin_to_core(0);
#ifdef __linux__
    // Should succeed (no special privileges needed for affinity)
    REQUIRE(result.success);
    REQUIRE(result.core_id == 0);
    REQUIRE(result.error_code == 0);

    // Verify we are pinned
    REQUIRE(CpuAffinity::is_pinned());

    // Restore original affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int c : original) {
        CPU_SET(c, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)original;
    // Not supported on other platforms
#endif
}

TEST_CASE("pin_to_core with invalid core returns error", "[cpu_affinity][regression]") {
    auto result = CpuAffinity::pin_to_core(99999);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.core_id == -1);
}

TEST_CASE("is_pinned returns true after pin_to_core", "[cpu_affinity][regression]") {
#ifdef __linux__
    auto original = CpuAffinity::get_affinity();

    auto pin_result = CpuAffinity::pin_to_core(0);
    REQUIRE(pin_result.success);
    REQUIRE(CpuAffinity::is_pinned());

    // Restore
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int c : original) {
        CPU_SET(c, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

// ============================================================================
// CpuAffinity - Session Hash
// ============================================================================

TEST_CASE("session_hash is deterministic", "[cpu_affinity][regression]") {
    auto h1 = CpuAffinity::session_hash("SENDER", "TARGET");
    auto h2 = CpuAffinity::session_hash("SENDER", "TARGET");
    REQUIRE(h1 == h2);
}

TEST_CASE("session_hash differs for different inputs", "[cpu_affinity][regression]") {
    auto h1 = CpuAffinity::session_hash("SENDER_A", "TARGET_A");
    auto h2 = CpuAffinity::session_hash("SENDER_B", "TARGET_B");
    REQUIRE(h1 != h2);

    // Also test that AB+CD != ABC+D (separator prevents collision)
    auto h3 = CpuAffinity::session_hash("AB", "CD");
    auto h4 = CpuAffinity::session_hash("ABC", "D");
    REQUIRE(h3 != h4);
}

TEST_CASE("pin_by_hash maps to valid core range", "[cpu_affinity][regression]") {
    CpuAffinityConfig config;
    config.allowed_cores = {0, 1};
    config.isolate_from_system = false;

    auto hash = CpuAffinity::session_hash("SENDER", "TARGET");
    auto result = CpuAffinity::pin_by_hash(hash, config);

#ifdef __linux__
    REQUIRE(result.success);
    REQUIRE((result.core_id == 0 || result.core_id == 1));

    // Restore affinity
    auto original = CpuAffinity::get_affinity();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int num_cores = CpuAffinity::core_count();
    for (int i = 0; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)hash;
    (void)result;
#endif
}

TEST_CASE("pin_by_hash with empty config returns failure", "[cpu_affinity][regression]") {
    CpuAffinityConfig config;
    config.allowed_cores.clear();

    auto result = CpuAffinity::pin_by_hash(42, config);
    REQUIRE_FALSE(result.success);
}

// ============================================================================
// CpuAffinityConfig
// ============================================================================

TEST_CASE("default_config has valid defaults", "[cpu_affinity][regression]") {
    auto config = CpuAffinityConfig::default_config();
    REQUIRE_FALSE(config.allowed_cores.empty());
    REQUIRE(config.isolate_from_system == true);
    REQUIRE(config.numa_node == -1);

    // All cores should be valid
    int num_cores = CpuAffinity::core_count();
    for (int c : config.allowed_cores) {
        REQUIRE(c >= 0);
        REQUIRE(c < num_cores);
    }
}

// ============================================================================
// SessionCoreMapper
// ============================================================================

TEST_CASE("SessionCoreMapper round-robin assigns distinct cores", "[cpu_affinity][regression]") {
    CpuAffinityConfig config;
    config.allowed_cores = {2, 3, 4, 5};

    SessionCoreMapper mapper{config};

    REQUIRE(mapper.next_core_round_robin() == 2);
    REQUIRE(mapper.next_core_round_robin() == 3);
    REQUIRE(mapper.next_core_round_robin() == 4);
    REQUIRE(mapper.next_core_round_robin() == 5);
    // Wraps around
    REQUIRE(mapper.next_core_round_robin() == 2);
}

TEST_CASE("SessionCoreMapper core_for_session is deterministic", "[cpu_affinity][regression]") {
    CpuAffinityConfig config;
    config.allowed_cores = {0, 1, 2, 3};

    SessionCoreMapper mapper{config};

    int c1 = mapper.core_for_session("SENDER", "TARGET");
    int c2 = mapper.core_for_session("SENDER", "TARGET");
    REQUIRE(c1 == c2);
    REQUIRE(c1 >= 0);
    REQUIRE(c1 <= 3);
}

TEST_CASE("SessionCoreMapper with empty config returns -1", "[cpu_affinity][regression]") {
    CpuAffinityConfig config;
    config.allowed_cores.clear();

    SessionCoreMapper mapper{config};
    REQUIRE(mapper.next_core_round_robin() == -1);
    REQUIRE(mapper.core_for_session("A", "B") == -1);
}

// ============================================================================
// ScopedCorePinning
// ============================================================================

TEST_CASE("ScopedCorePinning restores original affinity on destruction", "[cpu_affinity][regression]") {
#ifdef __linux__
    auto before = CpuAffinity::get_affinity();

    {
        ScopedCorePinning guard{0};
        REQUIRE(guard.is_pinned());
        REQUIRE(CpuAffinity::is_pinned());
    }

    auto after = CpuAffinity::get_affinity();
    REQUIRE(before == after);
#endif
}

TEST_CASE("ScopedCorePinning move constructor transfers ownership", "[cpu_affinity][regression]") {
#ifdef __linux__
    auto before = CpuAffinity::get_affinity();

    {
        ScopedCorePinning guard1{0};
        REQUIRE(guard1.is_pinned());

        ScopedCorePinning guard2{std::move(guard1)};
        REQUIRE(guard2.is_pinned());
        REQUIRE_FALSE(guard1.is_pinned());
    }

    auto after = CpuAffinity::get_affinity();
    REQUIRE(before == after);
#endif
}
