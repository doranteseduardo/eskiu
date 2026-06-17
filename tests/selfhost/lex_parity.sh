#!/usr/bin/env bash
# Parity gate for the self-hosted lexer (selfhost/lexer.esk) vs the C++ lexer.
#
# For each corpus file we run BOTH lexers and normalize each output to a canonical
# token stream `line|col|NAME|value`, one token per line, then `diff`. The two
# printers format differently (the C++ `--test-lexer` pads columns + prints a
# banner; the Eskiu driver prints `Line L, Col C: NAME 'value'`) — the normalizer
# erases that, so the diff compares the *token stream*, not the formatting.
#
# Usage: tests/selfhost/lex_parity.sh [file.esk ...]
#   no args -> the whole corpus under tests/selfhost/inputs/
# Green (exit 0) = byte-identical normalized streams for every file.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "lex_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi

DRIVER=selfhost/lex_main.esk

# Extract `line|col|NAME|value` from either printer; non-token lines (banners,
# "Total tokens", usage) don't match the pattern and are dropped.
normalize() {
    sed -nE "s/.*Line +([0-9]+), Col +([0-9]+):? +([A-Za-z_0-9?]+) +'(.*)'.*/\1|\2|\3|\4/p"
}

if [ "$#" -gt 0 ]; then
    files=("$@")
else
    files=(tests/selfhost/inputs/*.esk)
fi

fail=0
total=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "MISS  $f (no such file)"; fail=1; continue; }
    total=$((total + 1))
    ref=$("$BIN" --test-lexer "$f" 2>/dev/null | normalize)
    got=$("$BIN" run "$DRIVER" "$f" 2>/dev/null | normalize)
    if [ "$ref" = "$got" ]; then
        echo "ok    $f"
    else
        echo "FAIL  $f"
        diff <(printf '%s\n' "$ref") <(printf '%s\n' "$got") | sed 's/^/      /'
        fail=1
    fi
done

echo "----"
if [ "$fail" -eq 0 ]; then
    echo "parity: $total/$total files match"
else
    echo "parity: MISMATCH"
fi
exit "$fail"
