#!/usr/bin/env bash
# Smoke regression. Runs each kept command with a canonical scenario and
# diffs the output against scripts/smoke_expected/.
#
# Usage:
#   scripts/smoke.sh           # diff against baselines, exit 1 on diff
#   UPDATE=1 scripts/smoke.sh  # update baselines instead of diffing
set -euo pipefail

cd "$(dirname "$0")/.."

# Find the binary. Build system uses compiler-version subdir.
BIN=""
for candidate in \
    release_debug/gcc-15/bin/swr_calculator \
    release_debug/bin/swr_calculator \
    release_debug/swr_calculator \
; do
    if [[ -x "$candidate" ]]; then
        BIN="$candidate"
        break
    fi
done
if [[ -z "$BIN" ]]; then
    echo "Smoke: cannot find swr_calculator binary. Run 'make' first."
    exit 1
fi

EXP_DIR="scripts/smoke_expected"
mkdir -p "$EXP_DIR"

run_one() {
    local name="$1"; shift
    local expected="$EXP_DIR/$name.txt"
    local actual
    actual="$("$BIN" "$@" 2>&1 || true)"
    if [[ "${UPDATE:-0}" == "1" ]]; then
        printf '%s\n' "$actual" > "$expected"
        echo "Updated: $expected"
        return 0
    fi
    if [[ ! -f "$expected" ]]; then
        echo "Smoke: missing baseline $expected. Run 'UPDATE=1 scripts/smoke.sh'."
        return 1
    fi
    if ! diff -u "$expected" <(printf '%s\n' "$actual"); then
        echo "Smoke FAIL: $name"
        return 1
    fi
    echo "Smoke OK: $name"
}

# Phase 1: only dynamic_dollar uses flag style. The others still take
# positional args here; their smoke entries land in Phase 2.
run_one dynamic_dollar \
    dynamic_dollar \
    --balance 850000 --current_age 67 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" --inflation us_inflation \
    --rebalance yearly

echo "Smoke: all checks passed"
