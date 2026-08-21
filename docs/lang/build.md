# Building Eskiu from Source

## Requirements

| Dependency   | Minimum                                | Tested      |
| ------------ | -------------------------------------- | ----------- |
| LLVM         | 17                                     | 22.x        |
| CMake        | 3.20                                   | 3.x         |
| C++ compiler | C++17 (clang++ recommended, g++ works) | clang++ 17+ |
| git          | any                                    | n/a         |

LLVM is the only non-trivial dependency. The compiler uses the `support`, `core`, and `irreader` component libraries.

---

## macOS

### Install dependencies

```bash
brew install llvm cmake
```

Homebrew installs LLVM into a versioned prefix (e.g. `/opt/homebrew/opt/llvm`) and does not add it to `PATH` by default. Export the necessary paths before building:

```bash
export PATH="$(brew --prefix llvm)/bin:$PATH"
export LLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
```

Add these to your shell profile (`~/.zshrc` or `~/.bash_profile`) to make them permanent.

### Build

```bash
git clone https://github.com/doranteseduardo/eskiu.git
cd eskiu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(sysctl -n hw.logicalcpu)
```

Expected output (last few lines):

```
[ 85%] Building CXX object CMakeFiles/eskiuc.dir/codegen/codegen_expr.cpp.o
[100%] Linking CXX executable eskiuc
[100%] Built target eskiuc
```

The compiler binary is at `build/eskiuc`.

---

## Linux (Ubuntu / Debian)

### Install dependencies

```bash
sudo apt-get update
sudo apt-get install -y llvm-17-dev clang-17 cmake git
```

For Ubuntu 22.04 or older, the LLVM 17 packages are in the LLVM apt repository:

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 17
sudo apt-get install -y llvm-17-dev
```

### Build

```bash
git clone https://github.com/doranteseduardo/eskiu.git
cd eskiu
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \
  -DCMAKE_CXX_COMPILER=clang++-17
cmake --build build -- -j$(nproc)
```

---

## Linux (Alpine)

### Install dependencies

```bash
apk add llvm17-dev cmake clang17 git make
```

### Build

```bash
git clone https://github.com/doranteseduardo/eskiu.git
cd eskiu
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm17/lib/cmake/llvm \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build -- -j$(nproc)
```

---

## Build Options

### Debug vs Release

```bash
# Release: optimized binary, suitable for benchmarks
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Debug: unoptimized, assertions enabled, better stack traces
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

### Incremental builds

After changing source files, just re-run the build step; CMake tracks dependencies:

```bash
cmake --build build -- -j$(nproc)
```

### Clean rebuild

Delete the build directory and start over:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

---

## Verify the Build

### Version check

```bash
./build/eskiuc --version
```

Expected output:

```
Eskiu 0.8.0 (LLVM 22.x.x)
```

The LLVM version will reflect whichever version is installed on the host.

### Smoke test

```bash
./build/eskiuc examples/hello.esk --test-codegen
```

Expected output (LLVM IR for `hello.esk`):

```llvm
; ModuleID = 'eskiu_module'
source_filename = "eskiu_module"

@0 = private unnamed_addr constant [19 x i8] c"Hello from Eskiu!\0A\00"
@1 = private unnamed_addr constant [12 x i8] c"Result: %d\0A\00"

declare i32 @printf(ptr, ...)

define i32 @add(i32 %a, i32 %b) {
entry:
  ...
}

define i32 @main() {
entry:
  ...
}
```

The exact IR body will vary with LLVM version, but the module must contain `@printf`, `@add`, and `@main`.

---

## Troubleshooting

### LLVM not found

```
CMake Error: Could not find a package configuration file provided by "LLVM"
```

Point CMake at the correct LLVM cmake directory:

```bash
cmake -S . -B build -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
```

Common paths:

