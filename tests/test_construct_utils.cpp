#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "nexusfix/util/construct_utils.hpp"

using namespace nfx::util;

namespace {

struct Tracker {
    int value;
    static int construct_count;
    static int destruct_count;

    Tracker() noexcept : value(0) { ++construct_count; }
    explicit Tracker(int v) noexcept : value(v) { ++construct_count; }
    ~Tracker() { ++destruct_count; }

    Tracker(const Tracker& o) noexcept : value(o.value) { ++construct_count; }
    Tracker(Tracker&& o) noexcept : value(o.value) { o.value = -1; ++construct_count; }

    Tracker& operator=(const Tracker&) = default;
    Tracker& operator=(Tracker&&) = default;

    static void reset() { construct_count = 0; destruct_count = 0; }
};

int Tracker::construct_count = 0;
int Tracker::destruct_count = 0;

} // namespace

// ============================================================================
// construct / destroy Tests
// ============================================================================

TEST_CASE("construct and destroy", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* ptr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* result = construct(ptr, 42);
    REQUIRE(result == ptr);
    REQUIRE(result->value == 42);
    REQUIRE(Tracker::construct_count == 1);

    destroy(ptr);
    REQUIRE(Tracker::destruct_count == 1);
}

TEST_CASE("construct_default", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* ptr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* result = construct_default(ptr);
    REQUIRE(result->value == 0);
    REQUIRE(Tracker::construct_count == 1);

    destroy(ptr);
}

// ============================================================================
// reconstruct Tests
// ============================================================================

TEST_CASE("reconstruct replaces existing object", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* ptr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* initial = construct(ptr, 10);
    REQUIRE(initial->value == 10);

    auto* result = reconstruct(ptr, 20);
    REQUIRE(result->value == 20);
    REQUIRE(Tracker::construct_count == 2);
    REQUIRE(Tracker::destruct_count == 1);

    destroy(ptr);
}

TEST_CASE("reconstruct_default resets to default", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* ptr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* initial = construct(ptr, 99);
    (void)initial;
    auto* result = reconstruct_default(ptr);
    REQUIRE(result->value == 0);

    destroy(ptr);
}

// ============================================================================
// Raw Memory Tests
// ============================================================================

TEST_CASE("construct_in_raw / destroy_to_raw", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];

    Tracker::reset();

    auto* ptr = construct_in_raw<Tracker>(raw, 55);
    REQUIRE(ptr->value == 55);
    REQUIRE(Tracker::construct_count == 1);

    void* returned = destroy_to_raw(ptr);
    REQUIRE(returned == raw);
    REQUIRE(Tracker::destruct_count == 1);
}

// ============================================================================
// Array Construction Tests
// ============================================================================

TEST_CASE("construct_array and destroy_array", "[construct_utils][regression]") {
    constexpr size_t N = 4;
    alignas(Tracker) char raw[sizeof(Tracker) * N];
    auto* arr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    construct_array(arr, arr + N);
    REQUIRE(Tracker::construct_count == 4);
    for (size_t i = 0; i < N; ++i) {
        REQUIRE(arr[i].value == 0);
    }

    destroy_array(arr, arr + N);
    REQUIRE(Tracker::destruct_count == 4);
}

TEST_CASE("construct_array_n and destroy_array_n", "[construct_utils][regression]") {
    constexpr size_t N = 3;
    alignas(Tracker) char raw[sizeof(Tracker) * N];
    auto* arr = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    construct_array_n(arr, N);
    REQUIRE(Tracker::construct_count == 3);

    destroy_array_n(arr, N);
    REQUIRE(Tracker::destruct_count == 3);
}

// ============================================================================
// Move / Copy Construction Tests
// ============================================================================

TEST_CASE("construct_move", "[construct_utils][regression]") {
    Tracker source(77);
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* dest = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* result = construct_move(dest, source);
    REQUIRE(result->value == 77);
    REQUIRE(source.value == -1);

    destroy(dest);
}

TEST_CASE("construct_copy", "[construct_utils][regression]") {
    Tracker source(88);
    alignas(Tracker) char raw[sizeof(Tracker)];
    auto* dest = reinterpret_cast<Tracker*>(raw);

    Tracker::reset();

    auto* result = construct_copy(dest, source);
    REQUIRE(result->value == 88);
    REQUIRE(source.value == 88);

    destroy(dest);
}

// ============================================================================
// Type Traits Tests
// ============================================================================

TEST_CASE("is_trivially_reconstructible_v", "[construct_utils][regression]") {
    REQUIRE(is_trivially_reconstructible_v<int>);
    REQUIRE(is_trivially_reconstructible_v<double>);
    REQUIRE_FALSE(is_trivially_reconstructible_v<std::string>);
}

TEST_CASE("is_pool_safe_v", "[construct_utils][regression]") {
    REQUIRE(is_pool_safe_v<int>);
    REQUIRE(is_pool_safe_v<Tracker>);
    REQUIRE(is_pool_safe_v<std::string>);
}

// ============================================================================
// ConstructedObject RAII Tests
// ============================================================================

TEST_CASE("ConstructedObject basic lifecycle", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];

    Tracker::reset();

    {
        ConstructedObject<Tracker> obj(raw, 42);
        REQUIRE(obj);
        REQUIRE(obj->value == 42);
        REQUIRE((*obj).value == 42);
        REQUIRE(obj.get() != nullptr);
        REQUIRE(Tracker::construct_count == 1);
    }

    REQUIRE(Tracker::destruct_count == 1);
}

TEST_CASE("ConstructedObject move semantics", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];

    Tracker::reset();

    ConstructedObject<Tracker> obj1(raw, 10);
    ConstructedObject<Tracker> obj2(std::move(obj1));

    REQUIRE_FALSE(obj1);
    REQUIRE(obj2);
    REQUIRE(obj2->value == 10);
}

TEST_CASE("ConstructedObject release", "[construct_utils][regression]") {
    alignas(Tracker) char raw[sizeof(Tracker)];

    Tracker::reset();

    Tracker* released = nullptr;
    {
        ConstructedObject<Tracker> obj(raw, 33);
        released = obj.release();
        REQUIRE_FALSE(obj);
    }

    REQUIRE(released != nullptr);
    REQUIRE(released->value == 33);
    REQUIRE(Tracker::destruct_count == 0);

    destroy(released);
    REQUIRE(Tracker::destruct_count == 1);
}
