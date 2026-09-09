# Eskiu 0.9.0

A small release that fills a control-flow gap and fixes a global-initializer bug. Loops can
now carry a label so `break` and `continue` can target an outer loop, and global array
initializers keep their values instead of being zeroed. Existing code keeps compiling
unchanged.

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

- **Labeled `break` and `continue`.** A loop can be named with a leading label, and
  `break label` / `continue label` act on that loop from inside a nested one:

  ```eskiu
  outer: for (int i = 0; i < rows; i = i + 1) {
      for (int j = 0; j < cols; j = j + 1) {
          if (grid[i][j] == target) { break outer; }
      }
  }
  ```

  The label must name an enclosing `while`, `do`/`while`, `for`, or `for ... in` loop,
  otherwise the program is rejected at compile time. A labeled jump runs the same
  `defer`/`errdefer` cleanups as an unlabeled one (every deferred statement between the jump
  and the target loop runs, innermost first), may not escape a `defer` body, and is not
  supported inside an `async fn`. Eskiu has no `goto`; this closes the one case where the
  cleanup-driven `defer` and early-return patterns could not express an outer-loop exit
  directly. Landed in lockstep across both compilers.

## Fixed

- **Global array initializers are no longer dropped.** A global (or `static` local) array
  literal such as `int[3] G = {10, 20, 30};` was silently zero-filled: only scalar globals
  kept their value. The constant folder now builds the array from the initializer, with
  C-style zero-fill for a partial list (`int[3] = {7}` gives `{7, 0, 0}`) and support for
  nested arrays. Fixed in both compilers; locals were never affected.

See the full log in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. No breaking language or standard-library changes; recompiling picks up the fix, and
labeled `break`/`continue` is additive.
