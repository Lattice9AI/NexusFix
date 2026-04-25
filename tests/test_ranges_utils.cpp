#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "nexusfix/util/ranges_utils.hpp"

using namespace nfx::util;

// ============================================================================
// trim Tests
// ============================================================================

TEST_CASE("trim whitespace", "[utils][ranges][regression]") {
    REQUIRE(trim("  hello  ") == "hello");
    REQUIRE(trim("\t\nhello\r\n") == "hello");
    REQUIRE(trim("hello") == "hello");
    REQUIRE(trim("   ") == "");
    REQUIRE(trim("") == "");
    REQUIRE(trim("  a  b  ") == "a  b");
}

// ============================================================================
// split_string Tests
// ============================================================================

TEST_CASE("split_string by delimiter", "[utils][ranges][regression]") {
    SECTION("comma-separated") {
        auto parts = split_string("a,b,c", ',');
        std::vector<std::string_view> result;
        for (auto part : parts) {
            result.push_back(part);
        }
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == "a");
        REQUIRE(result[1] == "b");
        REQUIRE(result[2] == "c");
    }

    SECTION("single element") {
        auto parts = split_string("hello", ',');
        std::vector<std::string_view> result;
        for (auto part : parts) {
            result.push_back(part);
        }
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == "hello");
    }

    SECTION("empty string") {
        auto parts = split_string("", ',');
        std::vector<std::string_view> result;
        for (auto part : parts) {
            result.push_back(part);
        }
        REQUIRE(result.empty());
    }
}

// ============================================================================
// indices Tests
// ============================================================================

TEST_CASE("indices generates sequence", "[utils][ranges][regression]") {
    std::vector<size_t> result;
    for (auto i : indices(5)) {
        result.push_back(i);
    }
    REQUIRE(result.size() == 5);
    REQUIRE(result[0] == 0);
    REQUIRE(result[4] == 4);
}

TEST_CASE("indices with zero", "[utils][ranges][regression]") {
    std::vector<size_t> result;
    for (auto i : indices(0)) {
        result.push_back(i);
    }
    REQUIRE(result.empty());
}

// ============================================================================
// Range Algorithm Tests
// ============================================================================

TEST_CASE("contains", "[utils][ranges][regression]") {
    std::vector<int> v = {1, 2, 3, 4, 5};
    REQUIRE(contains(v, 3));
    REQUIRE_FALSE(contains(v, 6));
}

TEST_CASE("any_of / all_of / none_of", "[utils][ranges][regression]") {
    std::vector<int> v = {2, 4, 6, 8};
    auto is_even = [](int x) { return x % 2 == 0; };
    auto is_odd = [](int x) { return x % 2 != 0; };
    auto is_positive = [](int x) { return x > 0; };

    REQUIRE(all_of(v, is_even));
    REQUIRE_FALSE(any_of(v, is_odd));
    REQUIRE(none_of(v, is_odd));
    REQUIRE(all_of(v, is_positive));
}

TEST_CASE("count_if", "[utils][ranges][regression]") {
    std::vector<int> v = {1, 2, 3, 4, 5, 6};
    auto is_even = [](int x) { return x % 2 == 0; };
    REQUIRE(count_if(v, is_even) == 3);
}

// ============================================================================
// take_n / skip_n Tests
// ============================================================================

TEST_CASE("take_n", "[utils][ranges][regression]") {
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::vector<int> result;
    for (auto x : take_n(v, 3)) {
        result.push_back(x);
    }
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 1);
    REQUIRE(result[2] == 3);
}

TEST_CASE("skip_n", "[utils][ranges][regression]") {
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::vector<int> result;
    for (auto x : skip_n(v, 2)) {
        result.push_back(x);
    }
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 3);
    REQUIRE(result[2] == 5);
}

// ============================================================================
// FixFieldView Tests
// ============================================================================

TEST_CASE("FixFieldView iterates FIX fields", "[utils][ranges][fix_field][regression]") {
    std::string data = "35=D\x01""55=AAPL\x01""38=100\x01";
    auto view = fix_fields(data);

    std::vector<std::pair<int, std::string_view>> fields;
    for (auto [tag, value] : view) {
        if (tag == 0) break;
        fields.emplace_back(tag, value);
    }

    REQUIRE(fields.size() == 3);
    REQUIRE(fields[0].first == 35);
    REQUIRE(fields[0].second == "D");
    REQUIRE(fields[1].first == 55);
    REQUIRE(fields[1].second == "AAPL");
    REQUIRE(fields[2].first == 38);
    REQUIRE(fields[2].second == "100");
}

TEST_CASE("FixFieldView empty input", "[utils][ranges][fix_field][regression]") {
    auto view = fix_fields("");
    int count = 0;
    for (auto [tag, value] : view) {
        if (tag == 0) break;
        ++count;
    }
    REQUIRE(count == 0);
}

// ============================================================================
// Span Utilities Tests
// ============================================================================

TEST_CASE("as_span / as_const_span", "[utils][ranges][regression]") {
    std::vector<int> v = {1, 2, 3};
    auto s = as_span(v);
    REQUIRE(s.size() == 3);
    REQUIRE(s[0] == 1);

    const auto& cv = v;
    auto cs = as_const_span(cv);
    REQUIRE(cs.size() == 3);
}
