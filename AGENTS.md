# AGENTS.md

Guidelines for AI agents working on the Eskiu compiler.

---

## What this project is

Eskiu is a systems programming language compiler written in C++17 that emits LLVM IR and links against LLVM 17+. The pipeline is:

```
Source → Lexer → Parser → TypeChecker → CodeGen → LLVM IR → .o → (cc/clang/gcc) → executable

`eskiuc file.esk -o prog` emits the object to a temp file and links it into an
executable by invoking the system C driver (`$CC`, then `cc`/`clang`/`gcc`) —
see `linkExecutable` in `main.cpp`. A `.o` output, `-c`, or `--freestanding`
stops at the object file (no link). `-l`/`-L`/`--link-arg` pass through to the linker.
```

Every pass implements the `ASTVisitor` interface in `ast/ast.h`. When adding a new AST node you must update every visitor: `ASTPrinter`, `TypeChecker`, `CodeGen`.

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/eskiuc --version
```

On macOS, set `LLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm` if cmake cannot find LLVM.

## Test modes

```bash
./build/eskiuc file.esk --test-lexer            # print token stream
./build/eskiuc file.esk --test-parser           # print AST
./build/eskiuc file.esk --test-typechecker      # type check, report errors
./build/eskiuc file.esk --test-codegen          # print LLVM IR
./build/eskiuc file.esk --hover-at LINE:COL     # type at cursor position
./build/eskiuc file.esk --definition-at LINE:COL  # definition location
./build/eskiuc file.esk --target TRIPLE         # cross-compile
./build/eskiuc file.esk --freestanding          # no libc (esk_alloc/esk_free)
```

Use `examples/` and `tests/` as inputs. Add a `.esk` file for any feature you implement.

## Source layout

| Path | Responsibility |
|---|---|
| `lexer/` | Tokeniser. `Lexer::next_token()` only. |
| `parser/` | Recursive-descent. Returns `shared_ptr<Program>`. |
| `ast/ast.h` | All AST node types + `ASTVisitor` interface. |
| `ast/ast.cpp` | `accept()` definitions. |
| `ast/ast_printer.cpp` | Pretty-printer (`--test-parser`). |
| `sema/type_checker.cpp` | Type inference, scope, struct/interface/template registry. |
| `codegen/codegen.cpp` | LLVM IR via `IRBuilder`. |
| `main.cpp` | CLI entry point. |
| `stdlib/` | Eskiu stdlib modules (`result.esk`, `list.esk`, etc.). |
| `tests/` | Regression tests (`.esk` files). |
| `examples/` | Working demos. |
| `kernel/` | Bare-metal ARM64 kernel for QEMU (v0.1 milestone). |

## Current language status (v0.1.0)

All items below are implemented and tested end-to-end.

| Feature | Notes |
|---|---|
| Primitive types | int/8/16/32/64, uint, float, double, bool, char, string, void, `*T` |
| Structs + methods | `Struct_method(self, ...)`, named/positional initialisers |
| Interfaces | Structural typing, vtable fat-pointer dispatch |
| Templates | Monomorphic instantiation: `Result<int,string>` → `%Result_int_string`. Struct literals `Pair<int,float>{...}`; explicit `f<int>(...)` or inferred `f(3)` type args (inference only when a type param appears directly as a parameter type) |
| Enums | `enum Color { Red, Green = 5, Blue }` — members are int constants; the enum type maps to `i32`; usable in `switch`/comparisons |
| Type aliases | `type u8 = uint8;` — resolved to the underlying type in sema and codegen (`getTypeFromString`/`getExprEskiuType` expand aliases) |
| Bitfields | `struct F { uint32 a : 1; uint32 b : 3; }` — packed into storage words; masked load + read-modify-write store; signed fields sign-extend. Layout in `structLayout`; only structs with a bitfield change layout |
| Packed structs | `packed struct` or `#pragma pack(1)` → `StructDecl::isPacked` → `StructType::create(..., isPacked)`. `#pragma` survives `preprocess()`, lexes to a `PRAGMA` token, and the parser's pack stack (`currentPack`/`packStack`, `applyPragma`) tags structs declared under `pack(1)` |
| Preprocessor | object-like + function-like `#define`/`#undef` (recursive expansion, `\`-continued multi-line bodies) and `#ifdef`/`#ifndef`/`#else`/`#endif`. A text pass in the `Lexer` ctor (`preprocess()`); macro table is shared (passed into the ctor) so `#define`s cross `import`/multi-file; directives/skipped lines blank out to preserve line numbers. `#pragma` is passed through to the parser, not consumed |
| Forward declarations | Body-less `int f(int n);`; call-before-define and mutual recursion (codegen declares all prototypes before emitting bodies) |
| Compound assignment | `+= -= *= /= %=` and bitwise `&= \|= ^= <<= >>=` |
| Lambdas | `int(int x) { return x * 2; }` — anonymous functions |
| Closures | `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; captures by value |
| Function-as-value | A bare function name used as a value decays to a `fn(...)->R` via `makeFunctionPointer` (synthesizes a `__fnptr_<name>` env-ignoring thunk). `visit(CallExpr)` resolves direct named calls before `evaluateExpr` so calls don't decay |
| Predefined macros | `main.cpp` seeds the shared macro table with `__APPLE__`/`__linux__` (host OS) for `#ifdef` portability |
| `<net>` sockets | `stdlib/net.esk` — POSIX socket `extern`s + portable `packed sockaddr_in` (`#ifdef __APPLE__`) + `net_*` helpers. No compiler support needed beyond FFI |
| `thread_create` / `thread_join` | Language keywords; fat-pointer maps to `pthread_create(fn, env)` |
| `try` / `catch` / `finally` / `throw` | LLVM `invoke`/`landingpad` + `__gxx_personality_v0`; link `-lc++` |
| Inline assembly | `asm("cli")` simple; `asm("op" :: "r"(x) : "mem")` extended |
| `volatile` | `volatile let reg: *uint8 = addr;` — MMIO-safe |
| Cross-compilation | `--target TRIPLE` (AArch64 and X86 backends included) |
| Freestanding | `--freestanding` — `alloc`/`free` call `esk_alloc`/`esk_free` |
| Negative literals | `-1`, `-3.14` as first-class primary expressions |
| argv / argc | `int main(int argc, string* argv)` |
| Multi-file compile | `eskiuc a.esk b.esk -o prog` — declarations from all inputs are merged into one program |
| Warnings (`-Wall`) | Unused variables/parameters/functions, assignment-in-condition; off by default |
| VS Code | Real-time errors, hover types, go-to-definition |
| stdlib | `result.esk`, `list.esk`, `string.esk`, `math.esk`, `io.esk`, `mem.esk` |

## Roadmap (as of v0.1.0)

| Milestone | Items | Status |
|---|---|---|
| Systems milestone | Bare-metal kernel on QEMU | ✅ |
| v0.1 | Closures, threads, exceptions | ✅ |
| v0.2 | HTTP stdlib (`http.esk`) | ❌ |
| v1.0 | Package manager, self-hosting | ❌ |

## Adding a new AST node

1. Define the class in `ast/ast.h` — extend `Expr`, `Stmt`, or `Decl`.
2. Add `virtual void visit(YourNode*) = 0` to `ASTVisitor`.
3. Add `void YourNode::accept(ASTVisitor* v) { v->visit(this); }` in `ast/ast.cpp`.
4. Add `void visit(YourNode*) override` in: `ASTPrinter`, `TypeChecker`, `CodeGen`.
5. Add parse site in `parser/parser.cpp` (statement → `parseStatement`, expression → `parsePrimary` or `parseUnary`).
6. Write a test in `tests/` and verify with `--test-typechecker` and `--test-codegen`.

## Coding rules

- **C++17 only.** No C++20.
- **No new dependencies** beyond LLVM and the standard library.
- `camelCase` methods, `snake_case` locals. No trailing comments.
- AST nodes use `shared_ptr` throughout (`ExprPtr`, `StmtPtr`, `DeclPtr`). This is **load-bearing**, not incidental: struct-method bodies are co-owned by the synthesized `StructName_method` `FunctionDecl`, and template bodies are co-owned across every monomorphic instantiation (see `make_shared<FunctionDecl>(..., fd->body)` in codegen). Do **not** convert to `unique_ptr` without first adding deep-clone infrastructure for those shared bodies. To keep refcounting cheap: constructors take `Ptr` by value and `std::move` into members, and functions that only read a node take `const ExprPtr&` (never `ExprPtr` by value) — e.g. `evaluateExpr`, `getExprEskiuType`.
- Pointer types are strings ending or beginning with `*` (e.g. `"*uint8"`, `"int*"`). Use `isPointerType()` and `getPointeeType()` in both `TypeChecker` and `CodeGen`.
- Block bodies are `vector<BlockItem>` where `BlockItem = variant<DeclPtr, StmtPtr>`. Never split into two lists.

## Type mappings

| Eskiu | LLVM |
|---|---|
| `int` / `int32` | `i32` |
| `int8` / `uint8` | `i8` |
| `int16` / `uint16` | `i16` |
| `int64` / `uint64` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i8` |
| `string` | `ptr` (i8*) |
| `*T` / `T*` | opaque `ptr` |
| `fn(T)->R` | `{ ptr fn_ptr, ptr env_ptr }` struct |

## Key codegen patterns

**Closures:** `fn(T)->R` is `{ptr, ptr}`. Lambda functions always receive `ptr env` as the first parameter. Non-capturing lambdas get `env = null`. Call sites extract `fn_ptr` and `env_ptr` from the struct and invoke `fn_ptr(env_ptr, args...)`.

**Exceptions:** `throw` calls `__cxa_throw` via `invoke` when inside a try body (so the local landingpad fires). `try` bodies use `invoke` for all calls. `catch` uses `landingpad { ptr, i32 } catch ptr null` (catch-all) with manual type comparison via the embedded type name in the exception object.

**Threads:** `thread_create(fn()->void)` extracts `fn_ptr` and `env_ptr` from the fat pointer and calls `pthread_create(tid, null, fn_ptr, env_ptr)` directly.

**Inline assembly:** Uses `llvm::InlineAsm::get` with `AD_ATT` dialect. Operand references use `$0`, `$1` (LLVM IR syntax, not `%0` GCC syntax). Inside try bodies, asm statements are not converted to `invoke` — asm is assumed not to throw.

**Cross-compilation:** When `targetTriple != ""` and differs from native, the CPU is set to `"generic"` to avoid host CPU features leaking into the cross-compiled object.

## Error format

```
error: <file>:<line>:<col>: <message>
```

Use `errorAt(node, message)` in `TypeChecker`. Codegen errors use `throw std::runtime_error(...)`.

## What agents should not do

- Do not introduce third-party libraries or new CMake targets.
- Do not rewrite visitor dispatch to use `std::variant` — virtual dispatch is intentional.
- Do not amend published commits; always create new ones.
- Do not skip `--test-typechecker` and `--test-codegen` validation before declaring a feature complete.
