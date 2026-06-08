# Code Review: eskiu Compiler (all subsystems)

**Scope:** lexer, parser, sema, codegen, main  
**Dimensions:** correctness, performance, security, maintainability  
**Verdict:** Resolved — actionable findings fixed on `develop`; the remainder is deferred to a post-v0.2.0 cleanup pass (see **Resolution status**).

---

## Summary

The eskiu compiler is well-structured and impressively capable for its stage — it has a real preprocessor, template inference, interface vtables, bitfields, lambdas, inline asm, and a full LLVM backend. The code is readable and the subsystem boundaries are clean. Most issues are in the **correctness** and **maintainability** buckets; there are no obvious security holes (it's a compiler, not a web server), but a handful of patterns can crash or silently miscompile.

---

## Resolution status

_Updated 2026-06-07 — branch `develop`. Each finding was validated against the source before acting; a few were found inaccurate._

**Fixed**
- **#1, #2, #8** — the lexer now emits diagnostics for unterminated string / char / block-comment literals and for empty/multi-character char literals (`Lexer::hadError` + `lexError`, with recovery and driver checks). Regression tests added under `tests/errors/`.
- **#6** — `TargetMachine` is now held via `std::unique_ptr` in both `generateCode` and `emitObjectFile`; the latter also leaked on its error-return paths, now fixed.
- **#18** — `mangleTemplate` / `splitTemplateType` / `substType` (verbatim duplicates) extracted to a shared `template_utils.h`.
- **#19** — `loadProgram()` helper removes the load→lex→parse boilerplate duplicated across the parse / typecheck / codegen / `--hover-at` / `--definition-at` modes.
- **#20** — `dirOf()` helper replaces the six copy-pasted `basedir` computations in `main.cpp`.
- **#22** — `TypeChecker` collapsed to a single `public:` section.
- **#23** — `tokenTypeToString` no longer has a `default:` case; `-Wswitch` now enforces exhaustiveness (this immediately caught a missing `SIZEOF` case that was silently rendering as `"???"`).

**Invalid / won't fix** (verified against the code)
- **#5** — false: `buf` is zero-initialized and `readlink` is bounded to `sizeof(buf)-1`, so the result is always NUL-terminated; macOS uses `_NSGetExecutablePath`. At most silent truncation for ≥4 KB paths; not Critical.
- **#9, #10, #11** — unreachable: these run only after the keyword / a successful `match()` is consumed, so `current ≥ 1` always; the described UB cannot occur.
- **#15** — false: the Lexer's `source` is the *preprocessed* buffer, not a copy of the caller's input, so a `string_view` is impossible and `std::move` does not apply.

**Deferred to post-v0.2.0** (real but low-leverage; the v0.2.0 work will reshape these files)
- **#21** — `ParserConfig` instead of public mutable fields.
- **#24** — the `parseStatement` `LBRACE` "unget" idiom.
- **#25** — splitting the `CodeGen` god-class (best done once async/await and allocator hooks pull the seams apart).
- **#16, #17** — micro-perf (token-vector copies, redundant `getExpressionType` lookups); skip until profiling shows a need.

Severity note: several items the original review labelled 🔴 Critical were in fact diagnostic-quality or benign-cleanup issues (#3, #4, #6) or outright non-bugs (#5, #9–#11, #15). The genuinely silent-failure items were #1/#2/#8, now fixed.

---

## Critical Issues

| # | File | Location | Issue | Severity |
|---|------|----------|-------|----------|
| 1 | `lexer/lexer.cpp` | `read_string()` ~L479 | Unterminated string literal silently produces a token with whatever was consumed — no error is emitted. If `is_at_end()` is true when the loop exits, the closing `"` is never consumed and no diagnostic fires. | 🔴 Critical |
| 2 | `lexer/lexer.cpp` | `read_char()` ~L523 | Same issue for `'`: unterminated char literal is silently accepted. Multi-character char literals (e.g. `'ab'`) also silently swallow both characters without warning. | 🔴 Critical |
| 3 | `parser/parser.cpp` | `parseProgram()` L288–301 | Parse error recovery always skips to the next `;`. For block-level errors (e.g. missing `}`) this will consume tokens across declaration boundaries, turning one real error into a cascade of phantom errors that are hard to read. | 🔴 Critical |
| 4 | `parser/parser.cpp` | `parseBlockStatement()` L737–739 | Silent exception swallowing: `catch (...)` discards all errors and backtracks silently. If the block was actually a declaration and parsing failed for a real reason (e.g. type mismatch), the error disappears and the statement is re-parsed as an expression, producing a confusing second error instead. | 🔴 Critical |
| 5 | `main.cpp` | `resolveStdlibPath()` ~L93 | `buf[4096]` may be too small for long paths (Linux `readlink` doesn't null-terminate on success). The result is used directly as a `std::string(buf)` without checking the return length. If the path is ≥ 4095 chars the string will be corrupt. | 🔴 Critical |
| 6 | `codegen/codegen.cpp` | `generateCode()` L112–114 | `TargetMachine` is created with `new` and immediately `delete`d just to get the data layout. This is a raw owning pointer — if `createDataLayout()` throws or the delete path is missed on a future code path, it leaks. Use `std::unique_ptr`. | 🟠 High |

---

## Correctness Issues

| # | File | Location | Issue |
|---|------|----------|-------|
| 7 | `lexer/lexer.cpp` | `read_number()` L461 | Float detection: `1.` (trailing dot, no digit after) is NOT recognized as a float — the condition `std::isdigit(peek_next())` skips it. So `1.foo` lexes as `INT_LIT "1"` then `DOT` then `IDENT "foo"`, which is correct for method chaining — but `let x: float = 1.;` would silently assign an int. Document this intentional decision or handle it. |
| 8 | `lexer/lexer.cpp` | `skip_comment()` ~L417–429 | Unterminated block comment (`/* ...` with no `*/`) silently reaches EOF and stops — no error emitted. |
| 9 | `parser/parser.cpp` | `parseReturnStatement()` L827 | `tokens[current - 1]` is accessed without a bounds check. If `current` is 0 (edge case at start of stream), this is UB. |
| 10 | `parser/parser.cpp` | `parseContinueStatement()` L844 | Same pattern: `tokens[current - 1]` without bounds check. |
| 11 | `parser/parser.cpp` | `parseDeclaration()` L437–438 | `tokens[current - 1].type` is accessed after `match(TokenType::EQ)`. If `current` was 0 before the match (extremely unlikely but valid), this is UB. Use the return value of `match` instead of inspecting `tokens[current-1]`. |
| 12 | `sema/type_checker.cpp` | `visit(ForInStmt*)` L346–366 | The `for-in` element-type inference is heuristic (looks for fields named `data` and `size`). If a custom struct happens to have those fields with different semantics, the type checker silently infers the wrong element type rather than erroring. |
| 13 | `sema/type_checker.cpp` | `visit(ReturnStmt*)` L444 | Empty-return in a non-void function is only caught if `currentFunctionReturnType` is non-empty. Template functions skip type-checking (`if (!node->typeParams.empty()) return;`), so a template function with a missing return will never get this check on instantiation. |
| 14 | `main.cpp` | `main()` L514–516 | Default output filename is `InputFilename + ".o"` (e.g. `foo.esk.o`). This is unusual — most compilers produce `foo.o`. Minor but surprising for users. |

---

## Performance Issues

| # | File | Location | Issue |
|---|------|----------|-------|
| 15 | `lexer/lexer.h` | `Lexer` class | `source` is stored as a `std::string` copy in the `Lexer`. For large files this is an unnecessary copy since the caller owns the source. Consider `std::string_view` for the internal buffer (or at least `std::move` from the constructor parameter). |
| 16 | `parser/parser.h` | `Parser` class | `tokens` is a `std::vector<Token>` copy. In large files (many imports) the token vector is copied once per nested parser. Passing a `const std::vector<Token>&` and keeping an iterator range would halve allocations for imported files. |
| 17 | `sema/type_checker.cpp` | Various `visit()` methods | `getExpressionType()` does a `std::map<Expr*, std::string>` lookup on every call, and several visitors call it multiple times on the same node (e.g. `visit(BinaryExpr*)` calls it twice). Results are cached in `expressionTypes`, but a non-null check before the visitor recurse would short-circuit redundant visits. |
| 18 | `codegen/codegen.h` | Template utilities | `mangleTemplate`, `splitTemplateType`, and `substType` are duplicated verbatim between `codegen.cpp` and `type_checker.cpp`. Besides being a DRY violation, the duplicate parsing logic runs twice per template instantiation. Move to a shared `template_utils.h`. |
| 19 | `main.cpp` | All pipeline functions | The token vector is rebuilt from scratch for every pipeline function (`testLexer`, `testParser`, `testTypeChecker`, `testCodegen`, `--hover-at`, `--definition-at`). The file is re-read and re-lexed each time even though only one mode runs at a time — this is fine currently, but tokenizing is the cheapest step; the real concern is that each function duplicates 30+ lines of boilerplate. Extract a shared `loadAndParse(filename) -> shared_ptr<Program>` helper. |

---

## Maintainability Issues

| # | File | Location | Issue |
|---|------|----------|-------|
| 20 | `main.cpp` | L224, L272, L328, L403, L431 | The `basedir` computation (`rfind("/")` inline) is copy-pasted six times across the file. Extract a one-liner helper `dirOf(path)`. |
| 21 | `parser/parser.h` | `Parser` public fields | `basedir`, `stdlibPath`, `importedFiles`, `macros`, and `hadError` are all public mutable fields set after construction. This is an informal "configuration object" pattern; it's easy to forget to set one. Consider a `ParserConfig` struct passed at construction time, or at least document which are required. |
| 22 | `sema/type_checker.h` | Mixed `public`/`private` | The class has two separate `public:` sections (lines 63 and 161). The second public section (LSP helpers: `sourceFile`, `getTypeAtPosition`, `definitionLocations`, etc.) is conceptually a different interface from the type-checking interface. A single well-ordered `public` block with a comment separator would be clearer. |
| 23 | `lexer/lexer.cpp` | `tokenTypeToString()` | This function is 110 lines of switch-case with a `default: return "???"`. Any new `TokenType` added to the enum will silently fall through to `"???"` rather than failing a compile-time check. Use `[[clang::exhaustive_switch]]` or a `static_assert` trick, or at minimum use `-Wswitch` and don't rely on `default`. |
| 24 | `parser/parser.cpp` | `parseStatement()` L602–605 | The `LBRACE` case decrements `current` then calls `parseBlockStatement()`. This is a fragile "unget" idiom — it works, but it's easy to break. A `peekBlock()` helper or simply checking `check(LBRACE)` before consuming would be safer. |
| 25 | `codegen/codegen.h` | Class size | `CodeGen` has 40+ private members and 30+ visitor methods in one class. It is doing code generation, type resolution, template instantiation, interface boxing, bitfield layout, and sret ABI handling. Consider extracting sub-objects (e.g. `BitfieldLayoutHelper`, `InterfaceBoxHelper`, `TemplateInstantiator`) even if they remain `friend` classes. |

---

## What Looks Good

- **Preprocessor design** — the line-splicing, conditional compilation (`#ifdef`/`#ifndef`/`#else`/`#endif`), and recursive macro expansion with cycle detection are well-implemented. Line number preservation via blank-line injection is a thoughtful touch.
- **Import deduplication** — the shared `importedFiles` set correctly prevents re-parsing the same canonical path across recursive imports.
- **Error positions on AST nodes** — `withPos()` and the consistent `line`/`col` stamping throughout the parser make the diagnostics much more useful than most hobby compilers.
- **Template inference** — type parameter inference via `unifyTypeParam` handles composite types (`List<T>*`) and is structurally sound.
- **`#pragma pack` stack** — `push`/`pop` semantics are correctly implemented, which is easy to get wrong.
- **LLVM verification** — calling `llvm::verifyModule` before returning from `generateCode` catches IR bugs early and is a good safety net.
- **Freestanding mode** — the `esk_alloc`/`esk_free` path and the separate `kernel/` and `ine_decoder/` project trees show real-world usage of the compiler.

---

## Recommended Priority Order

1. Fix unterminated string/char/comment diagnostics (items 1, 2, 8) — these produce silent bad tokens that cause confusing downstream failures.
2. Fix `tokens[current - 1]` accesses without bounds checks (items 9, 10, 11).
3. Fix `readlink` buffer handling in `resolveStdlibPath` (item 5).
4. Replace raw `TargetMachine*` with `unique_ptr` (item 6).
5. Extract `dirOf()` helper and `loadAndParse()` helper in `main.cpp` (items 19, 20).
6. Move `mangleTemplate`/`splitTemplateType`/`substType` to a shared header (item 18).
