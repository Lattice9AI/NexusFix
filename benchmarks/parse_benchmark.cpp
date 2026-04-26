// parse_benchmark.cpp
// NexusFIX Parse Latency Benchmark
// Target: < 200 ns for ExecutionReport (vs QuickFIX 2,000-5,000 ns)

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <x86intrin.h>

#include "nexusfix/nexusfix.hpp"

namespace nfx::bench {

// ============================================================================
// High-Resolution Timing Utilities
// ============================================================================

/// RDTSCP-based timing (lower overhead than lfence+rdtsc+lfence)
/// rdtscp is self-serializing for reads before it
/// We add lfence after to prevent subsequent instructions from reordering

/// Start timestamp - use rdtscp + lfence to ensure clean measurement start
inline uint64_t rdtsc_start() noexcept {
    uint32_t aux;
    uint64_t tsc = __rdtscp(&aux);  // Serializes previous instructions
    _mm_lfence();                    // Prevent subsequent reordering
    return tsc;
}

/// End timestamp - use lfence + rdtscp to ensure all work is captured
inline uint64_t rdtsc_end() noexcept {
    _mm_lfence();                    // Ensure previous instructions complete
    uint32_t aux;
    return __rdtscp(&aux);           // Read timestamp
}

/// Legacy rdtsc for compatibility (CPU frequency calibration)
inline uint64_t rdtsc() noexcept {
    uint32_t aux;
    return __rdtscp(&aux);
}

/// Get CPU frequency for cycle-to-nanosecond conversion
inline double get_cpu_freq_ghz() noexcept {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t start_cycles = rdtsc();

    // Busy wait for calibration
    volatile uint64_t dummy = 0;
    for (int i = 0; i < 10000000; ++i) {
        dummy += i;
    }

    uint64_t end_cycles = rdtsc();
    auto end_time = std::chrono::steady_clock::now();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - start_time).count();

    return static_cast<double>(end_cycles - start_cycles) / elapsed_ns;
}

/// Convert cycles to nanoseconds
inline double cycles_to_ns(uint64_t cycles, double freq_ghz) noexcept {
    return static_cast<double>(cycles) / freq_ghz;
}

// ============================================================================
// Statistics
// ============================================================================

struct BenchmarkStats {
    double min_ns;
    double max_ns;
    double mean_ns;
    double p50_ns;
    double p90_ns;
    double p99_ns;
    double p999_ns;
    double stddev_ns;
    size_t iterations;
};

BenchmarkStats calculate_stats(std::vector<double>& latencies) {
    BenchmarkStats stats{};
    stats.iterations = latencies.size();

    if (latencies.empty()) return stats;

    std::sort(latencies.begin(), latencies.end());

    stats.min_ns = latencies.front();
    stats.max_ns = latencies.back();

    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    stats.mean_ns = sum / latencies.size();

    // Percentiles
    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * latencies.size());
        if (idx >= latencies.size()) idx = latencies.size() - 1;
        return latencies[idx];
    };

    stats.p50_ns = percentile(0.50);
    stats.p90_ns = percentile(0.90);
    stats.p99_ns = percentile(0.99);
    stats.p999_ns = percentile(0.999);

    // Standard deviation
    double sq_sum = 0.0;
    for (double lat : latencies) {
        sq_sum += (lat - stats.mean_ns) * (lat - stats.mean_ns);
    }
    stats.stddev_ns = std::sqrt(sq_sum / latencies.size());

    return stats;
}

struct NamedStats {
    const char* name;
    BenchmarkStats stats;
};

void print_stats(const char* name, const BenchmarkStats& stats) {
    std::cout << "\n=== " << name << " ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Iterations: " << stats.iterations << "\n";
    std::cout << "  Min:    " << std::setw(10) << stats.min_ns << " ns\n";
    std::cout << "  Mean:   " << std::setw(10) << stats.mean_ns << " ns\n";
    std::cout << "  P50:    " << std::setw(10) << stats.p50_ns << " ns\n";
    std::cout << "  P90:    " << std::setw(10) << stats.p90_ns << " ns\n";
    std::cout << "  P99:    " << std::setw(10) << stats.p99_ns << " ns\n";
    std::cout << "  P99.9:  " << std::setw(10) << stats.p999_ns << " ns\n";
    std::cout << "  Max:    " << std::setw(10) << stats.max_ns << " ns\n";
    std::cout << "  StdDev: " << std::setw(10) << stats.stddev_ns << " ns\n";
}

