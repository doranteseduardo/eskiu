---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors. Supersedes `docs/PHASES.md`.

Last updated: 2026-06-03. All phases 0–7 and Phase 5.5 are COMPLETE. Decoder fully ported to Eskiu (crypto.esk + output.esk, 727 lines). Running at 80 ms on arm64. See `ine_decoder/` for the reference implementation.

---

## v0.1 Milestone — COMPLETE

**Goal:** Port the INE credential image-processing pipeline from 3–5 seconds to under 1 second.

**Result:** 74.4 ms total — 2.5x faster than reference C, 40–70x faster than the original target.

| Stage | Eskiu | Reference C |
|-------|-------|-------------|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | <1 ms | 0.5 ms |
| **TOTAL** | **74.4 ms** | **188.9 ms** |

The crypto pipeline matches hand-written C within 0.1 ms.

---

## Recommended Next Steps (v0.2+)

Ordered by value to the compiler and its users.

### Tooling

1. **VS Code LSP extension** — language server backed by `eskiuc --test-typechecker`. Delivers real-time error squiggles, hover types, and go-to-definition without requiring a full LSP implementation: a thin wrapper that pipes the file through the compiler and parses `file:line:col: message` output is sufficient for v0.1 tooling. The TextMate grammar (`editor/vscode/`) is already in the repo and provides syntax highlighting.

### Language

2. **argv / argc support** — programs cannot yet accept CLI arguments natively; the decoder and any CLI tool built with Eskiu must hard-code inputs. This is the highest-leverage language unblock for real-world use.

3. **String.append with realloc** — `stdlib/string.esk` append is a stub. Any program that builds strings dynamically silently truncates. Needs a `realloc`-backed grow loop.

4. **List<T> auto-resize** — `List_push` silently overwrites when at capacity. Auto-resize (double capacity, realloc) prevents subtle bugs in programs that push more than the initial allocation.

5. **Interface dispatch with typed return values** — the current vtable always stores function pointers as `void` returning. Methods that return non-void values via interface dispatch produce garbage. Fixing this unblocks polymorphic APIs.

6. **switch/case in type checker** — codegen works correctly, but the type checker does not validate that case values are compatible with the switch expression type.

7. **Negative number literals** — `int x = -1` works via unary minus, but `-1` cannot be parsed as a primary literal directly. Minor parser gap but trips up newcomers.

### Long-term

8. **Self-hosting** — v1.0 goal. Requires argv/argc, String.append, List auto-resize, and interface dispatch.

9. **Lambdas and closures** — v0.2. Required for higher-order stdlib (`map`, `filter`, `fold` on `List<T>`).

10. **Thread primitives** — v0.2. POSIX `pthread_create`/`pthread_join` + `Mutex` stdlib type.

11. **Exception handling** — v1.0. `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.

---

Phase sections follow (0–11 + v0.1 readiness table), all statuses updated to match milestone completion. The v0.1 readiness table at the end marks every row DONE.
