// Simple parse test to debug performance
#include <iostream>
#include <span>
#include <string_view>
#include "nexusfix/parser/runtime_parser.hpp"
#include "benchmark_utils.hpp"

int main() {
    // Simple heartbeat message
    const char* msg = "8=FIX.4.4\x01" "9=80\x01" "35=0\x01" "49=SENDER\x01" "56=TARGET\x01" "34=102\x01" "52=20240331-14:30:47.789\x01" "10=789\x01";
    size_t len = std::strlen(msg);

    std::cout << "Message length: " << len << "\n";
    std::cout << "Message: ";
    for (size_t i = 0; i < len; ++i) {
        char c = msg[i];
        if (c == '\x01') std::cout << '|';
        else std::cout << c;
    }
    std::cout << "\n\n";

    // Test parse
    auto result = nfx::ParsedMessage::parse(std::span<const char>(msg, len));

    if (result.has_value()) {
        std::cout << "Parse SUCCESS\n";
        std::cout << "Field count: " << result->field_count() << "\n";
    } else {
        std::cout << "Parse FAILED\n";
        std::cout << "Error: " << static_cast<int>(result.error().code) << "\n";
        std::cout << "Offset: " << result.error().offset << "\n";
    }

    // Benchmark
    constexpr size_t iterations = 100'000;
    uint64_t total_cycles = 0;

    for (size_t i = 0; i < iterations; ++i) {
        auto start = nfx::bench::rdtsc_vm_safe();
        auto parsed = nfx::ParsedMessage::parse(std::span<const char>(msg, len));
        auto end = nfx::bench::rdtsc_vm_safe();

        if (parsed.has_value()) {
            total_cycles += (end - start);
        }
    }

    std::cout << "\nAverage cycles: " << (total_cycles / iterations) << "\n";

    return 0;
}