void print_stats_json(const std::vector<NamedStats>& all_stats) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "{\n  \"benchmarks\": [\n";
    for (size_t i = 0; i < all_stats.size(); ++i) {
        const auto& ns = all_stats[i];
        std::cout << "    { \"name\": \"" << ns.name
                  << "\", \"p50_ns\": " << ns.stats.p50_ns
                  << ", \"p99_ns\": " << ns.stats.p99_ns
                  << ", \"p999_ns\": " << ns.stats.p999_ns
                  << ", \"mean_ns\": " << ns.stats.mean_ns
                  << ", \"min_ns\": " << ns.stats.min_ns
                  << ", \"max_ns\": " << ns.stats.max_ns
                  << ", \"stddev_ns\": " << ns.stats.stddev_ns
                  << ", \"iterations\": " << ns.stats.iterations
                  << " }";
        if (i + 1 < all_stats.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

// ============================================================================
// Test Message Builder (generates messages with correct checksums)
// ============================================================================

/// Build a valid FIX message with correct checksum
std::string build_fix_message(std::string_view body_without_checksum) {
    std::string msg(body_without_checksum);

    // Calculate checksum (sum of all bytes before checksum field)
    uint32_t sum = 0;
    for (char c : msg) {
        sum += static_cast<uint8_t>(c);
    }
    uint8_t checksum = static_cast<uint8_t>(sum % 256);

    // Format checksum as 3-digit string
    char cs[8];
    cs[0] = '1';
    cs[1] = '0';
    cs[2] = '=';
    cs[3] = static_cast<char>('0' + (checksum / 100));
    cs[4] = static_cast<char>('0' + ((checksum / 10) % 10));
    cs[5] = static_cast<char>('0' + (checksum % 10));
    cs[6] = '\x01';
    cs[7] = '\0';

    msg += cs;
    return msg;
}

// ============================================================================
// Test Messages (without checksum - will be added at runtime)
// ============================================================================

// Sample ExecutionReport (35=8) message body
constexpr std::string_view EXEC_REPORT_BODY =
    "8=FIX.4.4\x01"
    "9=147\x01"
    "35=8\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=12345\x01"
    "52=20240115-10:30:00.123\x01"
    "37=ORD123456\x01"
    "17=EXEC789012\x01"
    "150=0\x01"
    "39=0\x01"
    "54=1\x01"
    "151=1000\x01"
    "14=0\x01"
    "6=0\x01"
    "55=AAPL\x01"
    "38=1000\x01"
    "44=150.50\x01";

// Sample NewOrderSingle (35=D) message body
constexpr std::string_view NEW_ORDER_BODY =
    "8=FIX.4.4\x01"
    "9=136\x01"
    "35=D\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=100\x01"
    "52=20240115-10:30:00.000\x01"
    "11=CLORD001\x01"
    "55=AAPL\x01"
    "54=1\x01"
    "60=20240115-10:30:00.000\x01"
    "38=1000\x01"
    "40=2\x01"
    "44=150.00\x01"
    "59=0\x01";

// Sample Heartbeat (35=0) message body
constexpr std::string_view HEARTBEAT_BODY =
    "8=FIX.4.4\x01"
    "9=57\x01"
    "35=0\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=50\x01"
    "52=20240115-10:30:00.000\x01";

// ============================================================================
// FIXT 1.1 / FIX 5.0 Test Messages
// ============================================================================

// FIXT 1.1 Logon (35=A) with DefaultApplVerID
constexpr std::string_view FIXT11_LOGON_BODY =
    "8=FIXT.1.1\x01"
    "9=75\x01"
    "35=A\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=1\x01"
    "52=20240115-10:30:00.000\x01"
    "98=0\x01"
    "108=30\x01"
    "1137=7\x01";  // DefaultApplVerID = FIX 5.0

// FIXT 1.1 Heartbeat (35=0)
constexpr std::string_view FIXT11_HEARTBEAT_BODY =
    "8=FIXT.1.1\x01"
    "9=57\x01"
    "35=0\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=50\x01"
    "52=20240115-10:30:00.000\x01";

// FIX 5.0 ExecutionReport (35=8) with ApplVerID
constexpr std::string_view FIX50_EXEC_REPORT_BODY =
    "8=FIXT.1.1\x01"
    "9=66\x01"
    "35=8\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=12345\x01"
    "52=20240115-10:30:00.123\x01"
    "1128=7\x01"  // ApplVerID = FIX 5.0
    "37=ORD123456\x01"
    "17=EXEC789012\x01"
    "150=0\x01"
    "39=0\x01"
    "54=1\x01"
    "151=1000\x01"
    "14=0\x01"
    "6=0\x01"
    "55=AAPL\x01"
    "38=1000\x01"
    "44=150.50\x01";

// FIX 5.0 NewOrderSingle (35=D) with ApplVerID
constexpr std::string_view FIX50_NEW_ORDER_BODY =
    "8=FIXT.1.1\x01"
    "9=64\x01"
    "35=D\x01"
    "49=SENDER\x01"
    "56=TARGET\x01"
    "34=100\x01"
    "52=20240115-10:30:00.000\x01"
    "1128=7\x01"  // ApplVerID = FIX 5.0
    "11=CLORD001\x01"
    "55=AAPL\x01"
    "54=1\x01"
    "60=20240115-10:30:00.000\x01"
    "38=1000\x01"
    "40=2\x01"
    "44=150.00\x01"
    "59=0\x01";

// ============================================================================
// Benchmarks
// ============================================================================

/// Benchmark: Full message parsing with IndexedParser
NamedStats benchmark_indexed_parser(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(EXEC_REPORT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: Parse failed\n";
        }
    }

    return {"IndexedParser (ExecutionReport)", calculate_stats(latencies)};
}

/// Benchmark: Field access after parsing
NamedStats benchmark_field_access(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(EXEC_REPORT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    auto result = IndexedParser::parse(data);
    if (!result) {
        std::cerr << "Error: Initial parse failed\n";
        return {"Field Access (4 fields)", {}};
    }

    const auto& parsed = *result;

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto v = parsed.get_string(tag::OrderID::value);
        (void)v;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();

        // Access multiple fields
        auto order_id = parsed.get_string(tag::OrderID::value);
        auto exec_id = parsed.get_string(tag::ExecID::value);
        auto side = parsed.get_char(tag::Side::value);
        auto msg_type = parsed.msg_type();

        uint64_t end = rdtsc_end();
        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        // Use values to prevent optimization
        if (order_id.empty() && exec_id.empty() && side == '\0' && msg_type == '\0') {
            std::cerr << "Error: Field not found\n";
        }
    }

    return {"Field Access (4 fields)", calculate_stats(latencies)};
}

/// Benchmark: Message boundary detection
NamedStats benchmark_message_boundary(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    // Create buffer with multiple messages
    std::string single_msg = build_fix_message(EXEC_REPORT_BODY);
    std::string buffer;
    buffer.reserve(10000);
    for (int i = 0; i < 10; ++i) {
        buffer += single_msg;
    }

    std::span<const char> data{buffer.data(), buffer.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto boundary = simd::find_message_boundary(data);
        (void)boundary;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto boundary = simd::find_message_boundary(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!boundary.complete) {
            std::cerr << "Error: No boundary found\n";
        }
    }

    return {"Message Boundary Detection", calculate_stats(latencies)};
}

/// Benchmark: Heartbeat message parsing (smallest message)
NamedStats benchmark_heartbeat_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(HEARTBEAT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: Heartbeat parse failed\n";
        }
    }

    return {"Heartbeat Parse", calculate_stats(latencies)};
}

