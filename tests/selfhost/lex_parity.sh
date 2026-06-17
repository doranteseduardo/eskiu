#!/usr/bin/env bash
# Parity gate for the self-hosted lexer (selfhost/lexer.esk) vs the C++ lexer.
#
# For each corpus file we run BOTH lexers. The Eskiu driver emits token lines in
# the EXACT format of the C++ `--test-lexer` (same padding, same decoded values),
# so we just strip the C++ banner/separators/total and raw-`diff` the token
# streams. Byte-identical = parity (embedded newlines in string values, decoded
# identically on both sides, match too — no fragile re-parsing of the output).
#
# Usage: tests/selfhost/lex_parity.sh [file.esk ...]
#   no args -> the whole corpus under tests/selfhost/inputs/
# Green (exit 0) = byte-identical token streams for every file.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "lex_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi

DRIVER=selfhost/lex_main.esk

# The C++ --test-lexer wraps the token lines in a banner; strip those three
# format-only lines (anchored at column 0 — real token lines start with "  Line"
# and value-continuation lines start with value content, so neither is touched).
strip_banner() {
    sed -E '/^Tokenizing: /d; /^=+$/d; /^Total tokens: /d'
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
    ref=$("$BIN" --test-lexer "$f" 2>/dev/null | strip_banner)
    got=$("$BIN" run "$DRIVER" "$f" 2>/dev/null)
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
