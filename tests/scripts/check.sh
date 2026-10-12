#!/usr/bin/env bash
#
# Runs the example scripts in examples/scripts/ the way a user does, through their
# `#!/usr/bin/env -S eskiuc run` line, and compares what they print with
# tests/scripts/NAME.expected. No network: repo_stats talks to a local fake of the
# GitHub API (fake_github.esk, started here), organize works on a temporary tree
# whose file times are set with `touch -t` and read in TZ=UTC.
#
# Each transcript holds a command's stdout, then its stderr with the log
# timestamps and the temporary path replaced, then its exit code.
#
# Usage: tests/scripts/check.sh                       (the C++ build/eskiuc)
#        ESKIUC=build/eskiuc-esk tests/scripts/check.sh  (the self-hosted compiler)
#        UPDATE=1 tests/scripts/check.sh                 (rewrite the .expected files)
# Prints "ok NAME" or "FAIL NAME" plus a diff per script; exits 1 on any failure.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
ESKIUC="${ESKIUC:-$root/build/eskiuc}"
case "$ESKIUC" in /*) ;; *) ESKIUC="$PWD/$ESKIUC" ;; esac
[[ -x "$ESKIUC" ]] || { echo "FAIL scripts: compiler not found at $ESKIUC"; exit 2; }
export ESKIU_ROOT="$root"
export TZ=UTC
unset NO_COLOR FORCE_COLOR LOG_LEVEL GITHUB_TOKEN COLUMNS

work="$(mktemp -d)"
server_pid=""
cleanup() {
    [[ -n "$server_pid" ]] && kill "$server_pid" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT

# The shebang names `eskiuc`: put the compiler under test first on PATH by that name.
mkdir -p "$work/bin"
ln -s "$ESKIUC" "$work/bin/eskiuc"
export PATH="$work/bin:$PATH"

fail=0
# run LABEL CMD...: append the command's transcript to $work/out.
run() {
    local label="$1"; shift
    echo "\$ $label" >> "$work/out"
    "$@" > "$work/stdout" 2> "$work/stderr"
    local code=$?
    cat "$work/stdout" >> "$work/out"
    sed -E -e 's/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+Z //' -e "s|$work|TMP|g" \
        -e '/disk usage: /d' "$work/stderr" | sed 's/^/stderr: /' >> "$work/out"
    echo "exit $code" >> "$work/out"
}
compare() {
    local name="$1"
    [[ -n "${UPDATE:-}" ]] && cp "$work/out" "$here/$name.expected"
    if diff -u "$here/$name.expected" "$work/out" > "$work/diff"; then
        echo "ok $name"
    else
        echo "FAIL $name"
        sed 's/^/        /' "$work/diff"
        fail=1
    fi
    rm -f "$work/out"
}

# ---- repo_stats: against the fake API --------------------------------------
port=18462
if "$ESKIUC" "$here/fake_github.esk" -o "$work/fake_github" > "$work/build.log" 2>&1; then
    "$work/fake_github" "$port" "$work/ready" &
    server_pid=$!
    for _ in $(seq 100); do [[ -f "$work/ready" ]] && break; sleep 0.1; done
    script="$root/examples/scripts/repo_stats.esk"
    api="http://127.0.0.1:$port"
    run "repo_stats eskiu-lang/eskiu" "$script" --api-url "$api" eskiu-lang/eskiu
    run "repo_stats --top 2 eskiu-lang/eskiu" "$script" --api-url "$api" --top 2 eskiu-lang/eskiu
    run "FORCE_COLOR=1 repo_stats --top 1" env FORCE_COLOR=1 "$script" --api-url "$api" --top 1 eskiu-lang/eskiu
    run "repo_stats --json eskiu-lang/eskiu" "$script" --api-url "$api" --json eskiu-lang/eskiu
    run "repo_stats eskiu-lang/missing" "$script" --api-url "$api" eskiu-lang/missing
    run "repo_stats eskiu-lang/broken" "$script" --api-url "$api" eskiu-lang/broken
    run "repo_stats no-slash" "$script" --api-url "$api" no-slash
    run "repo_stats --timeout 0 a/b" "$script" --timeout 0 a/b
    run "repo_stats (no repo)" "$script"
    run "repo_stats (connection refused)" "$script" --api-url http://127.0.0.1:1 a/b
    compare repo_stats
    kill "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null
    server_pid=""
else
    echo "FAIL repo_stats (fake_github.esk did not build)"
    sed 's/^/        /' "$work/build.log" | head -5
    fail=1
fi

# ---- organize: on a temporary tree -----------------------------------------
tree="$work/downloads"
make_tree() {
    rm -rf "$tree"
    mkdir -p "$tree/photos" "$tree/txt"
    printf 'hello\n' > "$tree/notes.txt"
    printf 'HELLO WORLD\n' > "$tree/README.TXT"
    printf '%%PDF-1.4 fake\n' > "$tree/report.pdf"
    printf 'jpegdata' > "$tree/photos/cat.jpg"
    printf 'jpegdata' > "$tree/cat-copy.jpg"
    printf 'name,size\n"a, b",2\n' > "$tree/data.csv"
    printf 'all:\n' > "$tree/Makefile"
    printf 'hidden' > "$tree/.hidden"
    printf 'same' > "$tree/one.log"
    printf 'same' > "$tree/two.log"
    printf 'older notes' > "$tree/txt/todo.txt"
    printf 'older notes' > "$tree/todo.txt"
    printf 'old' > "$tree/txt/draft.txt"
    printf 'new' > "$tree/draft.txt"
    touch -t 202609150930 "$tree/notes.txt" "$tree/README.TXT" "$tree/one.log" "$tree/two.log" "$tree/draft.txt"
    touch -t 202610010800 "$tree/report.pdf" "$tree/cat-copy.jpg" "$tree/data.csv" "$tree/Makefile" "$tree/todo.txt"
}
listing() { (cd "$tree" && find . -type f | LC_ALL=C sort) >> "$work/out"; }
script="$root/examples/scripts/organize.esk"
make_tree
run "organize --dry-run" "$script" --dry-run "$tree"
listing
run "organize --report report.csv" "$script" --report "$work/report.csv" "$tree"
listing
cat "$work/report.csv" >> "$work/out"
run "organize (again)" "$script" "$tree"
make_tree
run "organize --by date" "$script" --by date "$tree"
listing
run "organize --by size" "$script" --by size "$tree"
run "organize (missing dir)" "$script" "$work/nope"
compare organize

exit "$fail"
