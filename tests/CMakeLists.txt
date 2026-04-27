# Main unit tests (Catch2)
add_executable(nexusfix_tests
    test_types.cpp
    test_parser.cpp
    test_memory.cpp
    test_market_data.cpp
    test_sbe.cpp
    test_timestamp_parser.cpp
    test_rdtsc_timestamp.cpp
    test_session.cpp
    test_messages.cpp
    test_admin_messages.cpp
    test_e2e.cpp
    test_mpsc_queue.cpp
    test_spsc_queue.cpp
    test_seqlock.cpp
    test_fix_version.cpp
    test_utils.cpp
    test_serializer.cpp
    test_deferred_processor.cpp
    test_property.cpp
    test_io_uring.cpp
    test_fix42.cpp
    test_fix50.cpp
    test_fixt11.cpp
    test_sbe_codecs.cpp
    test_construct_utils.cpp
    test_ranges_utils.cpp
    test_memory_message_store.cpp
    test_error_mapping.cpp
    test_transport_factory.cpp
    test_cpu_affinity.cpp
    test_memory_lock.cpp
    test_huge_page.cpp
    test_tcp_transport.cpp
    test_ring_buffer.cpp
    test_icache_warmer.cpp
    test_diagnostic.cpp
    test_prefetch.cpp
    test_logger.cpp
    test_version_compat.cpp
    test_fix44_builders.cpp
    test_performance_gate.cpp
    test_platform.cpp
)

target_link_libraries(nexusfix_tests PRIVATE
    nexusfix
    Catch2::Catch2WithMain
)

if(MSVC)
    target_compile_options(nexusfix_tests PRIVATE /W4)
else()
    target_compile_options(nexusfix_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(nexusfix_tests PRIVATE -Wno-interference-size)
    endif()
endif()

include(CTest)
include(Catch)
catch_discover_tests(nexusfix_tests SKIP_RETURN_CODE 4)

# QuickFIX compatibility tests (standalone)
# TODO: Fix API mismatches in quickfix_compat.cpp (TICKET-011)
# - IndexedParser default constructor is private
# - FieldView API changed (has_value -> is_valid, member access)
# add_executable(quickfix_compat quickfix_compat.cpp)
# target_link_libraries(quickfix_compat PRIVATE nexusfix)
# add_test(NAME quickfix_compat COMMAND quickfix_compat)

# Platform abstraction tests (standalone, cross-platform)
add_executable(platform_test platform_test.cpp)
target_link_libraries(platform_test PRIVATE nexusfix)
if(MSVC)
    target_compile_options(platform_test PRIVATE /W4)
else()
    target_compile_options(platform_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(platform_test PRIVATE ws2_32)
endif()
add_test(NAME platform_test COMMAND platform_test)

# Set output directory
set_target_properties(nexusfix_tests platform_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

# mimalloc memory resource tests (optional, requires NFX_ENABLE_MIMALLOC=ON)
if(NFX_ENABLE_MIMALLOC)
    add_executable(mimalloc_tests test_mimalloc.cpp)
    target_link_libraries(mimalloc_tests PRIVATE nexusfix Catch2::Catch2WithMain)
    if(NOT MSVC)
        # Suppress Abseil __int128 pedantic warnings when Abseil is also enabled
        target_compile_options(mimalloc_tests PRIVATE
            -Wall -Wextra -Wno-pedantic -Wno-overflow -Wno-error=pedantic -Wno-error=overflow
        )
    endif()
    set_target_properties(mimalloc_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
    )
    catch_discover_tests(mimalloc_tests SKIP_RETURN_CODE 4)
endif()
