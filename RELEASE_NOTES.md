# Eskiu 0.10.1

A patch release for the scripting standard library of 0.10.0. It fixes what an audit of the
new modules and a round of fuzzing found, makes the whole library work on Windows, cleans up
names that did not follow the stdlib conventions, and adds macOS and leak checks to CI.
Programs written for 0.10.0 still compile: every renamed function keeps its old name as a
deprecated wrapper.

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

## Fixed

- **`<fetch>`**: a TLS connection that the server drops without `close_notify` is an error
  instead of a silently truncated body. `fetch_async` resolves the host on its own thread,
  so a slow DNS answer no longer blocks the event loop. A CR or LF in `user_agent` or in
  `o.headers` is rejected instead of being sent as an extra header line, a non-chunked
  transfer coding is read until the connection closes, and a redirect that cannot be followed
  reports its own error.
- **`fs_remove_all` and `fs_walk`** walk a tree through directory descriptors (handles on
  Windows), so a directory swapped for a symbolic link or junction while they run is never
  entered, and trees deeper than `PATH_MAX` (or `MAX_PATH` on Windows) are handled whole.
  Before, `fs_remove_all` could follow such a link and delete outside the tree.
- **`fs_copy`** keeps the copy owner-only until it is complete, so a private file is never
  readable by others while it is copied. It also refuses to copy a file onto itself.
- **`<process>`** writes a child's stdin without changing the process-wide `SIGPIPE`
  setting, which two threads running processes at once could break. A command that exits in
  time while something it started keeps the output pipe open reports its own exit code, not a
  timeout. A child with a timeout can read an inherited terminal.
- **Windows**: HTTPS trusts the system certificate store, `printf`-style formatting matches
  other platforms, `<executor>` links and wakes its loop, `eskiuc run` runs its program, and
  plain `http://` fetches no longer crash. The Windows CI now runs every 0.10 module test and
  the example scripts.
- **Smaller fixes**: `dns_resolve` keeps the zone of a link-local IPv6 address, `String_concat`
  and `String_split` keep NUL bytes, `<dotenv>` handles CRLF inside quotes, `<args>` no longer
  reads past an empty argument, `time_parse` rejects an out-of-range `%s`.
- **Compilers**: a lambda assigned to a local after its declaration no longer leaks its
  environment, an `extern` named like a generic function is a clear error (the C++ compiler
  crashed), and on macOS the linker no longer warns about the object's OS version.

## Changed

- **Names.** Functions that take a module's struct follow the `Type_method` convention, so
  they work as dot-calls: `ArgParser_flag` (was `args_flag`), `Option_is_some`,
  `Future_poll`, `HpackDecoder_decode` and others. One-shot functions take their module's
  prefix (`hash_sha256`, `time_sleep_ms`, `String_format`), and `<sort>`'s binary search is
  `sort_search`, so it no longer clashes with the C library's `bsearch`. The old names still
  work and are deprecated. Internal helpers are now underscore-prefixed, so they no longer
  clash with a program's own names such as `Node` or `Range`.
- **No more `extern` for the basics.** `<mem>` declares `strcmp`, `strncmp`, `strchr` and
  `strstr`, and the examples, docs and the book use `import <io>` instead of declaring
  `printf`. A program that declares one of these four with different types now gets a
  "conflicting declaration" error; remove its own `extern`.
- **CI** runs the test suite on macOS arm64, a leak-checking job, and a manual long fuzzing
  run of the stdlib parsers.

The full list of changes is in [CHANGELOG.md](CHANGELOG.md).
