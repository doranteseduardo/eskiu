# Eskiu 0.6.0

A memory-safety and standard-library release, in the Zig spirit: compile-time checks and
opt-in runtime guards rather than a borrow checker, so C-faithful code keeps compiling
unchanged while new tools make it safer when you want them. The language features landed
in lockstep across the C++ `eskiuc` and the self-hosted compiler; the two runtime-guard
features (`--safe`, `?*T`) ship in the C++ compiler with self-host mirrors on the
promotion track.

---

## Install

**macOS (Apple Silicon)**

```bash
tar -xzf eskiuc-macos-arm64.tar.gz -C /usr/local
eskiuc --version
```

**Linux (x86-64)**

```bash
tar -xzf eskiuc-linux-x86_64.tar.gz -C /usr/local
eskiuc --version
```

Or build from source (LLVM 17+ and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## What's new

### Memory safety

- **`defer` and `errdefer`.** `defer stmt;` runs a statement (or block) when the enclosing
  block is left, in LIFO order, on *every* path out: fall-through, `return`, `break`,
  `continue`, and `?`-propagation. It is the ergonomic way to pair an acquisition with its
  release without leaking on early exits. `errdefer` is the error-only variant, running
  only when the function leaves through a propagated `?` (for undoing partial work when a
  later step fails). Implementing this also fixed a latent bug where `finally` was skipped
  on an early return from inside a `try`.
- **Slice type `T[]`.** A fat pointer (data + length) that views a contiguous run of `T`.
  Build one by slicing a fixed array with a half-open range (`a[lo..hi]`); it supports
  `s[i]`, `s.len`, `for (x in s)`, and passing by value. Because the length travels with
  the slice, a function taking `T[]` needs no separate count argument.
- **`must_use` qualifier.** Prefixing a function with `must_use` makes discarding its
  result a compile error, catching leaked allocations and dropped return values. The
  stdlib's `alloc` is now `must_use`, so a bare `alloc<T>(n);` is rejected.
- **`--safe` build mode.** A new flag that inserts runtime bounds checks on slice and
  array indexing; an out-of-range index traps instead of reading or writing past the end.
  Off by default, so release builds are unaffected.
- **Checked nullable pointers `?*T`.** Opt-in null safety: a `?*T` cannot be dereferenced,
  indexed, or member-accessed until it is proven non-null, and `if (x != null) { ... }`
  narrows it in that branch. A `*T` widens to `?*T`; the reverse needs a check. Bare `*T`
  stays nullable (C-faithful), so existing code is unaffected, and `?*T` has the same
  representation as a bare pointer, so the safety is entirely at compile time.

### Standard library

- **`<random>`.** A seedable xoshiro256\*\* generator with no global state: `rng_seed`,
  `rng_next`, `rng_below` / `rng_range` (unbiased bounded integers), `rng_double`,
  `rng_bool`, `rng_fill`. Not cryptographically secure.
- **`<regex>`.** A regular-expression engine built as a Thompson NFA (Pike VM), so
  matching is linear in the input with no catastrophic backtracking. Supports `.`, classes
  `[a-z]` / `[^...]`, `\d \w \s`, quantifiers `* + ? {m,n}` (greedy or lazy), alternation,
  capturing groups, and the anchors `^ $`. Use `regex_match` for a quick yes/no, or
  `regex_compile` + `regex_search` + `match_group` to extract captures.
- **`<sort>`.** Generic in-place `sort<T>` (heapsort, a guaranteed O(n log n) worst case)
  and `bsearch<T>` over a `*T` array, driven by a `cmp(&x, &y)` function.
- **`<url>`.** RFC 3986 percent-encoding (`url_encode` / `url_decode`) and form-query
  lookup (`url_query_get`).
- **`<uuid>`.** RFC 4122 version-4 UUIDs (`uuid_v4`), built on `<random>`.
- **UTC civil calendar in `<time>`.** `DateTime` plus `time_to_utc` / `time_from_utc` and
  ISO 8601 formatting (`time_format_iso`).

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Existing code compiles unchanged. Every safety feature is opt-in: bare `*T` stays
nullable, `--safe` is off by default, and `defer` / `errdefer` / `must_use` / `T[]` are
new surface you reach for deliberately. The one thing to know is that `must_use` is now a
keyword, so a variable or field named `must_use` must be renamed.