| Platform               | Path                                    |
| ---------------------- | --------------------------------------- |
| macOS (Homebrew arm64) | `/opt/homebrew/opt/llvm/lib/cmake/llvm` |
| macOS (Homebrew x86)   | `/usr/local/opt/llvm/lib/cmake/llvm`    |
| Ubuntu apt llvm-17     | `/usr/lib/llvm-17/lib/cmake/llvm`       |
| Alpine apk llvm17      | `/usr/lib/llvm17/lib/cmake/llvm`        |

You can also run `llvm-config --cmakedir` (substituting the versioned binary name if needed) to get the correct path:

```bash
llvm-config-17 --cmakedir
```

### C++17 errors

If the system default compiler does not support C++17, specify the compiler explicitly:

```bash
cmake -S . -B build \
  -DCMAKE_CXX_COMPILER=clang++-17 \
  -DCMAKE_C_COMPILER=clang-17
```

### Linker errors: undefined LLVM symbols

The build links `support`, `core`, and `irreader`. If you see undefined symbols from LLVM:

1. Confirm the installed LLVM version matches the headers used at configure time (`llvm-config --version`).
2. On Ubuntu, ensure you installed `llvm-17-dev` (not just `llvm-17`); the `-dev` package contains the static libraries.
3. On Alpine, `llvm17-dev` is the correct package name; `llvm17` alone does not ship the `.a` archives.

---

## Running Tests

The automated suite is driven by `tests/run.sh`, which compiles, links, and runs every
`tests/*.esk` case (matching stdout against `*.expected`, smoke-running the rest, and
checking that `tests/errors/*.esk` are rejected):

```bash
tests/run.sh                       # full suite against build/eskiuc
ESKIUC=/path/eskiuc tests/run.sh   # point at a specific compiler
```

CI runs the suite three ways: plain, then under UBSan and ASan (`SANITIZE=ubsan` /
`SANITIZE=asan`) as hardening gates, plus a generative fuzzer
(`tests/fuzz/eskiu_fuzz.py`) that mutates programs and fails on any compiler crash,
IR-verifier failure, or O0-vs-O2 runtime divergence (a differential oracle); a
golden-IR oracle guards against unintended codegen changes.

The four `--test-*` modes below expose individual compiler phases for manual inspection.

### `--test-lexer`

Tokenizes the input and prints every token with its type, value, and source location.

```bash
./build/eskiuc examples/hello.esk --test-lexer
```

Expected output (truncated):

```
Tokenizing: examples/hello.esk
========================================================
  Line   1, Col   1           EXTERN  'extern'
  Line   1, Col   8              INT  'int'
  Line   1, Col  12            IDENT  'printf'
  Line   1, Col  18           LPAREN  '('
  Line   1, Col  19           STRING  'string'
  ...
========================================================
Total tokens: 57
```

### `--test-parser`

Parses the token stream and prints the AST in indented form.

```bash
./build/eskiuc examples/hello.esk --test-parser
```

Expected output (truncated):

```
Parsing: examples/hello.esk
========================================================
Program
  ExternDecl: printf -> int
    Parameters:
      string fmt
      ... ...
  FunctionDecl: add -> int
    Parameters:
      int a
      int b
    Body:
      BlockStmt
        ReturnStmt
          BinaryExpr: +
            Left:
              IdentExpr: a
            Right:
              IdentExpr: b
  FunctionDecl: main -> int
    ...
```

### `--test-typechecker`

Runs semantic analysis. Prints `Type checking succeeded!` on success, or typed errors on failure.

```bash
./build/eskiuc examples/hello.esk --test-typechecker
```

Expected output on a valid file:

```
Type checking: examples/hello.esk
========================================================
========================================================
Type checking succeeded!
```

Example error output for a type mismatch:

```
error: type mismatch in return: expected 'int', got 'float'
```

### `--test-codegen`

Runs the full pipeline through LLVM IR emission and prints the IR to stdout.

```bash
./build/eskiuc examples/hello.esk --test-codegen
```

A non-empty IR module with no `error:` lines on stderr indicates a passing build.

To batch-test all `.esk` files in the examples directory:

```bash
for f in examples/*.esk; do
  echo "=== $f ==="
  ./build/eskiuc "$f" --test-typechecker
done
```