/// Benchmark: NewOrderSingle parsing
NamedStats benchmark_new_order_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(NEW_ORDER_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: NewOrderSingle parse failed\n";
        }
    }

    return {"NewOrderSingle Parse", calculate_stats(latencies)};
}

/// Benchmark: Checksum calculation
NamedStats benchmark_checksum(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    // Use message body without checksum for this benchmark
    std::string msg(EXEC_REPORT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto cs = fix::calculate_checksum(data);
        (void)cs;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto checksum = fix::calculate_checksum(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));
        (void)checksum;
    }

    return {"Checksum Calculation", calculate_stats(latencies)};
}

/// Benchmark: Integer parsing (common operation)
NamedStats benchmark_int_parsing(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    constexpr std::string_view int_str = "12345678";
    std::span<const char> data{int_str.data(), int_str.size()};

    FieldView fv{0, data};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto v = fv.as_int();
        (void)v;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto value = fv.as_int();
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (value != 12345678) {
            std::cerr << "Error: Wrong int value\n";
        }
    }

    return {"Integer Parsing", calculate_stats(latencies)};
}

/// Benchmark: FixedPrice parsing
NamedStats benchmark_price_parsing(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    constexpr std::string_view price_str = "150.12345678";
    std::span<const char> data{price_str.data(), price_str.size()};

    FieldView fv{0, data};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto v = fv.as_price();
        (void)v;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto price = fv.as_price();
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));
        (void)price;
    }

    return {"FixedPrice Parsing", calculate_stats(latencies)};
}

