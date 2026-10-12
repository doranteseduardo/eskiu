# Eskiu 0.10.0

A release about scripting. Eskiu could already run a file like a script (`eskiuc run
file.esk`, or a `#!/usr/bin/env eskiuc run` shebang); now the standard library covers what
a script usually does: fetch a URL, walk a directory, run a command, parse arguments, read
JSON or CSV, print colored output. None of it needs an `extern` or a package.

```eskiu
import <fetch>;
import <json>;
import <io>;

int main() {
    let r: Response;
    fetch("https://api.github.com/repos/llvm/llvm-project", &r);
    defer Response_free(&r);
    *JsonValue repo = Response_json(&r);
    println(JsonValue_as_cstr(json_get(repo, "owner.login")));
    return 0;
}
```

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

## New in the standard library

- **`<fetch>`**: an HTTP/1.1 client for `http://` and `https://`. `fetch(url, &r)` for the
  simple case, `fetch_with` for the method, headers, a text, bytes or JSON body, a timeout,
  redirects and a body size cap, and `fetch_async` returning a future on the event loop.
  A `Response` gives the status, headers, text, bytes or parsed JSON. HTTPS verifies
  certificates against the system store and loads OpenSSL (libssl 3 or 1.1) when the first
  `https://` request runs, so a program links and runs `http://` requests without it.
- **Files and directories** (`<fs>`): read and write text in one call, read lines, `stat`,
  create and remove directory trees, copy, rename, list a directory, walk a tree with a
  callback, glob with `*`, `?`, `[...]` and `**`, temporary files and directories, the
  current directory. Paths are UTF-8 on every platform, Windows included.
- **Processes** (`<process>`): `process_run` and `sh` capture stdout, stderr and the exit
  code, with stdin text, a working directory, extra environment variables and a timeout;
  `process_spawn` streams to and from a running child. `<env>` gains `env_set`,
  `env_unset` and `env_list`.
- **Networking**: `<dns>` resolves host names (IPv4 and IPv6), and `net_tcp_connect` now
  takes a host name or an IPv6 address. `<url>` parses and resolves URLs.
- **Strings**: find, contains, replace, split on a string, lines, join, lower and upper case,
  repeat, padding, parsing and printing doubles, and `str_format` for printf-style text into
  a `String`.
- **Collections**: `List` gains map, filter, reduce, find, any, all, contains, index_of,
  reverse, slice, unique, pop, insert and sort_by; `Map` and `HashMap` can be iterated and
  entries removed; a new `Set<T>`.
- **Data formats**: JSON documents can be built, edited and printed (compact or indented)
  and read by path (`json_get(v, "data.items[0].name")`); `<csv>` reads and writes RFC 4180
  CSV; `<dotenv>` loads `.env` files; `<hash>` has SHA-256, SHA-1, MD5 and HMAC-SHA256
  written in Eskiu, with hex encoding and a constant-time compare.
- **Command-line tools**: `<args>` parses flags, options and positionals and prints a
  generated `--help`; `<term>` prints colors and styles and honors `NO_COLOR`,
  `FORCE_COLOR` and whether the output is a terminal; `<log>` has levels, timestamps and
  `LOG_LEVEL`; `<io>` has `print`, `println`, `input` and `read_line`; `<time>` formats and
  parses dates with strftime-style specifiers, in UTC or local time, and has a `Stopwatch`.

Examples for each area are under `examples/`, and two longer scripts that combine them are in
`examples/scripts/`. The Book has a new chapter, "Scripting with Eskiu".

## Fixed

- **C ABI on AArch64 Linux and Windows x64.** An `extern`'s narrow integer parameters and
  results (`int8`, `int16`, `uint8`, `uint16`, `char`, and `bool` on AArch64) were declared
  as extended by the caller or callee, which clang does not do on these targets. A narrow
  value returned by a clang-compiled C function could be read with garbage in its upper
  bits. Code compiled with gcc happened to extend them, which hid the bug.
- **`--freestanding`** never introduces calls to `memset`, `memcpy` or other C library
  functions, so a freestanding program links at `-O2`; and an escaping closure's
  environment uses the program's `esk_alloc`/`esk_free` instead of libc's `malloc`/`free`.
- **Self-hosted compiler**: a dot-call on a generic instance reaches a non-generic function
  named after the instance (`List_string_count`), and the driver accepts `--mattr` and
  `--reloc`.
- **Editor tooling**: `--hover-at` and `--definition-at` report a missing symbol, a semantic
  error and an internal error separately.

## Changed

- **Editors.** The VS Code extension checks errors as you type, formats with `eskiuc fmt`,
  runs the current file and has a setting for the compiler path; it works in Cursor,
  Antigravity and VSCodium too. The IntelliJ plugin builds against current platforms.
- **The bare-metal kernel** in `kernel/` now handles CPU exceptions, takes timer interrupts
  through the GIC, runs cooperative and preemptive tasks, and has a shell over the UART. CI
  boots it in QEMU at `-O0` and `-O2`.
- **Linking with gcc on x86-64 Linux.** Since GCC 12, gcc passes a struct with a zero-width
  bitfield (`uint32 : 0;`) by value differently from clang. Eskiu follows clang, so such a
  struct crossing an `extern` matches C compiled by clang.

The full list of changes is in [CHANGELOG.md](CHANGELOG.md).
