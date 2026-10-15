# Eskiu 0.11.0

A minor release about tooling and stability. Eskiu now has a REPL, a minimal package
manager, and editor support (completion, signatures, quick fixes) that comes from the
compiler itself, in VS Code-based editors and JetBrains IDEs. Diagnostics suggest the name
you meant, `deprecated` marks a function on its way out, and a written policy says what
can change between releases.

**Breaking:** the stdlib names renamed in 0.9.2 and 0.10.1 (`rng_seed`, `args_flag`,
`future_poll`, `sha256`, `str_join` and the rest) are removed. The
[migration guide](https://eskiu-lang.org/docs/migration.html) maps each one to its new
name, and the compiler now suggests it when it is one or two letters away.

---

## Install

**macOS (Apple Silicon) and Linux (x86-64, arm64)**

```bash
curl -fsSL https://eskiu-lang.org/install.sh | sh
eskiuc --version
```

Or download a tarball below and unpack it into a prefix (`tar -xzf eskiuc-<platform>.tar.gz -C /usr/local`).

**Windows (x86-64, experimental)**

```powershell
Expand-Archive eskiuc-windows-x86_64.zip -DestinationPath C:\eskiu   # then add C:\eskiu\bin to PATH
eskiuc --version
```

Or build from source (LLVM 21 or newer, LLVM 22 recommended, and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## Added

- **`eskiuc repl`.** Try code line by line over the LLVM ORC JIT. An expression prints its
  value by type (numbers, strings, structs field by field, enums, arrays, `List`, `String`);
  functions, types and imports stay for later inputs, and declaring one again replaces it.
  `await` works at the prompt. An unfinished input continues on the next line in any brace
  style (an open bracket or string, a trailing operator, a header whose `{` comes next), and
  an `if` or `try` waits one line for its `else`, `catch` or `finally`. An input with an
  error is discarded and the session goes on. `:type`, `:load`, `:reset`, `:help`, `:quit`.
- **Packages.** `eskiuc new NAME` creates a package (`eskiu.toml` and `src/main.esk`).
  `eskiuc add NAME --git URL [--rev R]` or `--path DIR` adds a dependency, `eskiuc build`,
  `run` and `test` build, run and test it, and `eskiuc fetch` updates the lock. Git
  dependencies are cloned into `~/.eskiu/pkgs` and pinned to a commit in `eskiu.lock`;
  dependencies of dependencies are resolved too. Modules are imported as
  `import <pkg/module>;`. There is no registry yet.
- **Editor support.** New compiler modes (`--complete-at`, `--symbols`, signatures in
  `--hover-at`, methods and fields in `--definition-at`) feed the VS Code extension 0.11.0
  (also Cursor, Antigravity and VSCodium: completion after `.`, signature help, the outline,
  a quick fix for misspelled names) and the IntelliJ plugin, which now runs the same
  language server through LSP4IJ. `editor/vscode/server.js` also serves Neovim, Helix and
  Zed.
- **`deprecated("use NEW")`** before a function: each use warns with the message, except in
  imported modules; `-Wno-deprecated` turns it off.
- **"Did you mean"**: an unknown variable, function, type, field or method suggests the
  closest name in scope (`undefined variable 'totl' (did you mean 'total'?)`), in both
  compilers.
- **Stability policy** (`docs/lang/stability.md`): from 0.11 a renamed function keeps its old
  name, marked deprecated, for one minor release.
- **`examples/scripts/todo.esk`**, a to-do list kept in a JSON file.

## Changed

- The docs are one tree that the site publishes, now including the migration guide, the
  stability policy and the roadmap. The changelog page shows each release's highlights with
  the full entry collapsed.
- CI runs the REPL and package checks on Windows, and a new workflow packages the editor
  extensions on each release.

The full list of changes is in [CHANGELOG.md](CHANGELOG.md).
