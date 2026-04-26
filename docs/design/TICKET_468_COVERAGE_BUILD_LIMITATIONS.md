# TICKET_468: Coverage Build Limitations and User-Facing Documentation

**Status**: Open
**Priority**: Medium
**Category**: Documentation / Build Configuration / CI
**Created**: 2026-04-26
**Related**: `TICKET_467_GITHUB_ACTIONS_COVERAGE_PIPELINE_FAILURES`

---

## Summary

NexusFIX exposes `NFX_ENABLE_COVERAGE`, but current public documentation does not clearly explain that coverage instrumentation is intended only for CI and local test analysis, not for customer production builds, benchmarking, or latency-sensitive deployments.

Recent CI failures showed that coverage builds have constraints that do not apply to normal product builds:

- concurrent tests may require GCC-specific coverage handling
- coverage flags must not be applied globally to third-party dependencies
- `lcov` post-processing behavior depends on tool version and matching file sets

This ticket adds explicit public documentation and build-option wording so users do not treat coverage as a normal runtime build mode.

---

## Problem

Without explicit documentation, users may reasonably assume:

- `NFX_ENABLE_COVERAGE` is a standard supported build mode
- coverage-instrumented binaries are acceptable for performance testing
- coverage flags can be enabled in customer environments without side effects

Those assumptions are incorrect.

Coverage builds:

- add runtime overhead
- change binary size and behavior characteristics
- write `.gcda/.gcno` profiling data
- may require special handling for concurrent tests
- are more sensitive to toolchain/version-specific `lcov` and `gcov` behavior

For a low-latency FIX engine, that distinction should be explicit.

---

## Why This Matters

NexusFIX positions itself as low-latency infrastructure. Build modes that materially change runtime characteristics must be documented with clear boundaries.

Users should not benchmark or deploy a coverage-instrumented build and then draw performance conclusions from it.

Maintainers also need a stable place to document known caveats so future CI/tooling changes are not rediscovered from scratch.

---

## Required Documentation Changes

### 1. Public limitations document

Add a dedicated public document, for example:

- `docs/COVERAGE_LIMITATIONS.md`

It should state:

- `NFX_ENABLE_COVERAGE` is for CI and local test analysis only
- do not use coverage builds for production, customer deployment, or benchmark runs
- GCC concurrent coverage may require `-fprofile-update=atomic`
- third-party dependencies should not be instrumented unintentionally
- `lcov` filtering behavior may vary by version

### 2. README entry point

Add a link in `README.md` so users can discover the limitation document without reading internal tickets.

### 3. CMake option wording

Change the `NFX_ENABLE_COVERAGE` option description from a generic technical description to a usage-boundary description.

Current:

```cmake
option(NFX_ENABLE_COVERAGE "Enable code coverage instrumentation (GCC/Clang)" OFF)
```

Target wording should make intent explicit, for example:

```cmake
option(NFX_ENABLE_COVERAGE
  "Enable coverage instrumentation for CI/local test analysis only; not for production or benchmarks"
  OFF)
```

### 4. Build options section

Add `NFX_ENABLE_COVERAGE` to the build options table with a warning-oriented description.

---

## Acceptance Criteria

- a public document exists describing coverage limitations
- `README.md` links to it
- `NFX_ENABLE_COVERAGE` wording clearly restricts intended usage
- users can distinguish normal builds from coverage builds without reading CI internals

---

## Notes

- This is not just an internal CI concern; it affects how external users may choose to build and evaluate the library.
- QuickFIX upstream appears to avoid a dedicated coverage pipeline in its main public workflows, which reduces the chance of users treating coverage as a standard build mode.
