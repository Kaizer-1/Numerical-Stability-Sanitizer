#!/usr/bin/env bash
set -euo pipefail

CLANG="${1:-build/numerical-clang}"

if [ ! -x "$CLANG" ]; then
    echo "ERROR: compiler wrapper not found at '$CLANG'."
    echo "Run ./build.sh first, or pass the wrapper path as the first argument."
    exit 1
fi

echo "======================================================="
echo " NSan smoke test"
echo " Compiler: $CLANG"
echo "======================================================="

# ── 1. Canonical example ────────────────────────────────────
echo ""
echo "--- Example: catastrophic cancellation (big + tiny) - big ---"
TMPBIN=$(mktemp /tmp/nsan_example.XXXXXX)
"$CLANG" -fsanitize=numerical -O1 -g examples/terminal_input.c -o "$TMPBIN"
echo "100000000 1" | "$TMPBIN" 2>&1 || true
rm -f "$TMPBIN"

# ── 2. Full test suite ──────────────────────────────────────
echo ""
echo "--- Running all 19 test cases ---"
python3 tests/run_tests.py --clang "$CLANG"

# ── 3. Benchmarks ───────────────────────────────────────────
echo ""
echo "--- Benchmark overhead (3 iterations, quick mode) ---"
python3 benchmarks/run_benchmarks.py --clang "$CLANG" --iterations 3 --quick

echo ""
echo "Done."
