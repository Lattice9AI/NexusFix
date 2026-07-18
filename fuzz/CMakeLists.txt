# fuzz/CMakeLists.txt
#
# libFuzzer harnesses for TICKET_497 Phase 3. Clang-only (libFuzzer ships with
# clang; GCC has no equivalent). Off by default: enable with -DNFX_BUILD_FUZZERS=ON.
#
# Each harness links the header-only nexusfix library and is compiled with
# -fsanitize=fuzzer,address,undefined so libFuzzer drives it and ASan/UBSan
# catch memory and undefined-behavior bugs on any input.
#
# The fuzz targets deliberately do NOT link Quill/Abseil: the parser and session
# hot paths are header-only and only optionally use xsimd (SIMD scanning). This
# keeps the instrumented binary small and avoids sanitizer conflicts with those
# libraries' own allocators.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "NFX_BUILD_FUZZERS requires Clang (libFuzzer). "
        "Configure with CC=clang CXX=clang++.")
endif()

set(NFX_FUZZ_SANITIZERS "fuzzer,address,undefined"
    CACHE STRING "Sanitizer set for fuzz harnesses")

# The top-level CMakeLists sets, for Clang/Linux, directory-scope
# `add_link_options(-stdlib=libc++ -lc++abi)` and `add_compile_options(-stdlib=libc++)`.
# The fuzz harnesses deliberately use libstdc++ (the packaged libFuzzer runtime is
# built against the libstdc++ ABI). Appending `-stdlib=libstdc++` per target wins for
# stdlib selection, but the inherited `-lc++abi` token stays on the link line and
# breaks the link on any host without libc++abi installed (e.g. the CI runner:
# "cannot find -lc++abi"). Clear the inherited directory-scope options here so the
# harnesses carry only their own libstdc++ flags. Must run before add_executable.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "" LINK_OPTIONS "")

set(NFX_FUZZ_HARNESSES
    fuzz_structural_index
    fuzz_runtime_parser
    fuzz_session_input
    fuzz_serializer_roundtrip
    fuzz_sbe_decode
)

foreach(harness ${NFX_FUZZ_HARNESSES})
    add_executable(${harness} ${harness}.cpp)

    target_include_directories(${harness} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${harness} PRIVATE cxx_std_23)

    # Use the default stdlib (libstdc++). The clang libFuzzer runtime shipped on
    # Ubuntu is built against libstdc++ (__cxx11 ABI); linking it against libc++
    # leaves its own std::string/iostream symbols unresolved. libstdc++'s
    # <expected> (all the parser needs) is gated on __cpp_concepts >= 202002L, so
    # this build requires clang >= 19 (clang-18 reports 201907L and has no
    # std::expected under libstdc++). The top-level -Werror plus header [[assume]]
    # expressions with side effects trip -Wassume, so relax it for the
    # instrumented fuzz build only.
    # Later -stdlib wins under clang, overriding the top-level libc++ default.
    target_compile_options(${harness} PRIVATE
        -stdlib=libstdc++
        -g -O1
        -fsanitize=${NFX_FUZZ_SANITIZERS}
        -fno-omit-frame-pointer
        -Wno-error -Wno-assume
    )
    target_link_options(${harness} PRIVATE
        -stdlib=libstdc++
        -fsanitize=${NFX_FUZZ_SANITIZERS}
    )

    # SIMD scanning path: give the fuzzer the vectorized code too when enabled.
    if(NFX_ENABLE_SIMD)
        target_compile_options(${harness} PRIVATE -mavx2)
        target_compile_definitions(${harness} PRIVATE NFX_HAS_SIMD=1)
        if(NFX_ENABLE_XSIMD)
            target_link_libraries(${harness} PRIVATE xsimd)
            target_compile_definitions(${harness} PRIVATE NFX_HAS_XSIMD=1)
        endif()
    endif()
endforeach()

# Convenience: a `fuzz_smoke` custom target that runs each harness for a short
# fixed time over its seed corpus. CI and local smoke both call this.
set(NFX_FUZZ_SMOKE_SECONDS 60 CACHE STRING "Per-harness smoke run seconds")

add_custom_target(fuzz_smoke
    COMMENT "Running ${NFX_FUZZ_SMOKE_SECONDS}s smoke over each fuzz harness")
foreach(harness ${NFX_FUZZ_HARNESSES})
    # Scratch corpus dir first (writable, in the build tree): libFuzzer writes
    # newly discovered inputs to the FIRST corpus argument. Passing the checked-in
    # seed corpus there dumps hundreds of hash-named files into the repo. Keep the
    # seed corpus as a read-only second argument.
    add_custom_command(TARGET fuzz_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "== smoke: ${harness} =="
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_CURRENT_BINARY_DIR}/smoke-corpus/${harness}
        COMMAND $<TARGET_FILE:${harness}>
            -max_total_time=${NFX_FUZZ_SMOKE_SECONDS}
            -print_final_stats=1
            ${CMAKE_CURRENT_BINARY_DIR}/smoke-corpus/${harness}
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${harness}
        VERBATIM)
    add_dependencies(fuzz_smoke ${harness})
endforeach()