// ============================================================================
// FIXT 1.1 / FIX 5.0 Benchmarks
// ============================================================================

/// Benchmark: FIXT 1.1 Logon parsing
NamedStats benchmark_fixt11_logon_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(FIXT11_LOGON_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: FIXT 1.1 Logon parse failed\n";
        }
    }

    return {"FIXT 1.1 Logon Parse", calculate_stats(latencies)};
}

/// Benchmark: FIXT 1.1 Heartbeat parsing
NamedStats benchmark_fixt11_heartbeat_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(FIXT11_HEARTBEAT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: FIXT 1.1 Heartbeat parse failed\n";
        }
    }

    return {"FIXT 1.1 Heartbeat Parse", calculate_stats(latencies)};
}

/// Benchmark: FIX 5.0 ExecutionReport parsing
NamedStats benchmark_fix50_exec_report_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(FIX50_EXEC_REPORT_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: FIX 5.0 ExecutionReport parse failed\n";
        }
    }

    return {"FIX 5.0 ExecutionReport Parse", calculate_stats(latencies)};
}

/// Benchmark: FIX 5.0 NewOrderSingle parsing
NamedStats benchmark_fix50_new_order_parse(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string msg = build_fix_message(FIX50_NEW_ORDER_BODY);
    std::span<const char> data{msg.data(), msg.size()};

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        auto result = IndexedParser::parse(data);
        (void)result;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(data);
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!result) {
            std::cerr << "Error: FIX 5.0 NewOrderSingle parse failed\n";
        }
    }

    return {"FIX 5.0 NewOrderSingle Parse", calculate_stats(latencies)};
}

/// Benchmark: Version detection performance
NamedStats benchmark_version_detection(size_t iterations, double freq_ghz) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    std::string fix44_msg = build_fix_message(EXEC_REPORT_BODY);
    std::string fixt11_msg = build_fix_message(FIX50_EXEC_REPORT_BODY);

    auto fix44_parsed = IndexedParser::parse(std::span<const char>{fix44_msg.data(), fix44_msg.size()});
    auto fixt11_parsed = IndexedParser::parse(std::span<const char>{fixt11_msg.data(), fixt11_msg.size()});

    if (!fix44_parsed || !fixt11_parsed) {
        std::cerr << "Error: Failed to parse messages for version detection benchmark\n";
        return {"Version Detection (3 checks)", {}};
    }

    // Warmup
    for (size_t i = 0; i < 1000; ++i) {
        bool v1 = fix44_parsed->is_fix44();
        bool v2 = fixt11_parsed->is_fixt11();
        (void)v1; (void)v2;
    }

    // Benchmark
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        bool is_44 = fix44_parsed->is_fix44();
        bool is_fixt = fixt11_parsed->is_fixt11();
        bool is_fix4 = fix44_parsed->is_fix4();
        uint64_t end = rdtsc_end();

        latencies.push_back(cycles_to_ns(end - start, freq_ghz));

        if (!is_44 || !is_fixt || !is_fix4) {
            // Expected values
        }
    }

    return {"Version Detection (3 checks)", calculate_stats(latencies)};
}

