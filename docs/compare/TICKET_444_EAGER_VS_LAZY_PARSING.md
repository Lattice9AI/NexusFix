# Eager vs Lazy Parsing: Design Tradeoffs in FIX Protocol Engines

## Why NexusFix Chose Eager Parsing

NexusFix parses all fields upfront during message construction. This is a deliberate design choice, not an oversight. The reasoning: in quantitative trading systems, FIX messages are typically accessed multiple times (routing decisions, position updates, risk checks, audit logging). Paying the parse cost once and getting O(1) field access afterward makes the total cost lower for these workloads.

This document compares the two fundamental parsing models found in FIX protocol libraries and shows where each one wins.

## Two Parsing Models

FIX libraries fall into two camps based on when they actually parse field data.

**Eager parsing** scans the entire message at construction time. Every SOH delimiter is located, every tag number is extracted, every field boundary is recorded into an array. After construction, any field can be accessed in O(1) time by index. The upfront cost is higher, but subsequent access is essentially free.

**Lazy parsing** does minimal work at construction. Typically just enough to validate the message envelope (BeginString, BodyLength) and locate the message boundaries. Fields are parsed on demand as the caller iterates through the message. Construction is very fast, but each field access pays its own parse cost, and there is no random access without iterating from the beginning.

Neither model is universally better. The right choice depends on how messages are consumed downstream.

## Total Cost Comparison

Measuring only construction time gives an incomplete picture. The fair comparison includes field access costs, since lazy parsing defers work that eager parsing does upfront.

**ExecutionReport** (159 bytes, 19 fields):

| Scenario | Lazy Model | Eager Model | Winner |
|----------|-----------|-------------|--------|
| Construction only | ~15 cycles | ~490 cycles | Lazy |
| Construction + access 8 fields (1 pass) | ~135 cycles | ~490 cycles | Lazy |
| Construction + access 8 fields (2 passes) | ~255 cycles | ~490 cycles | Lazy |
| Construction + access all 19 fields (1 pass) | ~280 cycles | ~490 cycles | Lazy |
| Construction + access 8 fields (4 passes) | ~495 cycles | ~490 cycles | Tie |
| Construction + access all 19 fields (2 passes) | ~560 cycles | ~490 cycles | **Eager** |
| Construction + random access 10 fields (repeated) | ~560 cycles | ~490 cycles | **Eager** |

The crossover point sits around 4 sequential passes over the message, or 2 full iterations of all fields. Real trading systems routinely exceed this: the hot path checks OrdStatus and ExecType for routing, then reads quantities and prices for position updates, then the audit layer logs the full message.

## Where Each Model Wins

**Lazy parsing is better when:**
- Messages are filtered early and most get discarded (e.g., only processing fills, dropping acks)
- Hot path touches 5-6 fields from a single linear scan
- Messages are processed once and never revisited
- Fire-and-forget routing where you check MsgType and forward

**Eager parsing is better when:**
- Multiple components access the same message (strategy, risk, audit)
- Fields are accessed out of order or by random tag lookup
- CheckSum validation is required before business logic runs
- Predictable latency matters more than minimum latency (every field access is O(1), no variance)

## Design Rationale

NexusFix targets quantitative trading strategies where messages flow through multiple processing stages. A single ExecutionReport might be touched by the order manager (check fill status), the position tracker (update quantities), the risk engine (check limits), and the audit logger (record full message). With lazy parsing, each stage would re-scan the buffer from the beginning. With eager parsing, the message is parsed once and all subsequent access is a direct array lookup.

The eager model also provides stronger correctness guarantees. CheckSum validation happens at construction time, so corrupt messages are rejected before any business logic sees them. Parse errors surface in one place rather than appearing unpredictably during field iteration.

For workloads that genuinely only need a single linear scan, lazy parsing libraries like hffix are a better fit. NexusFix is designed for the multi-access pattern that dominates in strategy-driven trading systems.

## Benchmark Environment

| Parameter | Value |
|-----------|-------|
| CPU | x86-64 with AVX2 |
| CPU Core | Pinned (isolated) |
| Compiler | GCC 13, `-O3 -march=native -DNDEBUG` |
| Timing | RDTSC with `lfence` serialization |
| Iterations | 100,000 (after 10,000 warmup) |
| Allocations | Zero on hot path (both libraries) |

## Test Messages

| Message | Type | Size | Fields |
|---------|------|------|--------|
| ExecutionReport | 35=8 | 159 bytes | 19 |
| Logon | 35=A | 85 bytes | 9 |
| Heartbeat | 35=0 | 73 bytes | 7 |

All messages use correct FIX 4.4 checksums. Timing measures CPU cycles via RDTSC to eliminate clock frequency variance.

## References

- NexusFix parser: `include/nexusfix/parser/runtime_parser.hpp`
- hffix: [https://github.com/jamesdbrock/hffix](https://github.com/jamesdbrock/hffix)
- TICKET_443: SIMD Timestamp Parse Benchmark (separate analysis)
