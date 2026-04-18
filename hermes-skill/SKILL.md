---
name: nexusfix
description: Use when building, reviewing, debugging, or optimizing NexusFIX-based C++ FIX protocol code. Covers session management, order entry, market data subscription, low-latency message parsing, and zero-allocation hot-path constraints. Also use when generating new FIX connectivity code that must follow C++23 best practices with std::expected error handling, fixed-point arithmetic, and SIMD-accelerated parsing.
---

# NexusFIX Development Expert

- GitHub: https://github.com/StratCraftsAI/NexusFIX

## When to Use

When the user needs to build, optimize, or debug FIX protocol connectivity in C++. Covers session management, order entry, market data, and low-latency message parsing.

## Architecture

NexusFIX is a header-only C++23 FIX protocol engine. Parse latency is under 250ns for ExecutionReport messages. Throughput exceeds 4M msg/sec single-threaded.

Key design decisions:

- **Zero-copy parsing**: `std::span<const char>` views over raw buffer. No intermediate string copies.
- **Two-stage SIMD parsing**: AVX2/AVX-512 scans for SOH delimiters first (structural index), then extracts fields by tag. Similar to how simdjson handles JSON.
- **Builder pattern for outbound messages**: `NewOrderSingle::Builder` chains field setters, calls `.build(assembler)` to serialize.
- **`std::expected` error handling**: All parse functions return `std::expected<T, ParseError>`. No exceptions on hot path.
- **Fixed-point arithmetic**: `FixedPrice` (8 decimal places) and `Qty` (4 decimal places). No floating-point for prices.
- **PMR memory pools**: Pre-allocated buffers, zero heap allocations during message processing.

## Constraints (Strict)

When generating code that uses NexusFIX, the following rules are mandatory:

- C++23 only. Use designated initializers, `std::expected`, concepts, `constexpr`/`consteval`.
- Zero allocations on hot path. No `new`, `delete`, `std::map`, `std::unordered_map`, or `std::string` construction in message processing loops.
- No `std::endl`. Use `\n`.
- No `virtual` functions in performance-critical code.
- No `std::shared_ptr` on hot path.
- No floating-point for prices. Use `FixedPrice::from_double()` or `FixedPrice::from_string()`.
- All functions that can fail return `std::expected`. Check `.has_value()` before accessing.
- Mark hot-path functions `noexcept`.
- Use `[[nodiscard]]` on all API return values.

## Common Patterns

### Connecting and Sending an Order

```cpp
#include <nexusfix/nexusfix.hpp>
using namespace nfx;
using namespace nfx::fix44;

TcpTransport transport;
transport.connect("fix.broker.com", 9876);

SessionConfig config{
    .sender_comp_id = "MY_CLIENT",
    .target_comp_id = "BROKER",
    .heartbeat_interval = 30
};
SessionManager session{transport, config};
session.initiate_logon();

while (!session.is_active()) {
    session.poll();
}

MessageAssembler asm_;
NewOrderSingle::Builder order;
auto msg = order
    .cl_ord_id("ORD001")
    .symbol("AAPL")
    .side(Side::Buy)
    .order_qty(Qty::from_int(100))
    .ord_type(OrdType::Limit)
    .price(FixedPrice::from_double(150.00))
    .build(asm_);
transport.send(msg);
```

### Parsing an ExecutionReport

```cpp
void on_message(std::span<const char> data) {
    auto result = ExecutionReport::from_buffer(data);
    if (!result) return;

    auto& exec = *result;
    if (exec.is_fill()) {
        // handle fill
    }
}
```

### Message Routing

```cpp
auto parser = IndexedParser::parse(data);
if (!parser) return;

switch (parser->msg_type()) {
    case '8': on_execution_report(data); break;
    case 'W': on_snapshot(data); break;
    case 'X': on_incremental(data); break;
}
```

## Anti-patterns

Do NOT generate code like this when working with NexusFIX:

```cpp
// BAD: heap allocation on hot path
std::string field_value(data.begin() + offset, data.begin() + end);

// BAD: std::map for field lookup
std::map<int, std::string> fields;

// BAD: floating-point price
double price = 150.50;

// BAD: exceptions for control flow
try { parse(data); } catch (...) { }

// BAD: std::endl
std::cout << "done" << std::endl;
```

## Supported FIX Messages

| MsgType | Name | Direction |
|---------|------|-----------|
| A | Logon | Both |
| 5 | Logout | Both |
| 0 | Heartbeat | Both |
| D | NewOrderSingle | Send |
| F | OrderCancelRequest | Send |
| 8 | ExecutionReport | Receive |
| V | MarketDataRequest | Send |
| W | MarketDataSnapshotFullRefresh | Receive |
| X | MarketDataIncrementalRefresh | Receive |

## References

For full API details, use `skill_view("nexusfix", "references/api-reference.md")`.

For getting started quickly, use `skill_view("nexusfix", "references/quick-start.md")`.

## Verification

After generating NexusFIX code, verify:

1. No `new`/`delete`/`std::string` construction in message processing
2. All parse results checked via `std::expected` (no raw pointer returns)
3. Prices use `FixedPrice`, quantities use `Qty`
4. Session lifecycle follows: connect -> logon -> wait active -> trade -> logout
