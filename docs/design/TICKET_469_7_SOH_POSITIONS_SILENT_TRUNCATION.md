# TICKET_469_7: SohPositions Silently Truncates Beyond MAX_SOH_POSITIONS

| Field | Value |
|-------|-------|
| Created | 2026-04-26 |
| Status | Resolved |
| Resolved | 2026-04-26 |
| Priority | High |
| Category | Parser / Data Integrity |
| Source | TICKET_469 audit expansion |

---

## Problem

`SohPositions::push()` silently drops SOH positions when the count reaches `MAX_SOH_POSITIONS` (256). No error is returned and no flag is set. Downstream consumers have no way to know the structural index is incomplete.

### Current Behavior

- `simd_scanner.hpp:74-78`: `push()` does nothing when `count >= MAX_SOH_POSITIONS`
- `simd_scanner.hpp:89`: `scan_soh_scalar()` calls `push()` in a loop with no overflow check
- `structural_index.hpp:222`: `build_index_scalar()` guards on `idx.soh_count < MAX_FIELDS` but relies on `SohPositions` not losing data independently
- SIMD paths (`build_index_xsimd`, AVX2, AVX-512) also feed through `push()` without overflow awareness

### Reproduction

A FIX message with 257+ SOH-delimited fields. SOH position 257 onward is silently lost. The structural index reports fewer fields than actually exist.

### Risk

- Double truncation: `SohPositions` truncates at 256, then `ParsedMessage` truncates again at 128. Two independent silent truncation layers with no error propagation.
- Fields beyond position 256 are invisible to all downstream parsers.
- Unlike TICKET_469_5 which truncates at the parse level, this truncates at the structural scan level, affecting every parser that depends on the structural index.

## Affected Files

- `include/nexusfix/parser/simd_scanner.hpp`
- `include/nexusfix/parser/structural_index.hpp`
- `tests/test_parser.cpp`

## Recommended Remediation

1. Add an `overflow` boolean flag to `SohPositions` that is set when `push()` is called at capacity.
2. Propagate the overflow flag through `FIXStructuralIndex` so parsers can detect incomplete scans.
3. In strict mode, return a parse error when structural index overflow is detected.
4. Add regression test with 257+ fields verifying overflow detection.

## Resolution

1. Added `truncated_` boolean flag to `SohPositions`, set when `push()` is called at capacity.
2. Added `check_truncation()` post-scan method that scans remaining data for unrecorded SOH bytes when buffer is at capacity.
3. All four scan paths (scalar, xsimd, AVX2, AVX-512) call `check_truncation()` after scanning.
4. `ParsedMessage::parse()` checks `soh_positions.truncated()` and returns `FieldCountExceeded` error.
5. Four regression tests: push overflow, scan_soh >256 truncation, scan_soh exactly 256 boundary (no truncation), ParsedMessage rejection.

## Related

- TICKET_469_5: ParsedMessage field truncation (truncates at parse level)
- TICKET_469_2: Duplicate tag detection
