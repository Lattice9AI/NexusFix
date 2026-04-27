# TICKET_480: CI Build Failures - Prefetch ICE + Dependency Isolation + Abseil Warnings

**Status**: Resolved (round 5)
**Priority**: High (blocks CI green)
**Category**: Build / CI / Portability
**Created**: 2026-04-27
**Affected Jobs**: `Coverage` (GCC-14, Debug, `--coverage`), `Linux Full Features` (GCC-14, Release, Abseil+SIMD+io_uring)
**Related**: TICKET_465 (Cross-Platform CI Build Fixes), TICKET_468 (Coverage Build Limitations)

---

## Summary

Two CI jobs fail with compilation errors and test failures across 5 rounds of investigation:

**Coverage job (rounds 1-4):**

1. `prefetch.hpp:76` - GCC rejects runtime `bool` as `__builtin_prefetch` argument (round 1, resolved)
2. `test_performance_gate.cpp:12` - unconditional `#include <mimalloc.h>` when `NFX_ENABLE_MIMALLOC=OFF` (round 1, resolved)
3. `Parser P99 latency gate` - P99=23655ns vs 5000ns threshold in Debug build (round 2, resolved)
4. `allocate and deallocate with 2MB huge pages` / `HugePageStlAllocator with std::vector` - CTest treats Catch2 SKIP exit code 4 as failure (round 2-4, resolved)

**Linux Full Features job (round 5):**

5. `test_e2e.cpp` - Abseil `__int128` and `_mm_set1_epi8(0x80)` overflow warnings promoted to errors by `-Wpedantic -Werror` (round 5, resolved)
6. `test_logger.cpp` - Missing `#include <quill/sinks/RotatingFileSink.h>` in `logger.hpp`, masked by error 5 (ninja stops on first failure) (round 5, resolved)

Compilation errors are "works on my machine" bugs: local builds with `-O2` or `NFX_ENABLE_MIMALLOC=ON` succeed, but CI's default configuration exposes the portability defects. Test failures are environment-awareness defects: tests assume bare-metal hardware or kernel privilege that CI runners lack.

---

## Root Cause Analysis

### Error 1: `__builtin_prefetch` requires Integer Constant Expressions (ICE)

**File**: `include/nexusfix/util/prefetch.hpp:76`

```cpp
template<PrefetchLocality Locality = PrefetchLocality::High>
inline void prefetch(const void* ptr, bool for_write = false) noexcept {
    __builtin_prefetch(ptr, for_write ? 1 : 0, std::to_underlying(Locality));
}
```

GCC mandates that arguments 2 and 3 of `__builtin_prefetch` are **compile-time integer constant expressions**. The ternary `for_write ? 1 : 0` is a runtime expression even if the caller passes a literal `true`/`false` -- GCC does not constant-fold function parameters.

Clang is more lenient (folds obvious cases at `-O1+`), but GCC enforces strictly regardless of optimization level. Coverage builds use `-fno-inline` which further prevents constant propagation.

**QuickFIX reference**: QuickFIX does not use hardware prefetch at all. This is a low-level optimization unique to our codebase, so there is no upstream precedent to follow. We must handle the portability ourselves.

### Error 2: mimalloc header in non-mimalloc build

**File**: `tests/test_performance_gate.cpp:12`

```cpp
#include <mimalloc.h>  // unconditional
```

This file is compiled as part of `nexusfix_tests` (the main test target, line 43 of `tests/CMakeLists.txt`). mimalloc is only fetched when `NFX_ENABLE_MIMALLOC=ON`. CI builds with default `OFF`, so the header does not exist.

The dedicated `mimalloc_tests` target is correctly guarded by `if(NFX_ENABLE_MIMALLOC)`, but `test_performance_gate.cpp` bypasses this guard by being in the main target.

### Error 3: P99 latency gate fails in Debug on shared VM

**File**: `tests/test_performance_gate.cpp:110`

```
CHECK( p99 < P99_THRESHOLD_NS )
with expansion:
  23655.0 < 5000.0
```

The test assumes P99 < 5000ns, a target for Release builds on bare metal. Coverage builds compile with `-O0 -fno-inline --coverage`, which disables all optimizations. GitHub Actions runners are shared VMs with noisy neighbors. The combination produces ~5x worse latency than the gate allows.

This is not a performance regression. The test's precondition (optimized build on reasonable hardware) is not met. Same pattern as huge page tests: environment doesn't satisfy prerequisite, test should skip.