/// Benchmark: FIX 4.4 vs FIX 5.0 comparison
void benchmark_fix44_vs_fix50_comparison(size_t iterations, double freq_ghz,
                                         bool json_mode, std::vector<NamedStats>& results) {
    std::string fix44_msg = build_fix_message(EXEC_REPORT_BODY);
    std::string fix50_msg = build_fix_message(FIX50_EXEC_REPORT_BODY);

    std::span<const char> fix44_data{fix44_msg.data(), fix44_msg.size()};
    std::span<const char> fix50_data{fix50_msg.data(), fix50_msg.size()};

    // FIX 4.4 benchmark
    std::vector<double> fix44_latencies;
    fix44_latencies.reserve(iterations);

    for (size_t i = 0; i < 1000; ++i) {
        auto r = IndexedParser::parse(fix44_data);
        (void)r;
    }

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(fix44_data);
        uint64_t end = rdtsc_end();
        fix44_latencies.push_back(cycles_to_ns(end - start, freq_ghz));
        (void)result;
    }

    // FIX 5.0 benchmark
    std::vector<double> fix50_latencies;
    fix50_latencies.reserve(iterations);

    for (size_t i = 0; i < 1000; ++i) {
        auto r = IndexedParser::parse(fix50_data);
        (void)r;
    }

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc_start();
        auto result = IndexedParser::parse(fix50_data);
        uint64_t end = rdtsc_end();
        fix50_latencies.push_back(cycles_to_ns(end - start, freq_ghz));
        (void)result;
    }

    auto fix44_stats = calculate_stats(fix44_latencies);
    auto fix50_stats = calculate_stats(fix50_latencies);

    results.push_back({"FIX 4.4 vs 5.0 (4.4 side)", fix44_stats});
    results.push_back({"FIX 4.4 vs 5.0 (5.0 side)", fix50_stats});

    if (!json_mode) {
        std::cout << "\n=== FIX 4.4 vs FIX 5.0 ExecutionReport Comparison ===\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "                 FIX 4.4      FIX 5.0      Diff\n";
        std::cout << "  Mean:     " << std::setw(10) << fix44_stats.mean_ns << " ns  "
                  << std::setw(10) << fix50_stats.mean_ns << " ns  "
                  << std::setw(+6) << (fix50_stats.mean_ns - fix44_stats.mean_ns) << " ns\n";
        std::cout << "  P50:      " << std::setw(10) << fix44_stats.p50_ns << " ns  "
                  << std::setw(10) << fix50_stats.p50_ns << " ns  "
                  << std::setw(+6) << (fix50_stats.p50_ns - fix44_stats.p50_ns) << " ns\n";
        std::cout << "  P99:      " << std::setw(10) << fix44_stats.p99_ns << " ns  "
                  << std::setw(10) << fix50_stats.p99_ns << " ns  "
                  << std::setw(+6) << (fix50_stats.p99_ns - fix44_stats.p99_ns) << " ns\n";
    }
}

} // namespace nfx::bench

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    using namespace nfx::bench;

    size_t iterations = 100000;
    bool json_mode = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else {
            iterations = std::stoul(argv[i]);
        }
    }

    // Calibrate CPU frequency (always to stderr so it doesn't pollute JSON)
    std::cerr << "Calibrating CPU frequency...\n";
    double freq_ghz = get_cpu_freq_ghz();
    std::cerr << "CPU frequency: " << std::fixed << std::setprecision(3)
              << freq_ghz << " GHz\n";

    std::vector<NamedStats> results;
    results.reserve(16);

    auto run = [&](auto benchmark_fn) {
        auto ns = benchmark_fn(iterations, freq_ghz);
        if (!json_mode) {
            print_stats(ns.name, ns.stats);
        }
        results.push_back(ns);
    };

    if (!json_mode) {
        std::cout << "NexusFIX Parse Benchmark\n";
        std::cout << "========================\n";
        std::cout << "Iterations: " << iterations << "\n";
        std::cout << "\n--- FIX 4.4 Benchmarks ---\n";
    }

    run(benchmark_indexed_parser);
    run(benchmark_field_access);
    run(benchmark_message_boundary);
    run(benchmark_heartbeat_parse);
    run(benchmark_new_order_parse);
    run(benchmark_checksum);
    run(benchmark_int_parsing);
    run(benchmark_price_parsing);

    if (!json_mode) {
        std::cout << "\n--- FIXT 1.1 / FIX 5.0 Benchmarks ---\n";
    }

    run(benchmark_fixt11_logon_parse);
    run(benchmark_fixt11_heartbeat_parse);
    run(benchmark_fix50_exec_report_parse);
    run(benchmark_fix50_new_order_parse);
    run(benchmark_version_detection);

    benchmark_fix44_vs_fix50_comparison(iterations, freq_ghz, json_mode, results);

    if (json_mode) {
        print_stats_json(results);
    } else {
        std::cout << "\n";
        std::cout << "========================================\n";
        std::cout << "Target: ExecutionReport parse < 200 ns\n";
        std::cout << "  FIX 4.4 and FIX 5.0 should have\n";
        std::cout << "  similar performance (no regression)\n";
        std::cout << "========================================\n";
    }

    return 0;
}
