#!/usr/bin/env bash
# Parity gate for the self-hosted compiler's editor modes vs the C++ eskiuc.
#
# Every `// DEF L:C ...`, `// HOVER L:C ...` and `// COMPLETE L:C ...` query of
# tests/lsp/*.esk runs through --definition-at / --hover-at / --complete-at with both
# compilers, and --symbols runs over a few files; stdout must be byte-identical (the
# whole completion list, kinds and details included). The error paths (a parse error, a
# semantic error, nothing at the cursor) are compared too.
#
#   ESKIUC      the C++ compiler     (default: build/eskiuc)
#   ESKIUC_ESK  the self-hosted one  (default: build/eskiuc-esk)

set -u
cd "$(dirname "$0")/../.." || exit 2

CXX="${ESKIUC:-build/eskiuc}"
ESK="${ESKIUC_ESK:-build/eskiuc-esk}"
for b in "$CXX" "$ESK"; do
    [ -x "$b" ] || { echo "lsp_parity: cannot find $b (set ESKIUC / ESKIUC_ESK)"; exit 2; }
done

pass=0; fail=0
same() {   # same LABEL ARGS...: compare both compilers' stdout
    local label="$1"; shift
    local ref got
    ref="$("$CXX" "$@" 2>/dev/null)"
    got="$("$ESK" "$@" 2>/dev/null)"
    if [ "$ref" == "$got" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL  $label"
        diff <(echo "$ref") <(echo "$got") | head -20 | sed 's/^/    /'
    fi
}

for esk in tests/lsp/*.esk; do
    [ -e "$esk" ] || continue
    while read -r q _; do same "$esk --definition-at $q" "$esk" --definition-at "$q"
    done < <(grep '^// DEF ' "$esk" | sed 's#^// DEF ##')
    while read -r q _; do same "$esk --hover-at $q" "$esk" --hover-at "$q"
    done < <(grep '^// HOVER ' "$esk" | sed 's#^// HOVER ##')
    while read -r q _; do same "$esk --complete-at $q" "$esk" --complete-at "$q"
    done < <(grep '^// COMPLETE ' "$esk" | sed 's#^// COMPLETE ##')
    same "$esk --symbols" "$esk" --symbols
done
for esk in tests/warnings/wall_uses.esk tests/operators.esk tests/enum_match.esk tests/interface_values.esk; do
    [ -e "$esk" ] && same "$esk --symbols" "$esk" --symbols
done

# The error paths: a parse error, a semantic error, and no symbol at the cursor.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "int f( {" > "$tmp/parse.esk"
echo "int f() { return undefined_var; }" > "$tmp/sema.esk"
same "parse error hover" "$tmp/parse.esk" --hover-at 1:1
same "parse error definition" "$tmp/parse.esk" --definition-at 1:1
same "parse error symbols" "$tmp/parse.esk" --symbols
same "semantic error hover" "$tmp/sema.esk" --hover-at 1:18
same "semantic error definition" "$tmp/sema.esk" --definition-at 1:18
same "no symbol hover" tests/lsp/scoped_defs.esk --hover-at 1:1
same "no symbol definition" tests/lsp/scoped_defs.esk --definition-at 1:1

echo "----"
echo "lsp parity: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