**Root cause**: Test lacks build-type awareness. Performance gates are only meaningful on Release builds.

### Error 4 & 5: Catch2 SKIP exit code not recognized by CTest

**File**: `tests/CMakeLists.txt:63`

```cmake
catch_discover_tests(nexusfix_tests)
```

Catch2 v3 exits with code 4 when all tests in a CTest-discovered test case are SKIPped. CTest only recognizes exit code 0 as success by default.

The huge page tests are correctly written: they check `HugePageAllocator::is_available()` and SKIP when huge pages aren't available. The tests themselves are fine. The CTest integration is missing the SKIP mapping.

**Root cause**: `catch_discover_tests` missing CTest `SKIP_RETURN_CODE` property.

**Round 2 misdiagnosis**: Initial fix used `catch_discover_tests(nexusfix_tests SKIP_RETURN_CODE 4)` as a direct parameter. This is **wrong for Catch2 v3.5.2**. The `SKIP_RETURN_CODE` direct parameter was added in **Catch2 v3.7.1** ([catchorg/Catch2#2873](https://github.com/catchorg/Catch2/issues/2873)). In v3.5.2, unknown parameters are silently ignored by `cmake_parse_arguments`.

**Correct fix for v3.5.2**: Use the `PROPERTIES` multi-value keyword, which IS supported in v3.5.2:

```cmake
catch_discover_tests(nexusfix_tests PROPERTIES SKIP_RETURN_CODE 4)
```

This passes `SKIP_RETURN_CODE 4` as a CTest test property (via `set_tests_properties`), which is the standard CTest mechanism and works regardless of Catch2 version.

**CI evidence** (commit `d64e971`, `test/1.txt`): All 3 tests correctly print `SKIPPED:` in Catch2 output and exit with code 4, but CTest still reports `***Failed` because the property was never applied.

### Error 5: Abseil `__int128` + `-Wpedantic -Werror` in linux-full job

**File**: `tests/test_e2e.cpp` (via `memory_message_store.hpp` -> `absl/container/flat_hash_map.h` -> `absl/numeric/int128.h`)

```
error: ISO C++ does not support '__int128' for 'type name' [-Werror=pedantic]
error: overflow in conversion from 'int' to 'char' changes value from '128' to '-128' [-Werror=overflow]
```

Abseil is fetched via `FetchContent_Declare` without the `SYSTEM` keyword. CMake treats its include directory as a regular `-I` path, so our `-Wpedantic -Werror` flags apply to Abseil headers. Abseil uses GCC's `__int128` extension (not ISO C++) and `_mm_set1_epi8(0x80)` which triggers `-Woverflow` (128 overflows to -128 in `char`).

The `linux-full` CI job uses `-DNFX_ENABLE_ABSEIL=ON` with GCC-14 `-Wpedantic -Werror`, exposing both warnings. The basic `build` job uses `-DNFX_ENABLE_ABSEIL=OFF`, which is why this only appeared in `linux-full`.

**Root cause**: `FetchContent_Declare(abseil-cpp ...)` missing `SYSTEM` keyword. CMake 3.25+ supports `SYSTEM` in `FetchContent_Declare`, which adds `-isystem` instead of `-I` for the dependency's include directories.

### Error 6: Missing `RotatingFileSink.h` include in `logger.hpp`

**File**: `include/nexusfix/util/logger.hpp:77`

```cpp
#include <quill/sinks/FileSink.h>       // included
// <quill/sinks/RotatingFileSink.h>     // NOT included
#include <quill/sinks/ConsoleSink.h>    // included

// ... later in code:
quill::RotatingFileSinkConfig file_config;  // error: not declared
auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(...);  // error: not declared
```

`logger.hpp` uses `quill::RotatingFileSinkConfig` and `quill::RotatingFileSink` (from Quill v7.3.0) but only includes `FileSink.h`, not `RotatingFileSink.h`. The `RotatingFileSinkConfig` class is defined in `quill/sinks/RotatingFileSink.h` and is not transitively included by `FileSink.h`.

This error was masked in CI by Error 5: ninja stops on the first failed translation unit (`test_e2e.cpp`), so `test_logger.cpp` never reached compilation. Fixing Error 5 alone would expose Error 6 in the next CI run.

**Root cause**: Missing `#include <quill/sinks/RotatingFileSink.h>` in `logger.hpp`.

---

## Fix Plan

### Fix 1: Make `for_write` a template parameter + constexpr locals

```cpp
template<PrefetchLocality Locality = PrefetchLocality::High, bool ForWrite = false>
inline void prefetch(const void* ptr) noexcept {
    constexpr int rw = ForWrite ? 1 : 0;
    constexpr int locality = static_cast<int>(Locality);
    __builtin_prefetch(ptr, rw, locality);
}
```

Both arguments are now `constexpr int` locals, which GCC accepts as ICE. Note: `std::to_underlying(Locality)` alone is insufficient -- GCC does not treat `constexpr` function return values as ICE in builtin arguments. Using `static_cast<int>` on an NTTP produces a true constant expression.

API changes from:
- `prefetch<PrefetchLocality::High>(ptr, true)` to `prefetch<PrefetchLocality::High, true>(ptr)`

Call site `test_prefetch.cpp:40` updated accordingly. No other callers exist in the codebase.

### Fix 2: Guard mimalloc include and dependent code with `NFX_HAS_MIMALLOC`

```cpp
#if defined(NFX_HAS_MIMALLOC) && NFX_HAS_MIMALLOC
#include <mimalloc.h>
#endif
```

All mimalloc-dependent code in `test_performance_gate.cpp` wrapped:
- `#include <mimalloc.h>`
- `HeapBlockCount` struct and `count_blocks_visitor` function
- `TEST_CASE("Zero heap allocations on hot path", ...)` test case

### Fix 3: Skip P99 latency gate in Debug builds

The test checks `NDEBUG` (standard C++ macro, defined by CMake in Release/RelWithDebInfo, absent in Debug) and SKIPs when not Release:

```cpp
#if !defined(NDEBUG)
    SKIP("P99 latency gate only meaningful in Release builds");
#endif
```

Uses `NDEBUG` because it requires zero CMake changes and is the standard C++ mechanism for "this is a non-debug build". The test stays in the codebase and runs on `linux-full` (Release). Coverage and Debug jobs skip it gracefully.

Alternative considered: CTest label filter in CI (`ctest -LE performance`). Rejected because it moves the precondition outside the test. Anyone running `ctest` locally in Debug would hit the same false failure.

### Fix 4: Add CTest `SKIP_RETURN_CODE` property via `PROPERTIES` keyword

**Round 2 (wrong)**:
```cmake
catch_discover_tests(nexusfix_tests SKIP_RETURN_CODE 4)
```
This syntax requires Catch2 >= v3.7.1. Silently ignored in v3.5.2.

**Round 3 (correct)**:
```cmake
catch_discover_tests(nexusfix_tests PROPERTIES SKIP_RETURN_CODE 4)
```

The `PROPERTIES` keyword is a multi-value argument supported since Catch2 v3.x. It passes key-value pairs to `set_tests_properties` for every discovered test. `SKIP_RETURN_CODE` is a standard CTest test property (not Catch2-specific) that tells CTest "if the test exits with this code, mark it as SKIPPED, not FAILED".

### Fix 5: Mark Abseil FetchContent as SYSTEM

```cmake
FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240722.0
    GIT_SHALLOW TRUE
    SYSTEM                # <-- added
)
```

The `SYSTEM` keyword (CMake 3.25+) causes `FetchContent_MakeAvailable` to add the dependency's include directories with `-isystem` instead of `-I`. GCC suppresses all warnings (including `-Wpedantic` and `-Woverflow`) for system headers, so Abseil's `__int128` usage and `_mm_set1_epi8(0x80)` overflow no longer trigger `-Werror`.

This is the correct approach because:
- We cannot modify Abseil's source code (third-party dependency)
- Selectively disabling `-Wpedantic` per translation unit would suppress warnings in our own code too
- `-isystem` is the standard mechanism for "this is a third-party header, don't warn about it"

Verified in build output: `-isystem /data/ws/NexusFix/build/_deps/abseil-cpp-src` appears in the compile command.

### Fix 6: Add missing RotatingFileSink.h include

```cpp
#include <quill/sinks/FileSink.h>
#include <quill/sinks/RotatingFileSink.h>    // <-- added
#include <quill/sinks/ConsoleSink.h>
```

Single missing include. `RotatingFileSinkConfig` and `RotatingFileSink` are declared in this header.

---

## Verification

```bash
# Reproduce Coverage job locally (rounds 1-4)
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DNFX_BUILD_TESTS=ON \
  -DNFX_ENABLE_SIMD=OFF \
  -DNFX_ENABLE_MIMALLOC=OFF \
  -DNFX_ENABLE_COVERAGE=ON

cmake --build build 2>&1 | grep -E "error:"
# Should produce zero errors after fix

# Reproduce Linux Full Features job locally (round 5)
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DNFX_BUILD_TESTS=ON \
  -DNFX_ENABLE_SIMD=ON \
  -DNFX_ENABLE_LOGGING=ON \
  -DNFX_ENABLE_ABSEIL=ON

cmake --build build --target nexusfix_tests 2>&1 | grep -E "error:"
# Should produce zero errors
# Verify -isystem:
grep -o '\-isystem [^ ]*abseil[^ ]*' build/build.ninja
# Should show: -isystem .../abseil-cpp-src
```

---

## Scope

| Item | In Scope | Notes |
|------|----------|-------|
| Fix `prefetch.hpp` template signature | Yes | Make `ForWrite` NTTP (round 1) |
| Fix `test_performance_gate.cpp` include guard | Yes | `#if NFX_HAS_MIMALLOC` (round 1) |
| Audit other `__builtin_*` usage for ICE issues | Yes | Preventive scan (round 1) |
| Audit unconditional optional-dep includes | Yes | Ensure no other leaks (round 1) |
| Update test_prefetch.cpp if API changed | Yes | Adjust call sites (round 1) |
| Add CTest SKIP_RETURN_CODE property | Yes | Via `PROPERTIES` keyword (round 2-3 wrong, round 4 correct) |
| Skip P99 gate in Debug builds | Yes | Build-type aware test (round 2) |
| Mark Abseil FetchContent as SYSTEM | Yes | `-isystem` for third-party headers (round 5) |
| Add missing `RotatingFileSink.h` include | Yes | `logger.hpp` (round 5) |

---

## Audit Checklist

Compiler builtins that require ICE arguments (scan all usages):

- [x] `__builtin_prefetch(addr, rw, locality)` - args 2,3 must be ICE. All call sites use hardcoded literals except the template in `prefetch.hpp` (fixed). `branch_hints.hpp` uses hardcoded literals (clean).
- [x] `__builtin_expect_with_probability` - arg 3 must be ICE. Not used in codebase.
- [x] `__builtin_assume_aligned` - arg 2 must be ICE. Single usage in `branch_hints.hpp` via macro `NFX_ASSUME_ALIGNED(ptr, n)` where `n` is caller-supplied constant (clean).

Optional dependency headers (must be guarded):

- [x] `mimalloc.h` - `mimalloc_resource.hpp` guarded by `NFX_HAS_MIMALLOC` (pre-existing). `test_performance_gate.cpp` now guarded (fixed).
- [x] `liburing.h` - `io_uring_transport.hpp` guarded by `NFX_HAS_IO_URING` (pre-existing). `io_uring_defer_taskrun_bench.cpp` is a standalone CMake target (acceptable).
- [x] `absl/*.h` - `memory_message_store.hpp` guarded by `NFX_HAS_ABSEIL` (pre-existing). `hash_map_bench.cpp` is a standalone CMake target that force-defines `NFX_HAS_ABSEIL=1` (acceptable). Abseil `FetchContent_Declare` now uses `SYSTEM` to suppress third-party warnings (fixed round 5).
- [x] `quill/*.h` - `logger.hpp` missing `RotatingFileSink.h` include (fixed round 5). All other quill includes present.

---

## QuickFIX Comparison (Industry Context)

QuickFIX **deliberately avoids all hardware-level optimizations**:

| Optimization | QuickFIX | NexusFIX | Maintenance Cost |
|--------------|----------|----------|------------------|
| `__builtin_prefetch` | Not used | Used | ICE constraints per compiler |
| SIMD (SSE/AVX) | Not used | AVX2 + AVX-512 | Runtime dispatch + scalar fallback |
| `__builtin_expect` | Not used (only in 3rd-party pugixml) | Used | Minor |
| Cache-line alignment | Not used | Used | Padding warnings (MSVC C4324) |
| `noexcept` guarantee | Not used | Full coverage | Must audit all paths |
| `consteval` / `constexpr` | 3 trivial constants | Extensive | Compiler version constraints |

**QuickFIX strategy**: "Write once, compile everywhere" -- `std::string::find()` for SOH scanning, exceptions for errors, heap allocation per message. Supports MSVC 2003 through latest GCC. Zero platform-specific code paths beyond Winsock vs POSIX sockets.

**NexusFIX strategy**: "Layered acceleration, tiered fallback" -- SIMD hot path with scalar fallback, zero-alloc parsing, compile-time field offsets. Requires GCC 12+ / Clang 16+ (C++23). Must maintain CI coverage across all optimization tiers.

**Trade-off**: QuickFIX pays 2000-5000ns parse latency for zero portability maintenance. NexusFIX targets <200ns but must maintain:
- Scalar fallback correctness as ground truth
- CI matrix covering: {GCC, Clang, MSVC} x {O0, O2} x {SIMD OFF, AVX2, AVX-512}
- Per-builtin ICE audit (GCC strictly enforces, Clang is lenient, MSVC uses different intrinsics entirely)
- Optional dependency isolation (mimalloc, io_uring, abseil all behind CMake flags)

---

## Changed Files

| File | Change | Round |
|------|--------|-------|
| `include/nexusfix/util/prefetch.hpp` | `for_write` runtime param to `ForWrite` NTTP; `std::to_underlying` to `static_cast<int>` via `constexpr int` locals | 1 |
| `tests/test_performance_gate.cpp` | `#include <mimalloc.h>` guarded; P99 gate skips in Debug builds | 1, 2 |
| `tests/test_prefetch.cpp` | Call site updated: `prefetch<..., true>(ptr)` | 1 |
| `tests/CMakeLists.txt` | `catch_discover_tests` adds `PROPERTIES SKIP_RETURN_CODE 4` (both targets) | 4 |
| `CMakeLists.txt` | `FetchContent_Declare(abseil-cpp ... SYSTEM)` | 5 |
| `include/nexusfix/util/logger.hpp` | Add `#include <quill/sinks/RotatingFileSink.h>` | 5 |

---

## Lessons Learned

1. **Coverage builds (`-fno-inline`) expose constant-folding assumptions** that Release builds hide. Always test with `-O0 -fno-inline`.
2. **GCC is stricter than Clang on builtin ICE requirements**. Never pass runtime values to builtin arguments documented as "must be constant".
3. **`std::to_underlying()` is not ICE-safe for GCC builtins**. Even when called on an NTTP, GCC does not treat the return value as an integer constant expression. Use `static_cast<int>` on the NTTP and assign to a `constexpr int` local.
4. **Optional dependencies must never appear in unconditional compilation paths**. Use `#if defined(NFX_HAS_X)` or move to separate translation units guarded in CMake.
5. **Catch2 v3 SKIP exit code must be declared to CTest via `PROPERTIES` keyword**. In Catch2 < v3.7.1, `catch_discover_tests(target SKIP_RETURN_CODE 4)` is silently ignored because `SKIP_RETURN_CODE` is not a recognized direct parameter. The correct syntax is `catch_discover_tests(target PROPERTIES SKIP_RETURN_CODE 4)`, which uses the `PROPERTIES` multi-value argument to set the CTest test property. The direct `SKIP_RETURN_CODE` parameter was added in v3.7.1. `cmake_parse_arguments` silently discards unknown keywords, so the wrong syntax produces no error or warning.
6. **Performance gates must be build-type aware**. A P99 gate that passes in Release `-O2` will fail in Debug `-O0 -fno-inline` by 5-10x. The test should own its precondition and skip, not rely on CI label filters.
7. **QuickFIX avoids these problems entirely by not optimizing** -- we accept the maintenance burden in exchange for 10-25x lower latency, but must keep scalar fallback paths green at all times.
8. **Third-party FetchContent dependencies must use `SYSTEM`**. Without `SYSTEM`, our `-Wpedantic -Werror` flags apply to third-party headers, causing build failures from warnings we cannot fix. CMake 3.25+ `FetchContent_Declare(... SYSTEM)` adds `-isystem` to suppress warnings in dependency headers.
9. **ninja stops on first error, masking cascading failures**. Error 6 (missing RotatingFileSink.h include) was hidden behind Error 5 (Abseil warnings) because ninja stopped after the first failed TU. Always check whether fixing one build error will expose another by examining all TUs that could be affected.
10. **Include what you use**. `logger.hpp` used `quill::RotatingFileSinkConfig` but relied on a transitive include that didn't exist. Every type used must have its header explicitly included.
