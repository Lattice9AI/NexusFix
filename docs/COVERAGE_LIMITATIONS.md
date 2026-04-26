# Coverage Limitations

`NFX_ENABLE_COVERAGE` is intended for CI and local test analysis only.

It is not a normal deployment mode for NexusFIX.

## Do Not Use Coverage Builds For

- production or customer deployments
- latency benchmarking
- throughput benchmarking
- comparing performance against non-instrumented builds

Coverage instrumentation changes runtime behavior and binary characteristics.

## Why

Coverage-enabled builds typically:

- add runtime overhead
- increase binary size
- write profiling artifacts such as `.gcda` and `.gcno`
- behave differently under concurrency-sensitive test workloads
- depend on toolchain-specific `gcov` and `lcov` behavior

For a low-latency FIX engine, these differences are material.

## Known Caveats

### GCC concurrent coverage

Concurrent tests may require atomic profile updates.

For GCC coverage builds, NexusFIX uses:

```text
-fprofile-update=atomic
```

This is a coverage-only mitigation for profile counter correctness. It is not a production optimization flag.

### Third-party dependencies

Coverage instrumentation should only apply to NexusFIX targets.

If coverage flags are applied globally, third-party dependencies fetched during the build may also be instrumented, which can break coverage collection or introduce noisy results.

### lcov version sensitivity

`lcov` post-processing behavior can vary by version. For example, some versions treat unmatched exclude patterns as fatal `unused` errors.

Coverage workflows should be written defensively and validated against the runner's installed `lcov` version.

## Recommended Usage

Use coverage builds only when you want to:

- inspect test coverage locally
- troubleshoot CI coverage behavior
- generate coverage reports for maintainers

Use a normal non-instrumented build for:

- customer evaluation
- production validation
- benchmark publication
- latency and throughput measurement

## Example

Coverage build:

```bash
cmake -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNFX_ENABLE_COVERAGE=ON \
  -DNFX_BUILD_TESTS=ON \
  -DNFX_BUILD_BENCHMARKS=OFF \
  -DNFX_BUILD_EXAMPLES=OFF
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure
```

Normal performance-oriented build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DNFX_ENABLE_COVERAGE=OFF \
  -DNFX_BUILD_BENCHMARKS=ON
cmake --build build -j
```
