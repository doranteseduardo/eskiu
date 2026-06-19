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
#   --full    -> the real corpus tests/*.esk. The self-hosted parser now follows
#                `import` (resolving <stdlib> + relative paths, recursively), so the
#                only files it can't match are those whose transitive import closure
#                touches a stream-rewriting preprocessor directive / built-in macro /
#                shebang — the preprocessor isn't self-hosted yet, so the C++ token
#                stream would differ. A file is excluded iff ANY file in its closure
#                (itself + everything it imports) has such a directive.
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

# A file's import closure is "dirty" if it (or any file it transitively imports) uses
# a stream-rewriting preprocessor directive / built-in macro / shebang — the
# self-hosted lexer doesn't preprocess, so the C++ token stream would differ.
PP_RE='(^[[:space:]]*#[[:space:]]*(define|undef|ifdef|ifndef|if|elif|else|endif|include)\b)|(^#!)|__LINE__|__FILE__'

# Resolve an `import` line to a path, mirroring the parser: bare `<name>` ->
# stdlib/name.esk, `<a/b>` -> a/b, relative `"p"` -> <dir>/p, absolute kept.
re_std='import[[:space:]]*<([^>]+)>'
re_rel='import[[:space:]]*"([^"]+)"'

# closure_dirty FILE -> exit 0 if FILE's transitive import closure touches the
# preprocessor, else exit 1. Plain string "seen" set for bash 3.2 portability.
closure_dirty() {
    local seen=" " queue="$1" f dir line resolved name p
    while [ -n "$queue" ]; do
        f="${queue%%$'\n'*}"
        if [ "$f" = "$queue" ]; then queue=""; else queue="${queue#*$'\n'}"; fi
        case "$seen" in *" $f "*) continue ;; esac
        seen="$seen$f "
        [ -f "$f" ] || continue
        grep -qE "$PP_RE" "$f" && return 0
        dir=$(dirname "$f")
        while IFS= read -r line; do
            resolved=""
            if [[ "$line" =~ $re_std ]]; then
                name="${BASH_REMATCH[1]}"
                case "$name" in */*) resolved="$name" ;; *) resolved="stdlib/${name}.esk" ;; esac
            elif [[ "$line" =~ $re_rel ]]; then
                p="${BASH_REMATCH[1]}"
                case "$p" in /*) resolved="$p" ;; *) resolved="$dir/$p" ;; esac
            fi
            [ -n "$resolved" ] && queue="$queue"$'\n'"$resolved"
        done < <(grep -E '^[[:space:]]*import\b' "$f")
    done
    return 1
}

if [ "$#" -eq 1 ] && [ "$1" = "--full" ]; then
    files=()
    excluded=()
    for f in tests/*.esk; do
        if closure_dirty "$f"; then excluded+=("$f"); else files+=("$f"); fi
    done
    echo "corpus: ${#files[@]} files (excluded ${#excluded[@]} with preprocessor in import closure)"
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
