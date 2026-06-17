#!/usr/bin/env bash
# Parity gate for the self-hosted lexer (selfhost/lexer.esk) vs the C++ lexer.
#
# The Eskiu driver is compiled once to a native binary, then run per file. For
# each corpus file we run BOTH lexers. The Eskiu driver emits token lines in
# the EXACT format of the C++ `--test-lexer` (same padding, same decoded values),
# so we just strip the C++ banner/separators/total and raw-`diff` the token
# streams. Byte-identical = parity (embedded newlines in string values, decoded
# identically on both sides, match too — no fragile re-parsing of the output).
#
# Usage: tests/selfhost/lex_parity.sh [file.esk ...]
#   no args   -> the synthetic corpus under tests/selfhost/inputs/
#   --full    -> the whole real corpus tests/*.esk, minus the preprocessor-
#                dependent files (the C++ lexer runs preprocess() first, so files
#                that #define/#ifdef/#include, use __LINE__/__FILE__, or carry a
#                shebang would diverge — #pragma passes through and IS compared).
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

# Compile the Eskiu driver ONCE to a native binary and run that per file — far
# faster than `eskiuc run DRIVER FILE` (which recompiles the driver every time),
# which is what makes the full corpus viable in CI.
LEXBIN="$(mktemp -t lex_main.XXXXXX)"
trap 'rm -f "$LEXBIN"' EXIT
if ! "$BIN" "$DRIVER" -o "$LEXBIN" 2>/tmp/lexbuild.$$; then
    echo "lex_parity: failed to build $DRIVER"; cat /tmp/lexbuild.$$; rm -f /tmp/lexbuild.$$; exit 2
fi
rm -f /tmp/lexbuild.$$

# The C++ --test-lexer wraps the token lines in a banner; strip those three
# format-only lines (anchored at column 0 — real token lines start with "  Line"
# and value-continuation lines start with value content, so neither is touched).
strip_banner() {
    sed -E '/^Tokenizing: /d; /^=+$/d; /^Total tokens: /d'
}

# A file is preprocessor-dependent (excluded from --full) if a directive that
# rewrites the token stream appears, or a built-in macro is used. #pragma is NOT
# excluded — it survives preprocessing and lexes identically on both sides.
PP_RE='(^[[:space:]]*#[[:space:]]*(define|undef|ifdef|ifndef|if|elif|else|endif|include)\b)|(^#!)|__LINE__|__FILE__'

if [ "$#" -eq 1 ] && [ "$1" = "--full" ]; then
    files=()
    excluded=()
    for f in tests/*.esk; do
        if grep -qE "$PP_RE" "$f"; then excluded+=("$f"); else files+=("$f"); fi
    done
    echo "corpus: ${#files[@]} files (excluded ${#excluded[@]} preprocessor-dependent)"
    for e in "${excluded[@]}"; do echo "  skip  ${e#tests/}  (preprocessor)"; done
    echo "----"
elif [ "$#" -gt 0 ]; then
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
    got=$("$LEXBIN" "$f" 2>/dev/null)
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
