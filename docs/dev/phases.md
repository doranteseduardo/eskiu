---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors. Supersedes `docs/PHASES.md`.

Last updated: 2026-06-03. All phases 0–7, Phase 5.5, Phase 8 (lambdas), and editor tooling are COMPLETE. Decoder fully ported to Eskiu (crypto.esk + output.esk, 727 lines). Running at 80 ms on arm64. See `ine_decoder/` for the reference implementation.

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

### Language

1. **Negative number literals** — `int x = -1` works via unary minus, but `-1` cannot be parsed as a primary literal directly. Minor parser gap but trips up newcomers.

### Long-term

4. **Self-hosting** — v1.0 goal. Requires argv/argc, interface dispatch with typed returns, and lambda closures.

5. **Thread primitives** — v0.2. POSIX `pthread_create`/`pthread_join` + `Mutex` stdlib type.

6. **Exception handling** — v1.0. `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.

---

Phase sections follow (0–11 + v0.1 readiness table), all statuses updated to match milestone completion. The v0.1 readiness table at the end marks every row DONE.
