#include <catch2/catch_test_macros.hpp>

#include "nexusfix/types/fix_version.hpp"

using namespace nfx;

// ============================================================================
// FIX Version String Constants
// ============================================================================

TEST_CASE("FIX version string constants", "[fix_version][regression]") {
    REQUIRE(fix_version::FIX_4_0 == "FIX.4.0");
    REQUIRE(fix_version::FIX_4_1 == "FIX.4.1");
    REQUIRE(fix_version::FIX_4_2 == "FIX.4.2");
    REQUIRE(fix_version::FIX_4_3 == "FIX.4.3");
    REQUIRE(fix_version::FIX_4_4 == "FIX.4.4");
    REQUIRE(fix_version::FIXT_1_1 == "FIXT.1.1");
    REQUIRE(fix_version::FIX_5_0 == "FIX.5.0");
    REQUIRE(fix_version::FIX_5_0_SP1 == "FIX.5.0SP1");
    REQUIRE(fix_version::FIX_5_0_SP2 == "FIX.5.0SP2");
}

// ============================================================================
// Version String Lookup (Compile-time)
// ============================================================================

TEST_CASE("Compile-time version string lookup", "[fix_version][regression]") {
    STATIC_REQUIRE(version_string<FixVersion::Unknown>() == "Unknown");
    STATIC_REQUIRE(version_string<FixVersion::FIX_4_0>() == "FIX.4.0");
    STATIC_REQUIRE(version_string<FixVersion::FIX_4_4>() == "FIX.4.4");
    STATIC_REQUIRE(version_string<FixVersion::FIXT_1_1>() == "FIXT.1.1");
    STATIC_REQUIRE(version_string<FixVersion::FIX_5_0>() == "FIX.5.0");
    STATIC_REQUIRE(version_string<FixVersion::FIX_5_0_SP1>() == "FIX.5.0SP1");
    STATIC_REQUIRE(version_string<FixVersion::FIX_5_0_SP2>() == "FIX.5.0SP2");
}

// ============================================================================
// Version String Lookup (Runtime)
// ============================================================================

TEST_CASE("Runtime version string lookup", "[fix_version][regression]") {
    REQUIRE(version_string(FixVersion::Unknown) == "Unknown");
    REQUIRE(version_string(FixVersion::FIX_4_0) == "FIX.4.0");
    REQUIRE(version_string(FixVersion::FIX_4_1) == "FIX.4.1");
    REQUIRE(version_string(FixVersion::FIX_4_2) == "FIX.4.2");
    REQUIRE(version_string(FixVersion::FIX_4_3) == "FIX.4.3");
    REQUIRE(version_string(FixVersion::FIX_4_4) == "FIX.4.4");
    REQUIRE(version_string(FixVersion::FIXT_1_1) == "FIXT.1.1");
    REQUIRE(version_string(FixVersion::FIX_5_0) == "FIX.5.0");
    REQUIRE(version_string(FixVersion::FIX_5_0_SP1) == "FIX.5.0SP1");
    REQUIRE(version_string(FixVersion::FIX_5_0_SP2) == "FIX.5.0SP2");

    SECTION("out of range returns Unknown") {
        auto invalid = static_cast<FixVersion>(255);
        REQUIRE(version_string(invalid) == "Unknown");
    }
}

// ============================================================================
// is_fixt / is_fix4 Queries
// ============================================================================

TEST_CASE("is_fixt_version compile-time", "[fix_version][regression]") {
    STATIC_REQUIRE_FALSE(is_fixt_version<FixVersion::Unknown>());
    STATIC_REQUIRE_FALSE(is_fixt_version<FixVersion::FIX_4_0>());
    STATIC_REQUIRE_FALSE(is_fixt_version<FixVersion::FIX_4_4>());
    STATIC_REQUIRE(is_fixt_version<FixVersion::FIXT_1_1>());
    STATIC_REQUIRE(is_fixt_version<FixVersion::FIX_5_0>());
    STATIC_REQUIRE(is_fixt_version<FixVersion::FIX_5_0_SP1>());
    STATIC_REQUIRE(is_fixt_version<FixVersion::FIX_5_0_SP2>());
}

TEST_CASE("is_fix4_version compile-time", "[fix_version][regression]") {
    STATIC_REQUIRE_FALSE(is_fix4_version<FixVersion::Unknown>());
    STATIC_REQUIRE(is_fix4_version<FixVersion::FIX_4_0>());
    STATIC_REQUIRE(is_fix4_version<FixVersion::FIX_4_1>());
    STATIC_REQUIRE(is_fix4_version<FixVersion::FIX_4_2>());
    STATIC_REQUIRE(is_fix4_version<FixVersion::FIX_4_3>());
    STATIC_REQUIRE(is_fix4_version<FixVersion::FIX_4_4>());
    STATIC_REQUIRE_FALSE(is_fix4_version<FixVersion::FIXT_1_1>());
    STATIC_REQUIRE_FALSE(is_fix4_version<FixVersion::FIX_5_0>());
}

