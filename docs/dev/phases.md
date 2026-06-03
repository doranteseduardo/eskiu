---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors. Supersedes `docs/PHASES.md`.

Last updated: 2026-06-03. All phases 0–7 and Phase 5.5 are COMPLETE. v0.1 milestone achieved. Additional fixes: global variables, sret for large struct returns, integer argument widening.

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

1. **argv / argc support** — programs cannot yet accept CLI arguments natively; the decoder and any CLI tool built with Eskiu must hard-code inputs. This is the highest-leverage unblock for real-world use.

2. **String.append with realloc** — `stdlib/string.esk` append is a stub. Any program that builds strings dynamically silently truncates. Needs a `realloc`-backed grow loop.

3. **List<T> auto-resize** — `List_push` silently overwrites when at capacity. Auto-resize (double capacity, realloc) prevents subtle bugs in programs that push more than the initial allocation.

4. **Interface dispatch with typed return values** — the current vtable always stores function pointers as `void` returning. Methods that return non-void values via interface dispatch produce garbage. Fixing this unblocks polymorphic APIs.

5. **switch/case in type checker** — codegen works correctly, but the type checker does not validate that case values are compatible with the switch expression type. Mismatched types silently fall through to codegen.

6. **Negative number literals in declarations** — `int x = -1` works via unary minus, but `-1` cannot be parsed as a primary literal directly. Minor parser gap but trips up newcomers writing constant tables.

7. **Self-hosting** — long-term v1.0 goal. Requires argv/argc, String.append, List auto-resize, and interface dispatch. No other fundamental blockers remain after those land.

8. **Lambdas and closures** — v0.2. Required for higher-order stdlib functions (`map`, `filter`, `fold` on `List<T>`).

9. **Thread primitives** — v0.2. POSIX `pthread_create`/`pthread_join` + `Mutex` stdlib type.

10. **Exception handling** — v1.0. `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.

---

Phase sections follow (0–11 + v0.1 readiness table), all statuses updated to match milestone completion. The v0.1 readiness table at the end marks every row DONE.
