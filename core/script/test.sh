#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# core/script/test.sh — Build, test, and collect benchmarks for libve core
# ----------------------------------------------------------------------------
# Usage:
#   bash core/script/test.sh                          # from project root
#   bash core/script/test.sh --build-dir <path>       # custom build dir
#   bash core/script/test.sh --no-build               # skip build
#   bash core/script/test.sh --bench-only             # bench only
# ----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

BUILD_DIR=""
CONFIG="Release"
NO_BUILD=0
BENCH_ONLY=0

# --- Parse args ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --config)     CONFIG="$2"; shift 2 ;;
        --no-build)   NO_BUILD=1; shift ;;
        --bench-only) BENCH_ONLY=1; NO_BUILD=1; shift ;;
        *)            echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --- Auto-detect build dir ---
if [[ -z "$BUILD_DIR" ]]; then
    for c in cmake-build-release cmake-build-release-vs2022 build out/build/Release; do
        if [[ -d "$PROJECT_ROOT/$c" ]]; then
            BUILD_DIR="$PROJECT_ROOT/$c"
            break
        fi
    done
    if [[ -z "$BUILD_DIR" ]]; then
        echo "[ERROR] No build directory found. Use --build-dir <path>"
        exit 1
    fi
fi

# resolve relative
[[ "$BUILD_DIR" != /* ]] && BUILD_DIR="$PROJECT_ROOT/$BUILD_DIR"

BIN_DIR="$BUILD_DIR/bin"
TEST_EXE="$BIN_DIR/ve_test"
OUT_DIR="$PROJECT_ROOT/core/script/output"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
TEST_LOG="$OUT_DIR/test_${TIMESTAMP}.log"
BENCH_LOG="$OUT_DIR/bench_${TIMESTAMP}.log"

echo ""
echo "======================================"
echo "  ve core — test & benchmark"
echo "======================================"
echo "  Project : $PROJECT_ROOT"
echo "  Build   : $BUILD_DIR"
echo "  Config  : $CONFIG"
echo ""

mkdir -p "$OUT_DIR"

# --- Build ---
if [[ $NO_BUILD -eq 0 && $BENCH_ONLY -eq 0 ]]; then
    echo "[1/3] Building ve_test ..."
    if cmake --build "$BUILD_DIR" --config "$CONFIG" --target ve_test 2>&1 | tail -5; then
        echo "[BUILD OK]"
    else
        echo "[BUILD FAILED]"
        exit 1
    fi
else
    echo "[1/3] Build skipped"
fi

# --- Check exe ---
if [[ ! -x "$TEST_EXE" ]]; then
    echo "[ERROR] $TEST_EXE not found or not executable"
    exit 1
fi

# --- Run tests ---
echo "[2/3] Running ve_test ..."
set +e
OUTPUT=$("$TEST_EXE" 2>&1)
EXIT_CODE=$?
set -e

echo "$OUTPUT" > "$TEST_LOG"
echo "  Full log : $TEST_LOG"

# --- Summary ---
SUMMARY=$(echo "$OUTPUT" | grep -E "passed.*failed" | tail -1)
if [[ -n "$SUMMARY" ]]; then
    if [[ $EXIT_CODE -eq 0 ]]; then
        echo -e "  \033[32m${SUMMARY}\033[0m"
    else
        echo -e "  \033[31m${SUMMARY}\033[0m"
        echo "$OUTPUT" | grep -E "^\s*FAIL" | while read -r line; do
            echo -e "    \033[31m${line}\033[0m"
        done
    fi
fi

# --- Extract benchmarks ---
echo "[3/3] Extracting benchmarks ..."
BENCH_LINES=$(echo "$OUTPUT" | grep "\[bench\]" || true)

if [[ -n "$BENCH_LINES" ]]; then
    COUNT=$(echo "$BENCH_LINES" | wc -l)
    {
        echo "# ve core benchmark — $(date '+%Y-%m-%d %H:%M:%S')"
        echo "# Config: $CONFIG"
        echo ""
        echo "$BENCH_LINES" | sed 's/.*\(\[bench\]\)/\1/'
    } > "$BENCH_LOG"

    echo "  Benchmarks ($COUNT entries) : $BENCH_LOG"
    echo ""
    echo "$BENCH_LINES" | sed 's/.*\(\[bench\]\)/\1/' | while read -r line; do
        echo -e "  \033[36m${line}\033[0m"
    done
else
    echo "  No [bench] entries found"
fi

echo ""
echo "======================================"
if [[ $EXIT_CODE -eq 0 ]]; then
    echo -e "  \033[32mALL DONE — tests passed\033[0m"
else
    echo -e "  \033[31mDONE — some tests failed (exit $EXIT_CODE)\033[0m"
fi
echo "======================================"
echo ""

exit $EXIT_CODE
