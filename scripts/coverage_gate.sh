#!/usr/bin/env bash
#
# coverage_gate.sh - Hard branch-coverage gate for CI (TICKET_497 Phase 5).
#
# Parses a merged lcov tracefile and fails if overall branch coverage or any
# per-module floor drops below its anchor. Anchors are the coverage measured on
# 2026-07-17 (merged SIMD off + on, GCC 14, lcov 2.0) minus a 2-point margin, so
# the gate lands green and absorbs the run-to-run jitter in branch totals that
# comes from template-instantiation counts shifting between test call sites.
#
# This gate is independent of Codecov: it holds even when the CODECOV_TOKEN
# secret is absent (forks, first-party PRs before the secret is set).
#
# Usage:
#   scripts/coverage_gate.sh <coverage.info>
#
# Exit status: 0 if every floor is met, 1 otherwise (with a per-module report).
#
# Floors are BRANCH percentages. Modules are the top-level directories under
# include/nexusfix/. sbe/ is floored at its measured GCC-artifact ceiling: 169
# of its uncovered branches sit on inlined memcpy/memset size-dispatch lines in
# composite_types.hpp that no source-level test can flip (TICKET_497 Phase 1).
set -euo pipefail

INFO="${1:?usage: coverage_gate.sh <coverage.info>}"

if [[ ! -f "${INFO}" ]]; then
    echo "coverage_gate: tracefile not found: ${INFO}" >&2
    exit 2
fi

# module -> floor (branch %). Overall floor is the special key "__overall__".
# Anchored to the 2026-07-16 merged measurement minus 2 points.
python3 - "${INFO}" <<'PY'
import re, sys, collections

info = sys.argv[1]

# Per-module branch-coverage floors (achieved-minus-2, re-anchored 2026-07-17
# after TICKET_499 WI3). Floors are computed over source-level branches only:
# LCOV_EXCL_BR_LINE markers are honored by this gate (see load_excl below), which
# removes the GCC template-inlining artifact branches lcov itself cannot exclude
# (SBE codec isValid chains, FixedString encode memcpy/memset dispatch, dispatch
# switch). Achieved snapshot for reference: engine 73.5, memory 72.4,
# messages 82.2, parser 85.0, platform 91.5, sbe 75.0, serializer 92.3,
# session 86.2, store 76.8, transport 73.6, types 87.2, util 70.8; overall 81.2.
# Raised from the 73.0 TICKET_497_3 baseline by the TICKET_499 WI3 test batches.
FLOORS = {
    "engine":     71.0,
    "memory":     70.0,
    "messages":   80.0,
    "parser":     83.0,
    "platform":   89.0,
    "sbe":        73.0,
    "serializer": 90.0,
    "session":    84.0,
    "store":      74.0,
    "transport":  71.0,
    "types":      85.0,
    "util":       68.0,
}
OVERALL_FLOOR = 79.0

mods = collections.defaultdict(lambda: [0, 0])  # module -> [hit, total]
overall = [0, 0]
cur = None
excl_lines = set()   # source line numbers carrying LCOV_EXCL_BR_LINE for cur file

# lcov honors LCOV_EXCL_BR_LINE for ordinary source branches, but NOT for
# branches gcov attributes to a template body line that was inlined at call
# sites in other files (e.g. FixedString<N>::encode's memcpy/memset dispatch in
# sbe/types/composite_types.hpp). Those artifact branch pairs survive capture.
# Honor the marker here so it is authoritative regardless of lcov's inlining
# quirk: skip any BRDA whose line carries the marker in the source file.
def load_excl(src_path):
    lines = set()
    try:
        with open(src_path) as sf:
            for i, ln in enumerate(sf, 1):
                if "LCOV_EXCL_BR_LINE" in ln:
                    lines.add(i)
    except OSError:
        pass
    return lines

with open(info) as f:
    for line in f:
        line = line.strip()
        if line.startswith("SF:"):
            src = line[3:]
            m = re.search(r"include/nexusfix/([^/]+)/", src)
            cur = m.group(1) if m else None
            excl_lines = load_excl(src) if cur is not None else set()
        elif line.startswith("BRDA:"):
            brda_line = int(line[5:].split(",")[0])
            if brda_line in excl_lines:
                continue  # excluded artifact branch (LCOV_EXCL_BR_LINE)
            taken = line.split(",")[-1]
            hit = 1 if taken not in ("-", "0") else 0
            overall[1] += 1
            overall[0] += hit
            if cur is not None:
                mods[cur][1] += 1
                mods[cur][0] += hit

def rate(h, t):
    return 100.0 * h / t if t else 100.0

failures = []
print(f"{'module':<14} {'branch%':>8} {'hit/total':>12} {'floor':>7}  status")
print("-" * 52)

for mod in sorted(set(list(FLOORS) + list(mods))):
    h, t = mods.get(mod, [0, 0])
    r = rate(h, t)
    floor = FLOORS.get(mod)
    if floor is None:
        status = "(no floor)"
    elif t == 0:
        # A floored module with zero branches means the tracefile lost this
        # module's branch data (path stopped matching, or lcov dropped it).
        # rate() would return a vacuous 100% and green-light the gap, so fail.
        status = "FAIL"
        failures.append(f"{mod}: 0 branches recorded (expected floor {floor:.1f}%)")
    elif r + 1e-9 >= floor:
        status = "ok"
    else:
        status = "FAIL"
        failures.append(f"{mod}: {r:.1f}% < floor {floor:.1f}%")
    fl = f"{floor:.1f}" if floor is not None else "-"
    print(f"{mod:<14} {r:>7.1f}% {h}/{t:<10} {fl:>7}  {status}")

oh, ot = overall
orate = rate(oh, ot)
print("-" * 52)
if ot == 0:
    # No branch data at all: lcov produced no BRDA records (gcov version
    # mismatch, --rc branch flag regression). rate() reads a vacuous 100%.
    ostatus = "FAIL"
    failures.append("overall: 0 branches recorded (no BRDA data in tracefile)")
else:
    ostatus = "ok" if orate + 1e-9 >= OVERALL_FLOOR else "FAIL"
    if ostatus == "FAIL":
        failures.append(f"overall: {orate:.1f}% < floor {OVERALL_FLOOR:.1f}%")
print(f"{'OVERALL':<14} {orate:>7.1f}% {oh}/{ot:<10} {OVERALL_FLOOR:>7.1f}  {ostatus}")

if failures:
    print("\ncoverage_gate: FAILED")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)

print("\ncoverage_gate: PASSED")
PY
