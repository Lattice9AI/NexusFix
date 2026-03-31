# NexusFix Benchmark Reproduction Guide

This guide explains how to reproduce the performance benchmarks claimed in the README.

## Quick Start

```bash
# Clone the repository
git clone https://github.com/Lattice9AI/NexusFix.git
cd NexusFix

# Build with optimizations
./start.sh build

# Run benchmarks (100,000 iterations)
./start.sh bench 100000
```

## Expected Results

Based on Linux GCC 13.3, 100,000 iterations:

| Metric | Expected Result |
|--------|----------------|
| ExecutionReport Parse | ~246 ns |
| NewOrderSingle Parse | ~229 ns |
| P99 Parse Latency | ~258 ns |
| Parse Throughput | ~4.17M msg/sec |

## Benchmark Components

### 1. Parse Benchmark

**Location**: `benchmarks/parse_benchmark.cpp`

**What it measures**: Message parsing latency for common FIX message types.

**Run command**:
```bash
./build/bin/benchmarks/parse_benchmark 100000
```

**Output format**:
```
[BENCHMARK] ExecutionReport Parse
  Iterations: 100000
  Mean: 246 ns
  P50:  245 ns
  P99:  258 ns
  Throughput: 4.17M msg/sec
```

### 2. Session Benchmark

**Location**: `benchmarks/session_benchmark.cpp`

**What it measures**: Session state machine performance (Logon, Heartbeat, Logout).

**Run command**:
```bash
./build/bin/benchmarks/session_benchmark 100000
```

### 3. QuickFIX Comparison (Optional)

**Prerequisites**:
```bash
# Ubuntu/Debian
sudo apt-get install libquickfix-dev

# Or build from source
git clone https://github.com/quickfix/quickfix.git
cd quickfix
./bootstrap
./configure
make
sudo make install
```

**Run command**:
```bash
./start.sh compare 100000
```

**Expected output**:
```
=== NexusFIX vs QuickFIX Benchmark ===

ExecutionReport Parse:
  QuickFIX:  730 ns
  NexusFIX:  246 ns
  Speedup:   3.0x

NewOrderSingle Parse:
  QuickFIX:  661 ns
  NexusFIX:  229 ns
  Speedup:   2.9x
```

## System Requirements

### Minimum Requirements

- **CPU**: x86_64 with AVX2 support
- **OS**: Linux (kernel 5.1+), macOS, or Windows
- **Compiler**: GCC 13+ or Clang 17+
- **CMake**: 3.20+

### Recommended for Best Performance

- **CPU**: x86_64 with AVX-512 support
- **OS**: Linux (kernel 5.19+ for io_uring DEFER_TASKRUN)
- **Compiler**: GCC 13.3 or Clang 18+
- **RAM**: 4GB+

## Factors Affecting Benchmark Results

### Hardware

| Factor | Impact | Notes |
|--------|--------|-------|
| CPU Generation | High | Newer CPUs have better SIMD and branch prediction |
| CPU Frequency | Medium | Higher frequency → lower latency |
| Cache Size | Medium | Larger L3 cache helps with session maps |
| Memory Speed | Low | Minimal impact due to zero-copy design |

### Software

| Factor | Impact | Notes |
|--------|--------|-------|
| Compiler Version | High | GCC 13.3 shows best results |
| Kernel Version | Medium | 5.19+ enables io_uring optimizations |
| CPU Governor | High | Use `performance` mode for benchmarking |
| Turbo Boost | High | Ensure enabled for fair comparison |

### Build Configuration

| Flag | Impact | Notes |
|------|--------|-------|
| `-O3` | Critical | Must use Release build |
| `-march=native` | High | Enables AVX2/AVX-512 SIMD |
| `-flto` | Medium | Link-time optimization |
| `NFX_ENABLE_SIMD` | Critical | Must be ON for claimed performance |

## Environment Setup for Fair Benchmarking

### 1. Set CPU Governor to Performance

```bash
# Check current governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Set to performance mode (requires root)
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

### 2. Disable CPU Frequency Scaling

```bash
# Disable turbo boost (for stable results)
echo 0 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

### 3. Pin to Specific CPU Core

```bash
# Run benchmark on core 0
taskset -c 0 ./build/bin/benchmarks/parse_benchmark 100000
```

### 4. Increase Process Priority

```bash
# Run with real-time priority (requires root)
sudo chrt -f 99 ./build/bin/benchmarks/parse_benchmark 100000
```

## Interpreting Results

### Latency Metrics

- **Mean**: Average latency across all iterations
- **P50 (Median)**: 50th percentile - half of iterations are faster
- **P99**: 99th percentile - critical for trading systems (tail latency)

### Why P99 Matters in HFT

In high-frequency trading:
- Mean latency is misleading (can hide outliers)
- **P99 determines worst-case execution time**
- A single slow parse can miss a trading opportunity

### Throughput Metric

**Throughput = 1,000,000,000 ns / Mean Latency**

Example:
- 246 ns mean latency → 4.07M messages/second
- Useful for high-volume market data processing

## Build Options for Benchmarking

```bash
# Default Release build (what we use for README numbers)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DNFX_ENABLE_SIMD=ON
cmake --build build -j

# Maximum optimization build
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DNFX_ENABLE_SIMD=ON \
  -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
  -DNFX_ENABLE_IO_URING=ON
cmake --build build -j

# Debug build (not for benchmarking)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DNFX_ENABLE_SIMD=OFF
```

## Troubleshooting

### Benchmark Results Much Slower Than Expected

**Possible causes**:
1. Not using Release build → Check: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
2. SIMD disabled → Check: `-DNFX_ENABLE_SIMD=ON`
3. CPU governor not in performance mode → See setup section
4. Running in VM or container → Use bare metal for accurate results
5. Older CPU without AVX2 → Expected, will fall back to scalar code

### QuickFIX Comparison Fails

**Error**: `libquickfix-dev not found`

**Solution**:
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install libquickfix-dev

# Or skip QuickFIX comparison
./start.sh bench 100000  # Only run NexusFIX benchmarks
```

### Compilation Errors

**Error**: `error: 'std::expected' is not a member of 'std'`

**Solution**: Upgrade to GCC 13+ or Clang 17+
```bash
# Ubuntu 24.04
sudo apt-get install g++-13

# Set as default
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100
```

## Reporting Issues

If your benchmark results differ significantly from README claims:

1. **Check build configuration** (Release mode, SIMD enabled)
2. **Run with environment setup** (CPU governor, pinning)
3. **Report your results** with:
   - CPU model (`cat /proc/cpuinfo | grep "model name" | head -1`)
   - Compiler version (`g++ --version`)
   - OS/Kernel (`uname -a`)
   - Benchmark output

Open an issue: https://github.com/Lattice9AI/NexusFix/issues

## License

This benchmark suite is part of NexusFix and is licensed under the MIT License.