TEST_CASE("is_fixt/is_fix4 runtime", "[fix_version][regression]") {
    REQUIRE_FALSE(is_fixt_version(FixVersion::FIX_4_4));
    REQUIRE(is_fixt_version(FixVersion::FIXT_1_1));
    REQUIRE(is_fix4_version(FixVersion::FIX_4_4));
    REQUIRE_FALSE(is_fix4_version(FixVersion::FIXT_1_1));

    // Out of range
    REQUIRE_FALSE(is_fixt_version(static_cast<FixVersion>(255)));
    REQUIRE_FALSE(is_fix4_version(static_cast<FixVersion>(255)));
}

// ============================================================================
// detect_version from BeginString
// ============================================================================

TEST_CASE("detect_version from BeginString", "[fix_version][regression]") {
    SECTION("FIX 4.x versions") {
        REQUIRE(detect_version("FIX.4.0") == FixVersion::FIX_4_0);
        REQUIRE(detect_version("FIX.4.1") == FixVersion::FIX_4_1);
        REQUIRE(detect_version("FIX.4.2") == FixVersion::FIX_4_2);
        REQUIRE(detect_version("FIX.4.3") == FixVersion::FIX_4_3);
        REQUIRE(detect_version("FIX.4.4") == FixVersion::FIX_4_4);
    }

    SECTION("FIXT.1.1") {
        REQUIRE(detect_version("FIXT.1.1") == FixVersion::FIXT_1_1);
    }

    SECTION("unknown versions") {
        REQUIRE(detect_version("FIX.4.5") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX.3.0") == FixVersion::Unknown);
        REQUIRE(detect_version("") == FixVersion::Unknown);
        REQUIRE(detect_version("X") == FixVersion::Unknown);
        REQUIRE(detect_version("FIX.5.0") == FixVersion::Unknown);  // 7 chars but not 4.x
    }

    SECTION("constexpr evaluation") {
        // detect_version is constexpr
        constexpr std::string_view sv = "FIX.4.4";
        constexpr auto v = detect_version(sv);
        REQUIRE(v == FixVersion::FIX_4_4);
    }
}

// ============================================================================
// ApplVerID Tests
// ============================================================================

TEST_CASE("ApplVerID constants", "[fix_version][regression]") {
    REQUIRE(appl_ver_id::FIX_2_7 == '0');
    REQUIRE(appl_ver_id::FIX_4_4 == '6');
    REQUIRE(appl_ver_id::FIX_5_0 == '7');
    REQUIRE(appl_ver_id::FIX_5_0_SP2 == '9');
}

TEST_CASE("ApplVerID to_string compile-time", "[fix_version][regression]") {
    STATIC_REQUIRE(appl_ver_id::to_string<'0'>() == "FIX.2.7");
    STATIC_REQUIRE(appl_ver_id::to_string<'6'>() == "FIX.4.4");
    STATIC_REQUIRE(appl_ver_id::to_string<'7'>() == "FIX.5.0");
    STATIC_REQUIRE(appl_ver_id::to_string<'9'>() == "FIX.5.0SP2");
}

TEST_CASE("ApplVerID to_string runtime", "[fix_version][regression]") {
    REQUIRE(appl_ver_id::to_string('0') == "FIX.2.7");
    REQUIRE(appl_ver_id::to_string('6') == "FIX.4.4");
    REQUIRE(appl_ver_id::to_string('9') == "FIX.5.0SP2");

    SECTION("out of range") {
        REQUIRE(appl_ver_id::to_string('A') == "Unknown");
        REQUIRE(appl_ver_id::to_string('/') == "Unknown");
    }
}

// ============================================================================
// FIX 5.0 Tag Constants
// ============================================================================

TEST_CASE("FIX 5.0 tag constants", "[fix_version][regression]") {
    REQUIRE(tag::ApplVerID::value == 1128);
    REQUIRE(tag::CstmApplVerID::value == 1129);
    REQUIRE(tag::DefaultApplVerID::value == 1137);
    REQUIRE(tag::ApplExtID::value == 1156);
    REQUIRE(tag::DefaultApplExtID::value == 1407);
    REQUIRE(tag::DefaultCstmApplVerID::value == 1408);
}
