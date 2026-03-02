#!/bin/bash
# check_perf_regression.sh - Performance regression gate
# TICKET_228 Phase 3: Compares parse_benchmark --json output against baselines.json thresholds.
# Exits non-zero if any benchmark exceeds its absolute threshold.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${PROJECT_DIR}/build"
BENCHMARK_BIN="${BUILD_DIR}/bin/benchmarks/parse_benchmark"
BASELINES="${PROJECT_DIR}/benchmarks/baselines.json"
ITERATIONS="${1:-10000}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if ! command -v jq &>/dev/null; then
    echo -e "${RED}[ERROR]${NC} jq is required but not installed. Install with: sudo apt-get install -y jq"
    exit 1
fi

if [ ! -f "${BASELINES}" ]; then
    echo -e "${RED}[ERROR]${NC} Baselines file not found: ${BASELINES}"
    exit 1
fi

# Build parse_benchmark if not found
if [ ! -x "${BENCHMARK_BIN}" ]; then
    echo -e "${YELLOW}[WARN]${NC} parse_benchmark not found, building..."
    cmake --build "${BUILD_DIR}" --target parse_benchmark -j"$(nproc 2>/dev/null || echo 4)"
fi

echo "Running parse_benchmark (${ITERATIONS} iterations, JSON mode)..."
RESULTS=$("${BENCHMARK_BIN}" "${ITERATIONS}" --json)

if ! echo "${RESULTS}" | jq . >/dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} parse_benchmark --json produced invalid JSON"
    echo "${RESULTS}"
    exit 1
fi

FAILURES=0
CHECKED=0
BASELINE_COUNT=$(jq '.baselines | length' "${BASELINES}")

for i in $(seq 0 $((BASELINE_COUNT - 1))); do
    NAME=$(jq -r ".baselines[$i].name" "${BASELINES}")
    P50_MAX=$(jq ".baselines[$i].p50_max_ns" "${BASELINES}")
    P99_MAX=$(jq ".baselines[$i].p99_max_ns" "${BASELINES}")

    # Find matching benchmark in results
    P50_ACTUAL=$(echo "${RESULTS}" | jq -r ".benchmarks[] | select(.name == \"${NAME}\") | .p50_ns")
    P99_ACTUAL=$(echo "${RESULTS}" | jq -r ".benchmarks[] | select(.name == \"${NAME}\") | .p99_ns")

    if [ -z "${P50_ACTUAL}" ] || [ "${P50_ACTUAL}" = "null" ]; then
        echo -e "${YELLOW}[SKIP]${NC} ${NAME} - not found in benchmark results"
        continue
    fi

    CHECKED=$((CHECKED + 1))

    # Compare using awk for floating point
    P50_FAIL=$(awk "BEGIN { print (${P50_ACTUAL} > ${P50_MAX}) ? 1 : 0 }")
    P99_FAIL=$(awk "BEGIN { print (${P99_ACTUAL} > ${P99_MAX}) ? 1 : 0 }")

    if [ "${P50_FAIL}" -eq 1 ] || [ "${P99_FAIL}" -eq 1 ]; then
        echo -e "${RED}[FAIL]${NC} ${NAME}"
        if [ "${P50_FAIL}" -eq 1 ]; then
            echo "        P50: ${P50_ACTUAL} ns > ${P50_MAX} ns (threshold)"
        fi
        if [ "${P99_FAIL}" -eq 1 ]; then
            echo "        P99: ${P99_ACTUAL} ns > ${P99_MAX} ns (threshold)"
        fi
        FAILURES=$((FAILURES + 1))
    else
        echo -e "${GREEN}[PASS]${NC} ${NAME}  P50=${P50_ACTUAL}ns/<${P50_MAX}  P99=${P99_ACTUAL}ns/<${P99_MAX}"
    fi
done

echo ""
echo "Checked ${CHECKED} benchmarks against baselines"

if [ "${FAILURES}" -gt 0 ]; then
    echo -e "${RED}${FAILURES} benchmark(s) exceeded performance thresholds${NC}"
    exit 1
fi

echo -e "${GREEN}All benchmarks within performance thresholds${NC}"
exit 0
