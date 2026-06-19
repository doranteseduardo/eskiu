#!/usr/bin/env bash
# Parity gate for the self-hosted parser (selfhost/parser.esk) vs the C++ parser.
#
# The Eskiu driver is compiled once to a native binary, then run per file. For each
# file we run BOTH parsers, strip the C++ `--test-parser` banner, and raw-`diff` the
# AST dumps (the Eskiu driver prints the dump in the exact ast_printer.cpp format).
# Byte-identical = parity.
#
# Usage: tests/selfhost/parse_parity.sh [file.esk ...]
#   no args   -> the synthetic corpus under tests/selfhost/parse_inputs/
#   --full    -> the real corpus tests/*.esk, minus files the single-file parser
#                can't match: any that `import` (the C++ parser follows imports and
#                merges their decls) or that use stream-rewriting preprocessor
#                directives / built-in macros / a shebang.
# Green (exit 0) = byte-identical AST dumps for every file.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "parse_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi

DRIVER=selfhost/parse_main.esk

# Compile the Eskiu driver ONCE to a native binary and run that per file — far
# faster than `eskiuc run DRIVER FILE` (which recompiles the driver every time),
# which is what makes the full corpus viable in CI.
PBIN="$(mktemp -t parse_main.XXXXXX)"
trap 'rm -f "$PBIN"' EXIT
if ! "$BIN" "$DRIVER" -o "$PBIN" >/dev/null 2>/tmp/parsebuild.$$; then
    echo "parse_parity: failed to build $DRIVER"; cat /tmp/parsebuild.$$; rm -f /tmp/parsebuild.$$; exit 2
fi
rm -f /tmp/parsebuild.$$

# The C++ --test-parser wraps the AST dump in a banner; strip those format-only
# lines (anchored at column 0 — AST lines are indented, so they're untouched).
strip_banner() {
    sed -E '/^Parsing: /d; /^=+$/d; /^Parse succeeded!$/d'
}

# Excluded from --full: imports (C++ parser follows them) or stream-rewriting
# preprocessor directives / built-in macros / shebang (change tokens before parse).
EXCL_RE='(^[[:space:]]*import\b)|(^[[:space:]]*#[[:space:]]*(define|undef|ifdef|ifndef|if|elif|else|endif|include)\b)|(^#!)|__LINE__|__FILE__'

if [ "$#" -eq 1 ] && [ "$1" = "--full" ]; then
    files=()
    excluded=()
    for f in tests/*.esk; do
        if grep -qE "$EXCL_RE" "$f"; then excluded+=("$f"); else files+=("$f"); fi
    done
    echo "corpus: ${#files[@]} files (excluded ${#excluded[@]} import/preprocessor-dependent)"
    echo "----"
elif [ "$#" -gt 0 ]; then
    files=("$@")
else
    files=(tests/selfhost/parse_inputs/*.esk)
fi

fail=0
total=0
skipped=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "MISS  $f (no such file)"; fail=1; continue; }
    # The reference oracle itself must parse cleanly. The C++ ASTPrinter crashes on
    # a body-less FunctionDecl (top-level prototype), so skip files where
    # --test-parser doesn't reach "Parse succeeded!" — they can't be compared.
    rawref=$("$BIN" --test-parser "$f" 2>/dev/null)
    case "$rawref" in
        *"Parse succeeded!"*) ;;
        *) echo "skip  ${f#tests/}  (C++ --test-parser did not succeed)"; skipped=$((skipped + 1)); continue ;;
    esac
    total=$((total + 1))
    ref=$(printf '%s' "$rawref" | strip_banner)
    got=$("$PBIN" "$f" 2>/dev/null)
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
