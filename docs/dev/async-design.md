# Async / Await — Design Note

**Status:** implemented (async core complete). This note pinned the contract
*before* the transform was written, because the parts it fixes are expensive to
change once async functions exist in the wild (see "What is locked, what is free").

**Progress (gate, §9): COMPLETE ✅.** Step 0 (atomic intrinsics) ✅.
`stdlib/future.esk` holds the locked `Future<T>`/`FutureHdr` contract and the §3
handshake. A hand-written coroutine validated all three gate requirements:
reactor-driven read over `<eventloop>` ✅, type-erased drop/cancel ✅, and
**cross-thread resume** (worker thread completes; atomic CAS catches the parked
loop; waker marshals via a self-pipe; continuation runs on the loop thread) ✅.
The gate surfaced and we solved a blocking prerequisite — **escaping closures**
(a waker/callback outlives its creating function) read a dangling stack env — now
fixed language-wide via escape analysis + the `escaping` qualifier + `free_closure`
(spec §6.5). The runtime model is proven sound. Implementation since: the Executor + leaf
futures (`<executor>`/`<net_async>`, steps 2–3) ✅; the `async`/`await` frontend
(steps 4–5) ✅; and the AST→state-machine transform (`sema/async_transform.cpp`,
steps 6–7) — **single and multiple `await` working end-to-end** (fast path +
single/multi suspend over real reactor reads; values thread through frame fields
across N+1 states). Done: single+multi await, return await, async void, cancellation, control flow
around await (if/while), full closure-env ownership — verified leak-free with
`leaks`. Remaining: for/switch around await, tighter locals-across-await liveness.

**Audience:** compiler maintainers. Assumes familiarity with the existing closure
model (`fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}` that captures by value),
monomorphic templates, the `<eventloop>` reactor, and `<threading>`.

---

## 1. Goals and non-goals

**Goals**

- `async fn` and `await` usable for real network I/O today (the prod projects need
  it for HTTP): an async TCP read/connect that suspends without blocking a thread.
- **Cancellation / drop in v1.** A future can be dropped before it completes;
  doing so releases its resources (loop registrations, memory) and cascades into
  whatever it was awaiting.
- **Multi-threaded execution with thread-affinity in v1.** A future may be
  completed on one thread while its continuation must resume on another — the
  motivating case is the future UI framework: background work completes on a worker
  pool, but the continuation that touches UI state must run on the UI thread. This
  dictates how completion and the waker work, so it is part of the locked contract,
  not a later bolt-on.
- A `Future<T>` whose shape and atomic protocol we will *not* break when we later
  add timers, channels, and `select`/`join` combinators.
- Reuse the existing compiler: closures (resume/drop continuations), monomorphic
  templates (`Future<T>`), AST-visitor passes. The state-machine split is an
  **AST→AST transform** — inspectable via `--test-parser`, reuses 100% of existing
  codegen, no LLVM coroutine intrinsics.
- Manual memory, no GC, no hidden refcounting.

**Prerequisite this design pulls in**

- **Atomic intrinsics** (atomic load/store/CAS/swap with acquire/release ordering).
  Multi-thread completion needs a lock-free state handshake. LLVM exposes these
  directly (`cmpxchg`, atomic `load`/`store`), so codegen is tractable, and
  `<threading>` benefits independently. This is implemented *before* the executor
  (§9, step 0).

**Non-goals (v1 — deferred, each forward-compatible)**

- `select` / `join` combinators. Composable later on this shape; the v1 pieces they
  need — cancellation (to drop losers) and cross-thread completion — are present.
- Cooperative cancellation that runs cleanup in a *suspended* coroutine (i.e.
  `finally`-past-`await`). v1 drop frees the frame without resuming it (§7); the
  forward path is a cancellation token, no contract change.
- Work-stealing / load-balancing across executors. The executor abstraction allows
  it later; v1 uses fixed thread affinity.

---

## 2. The runtime model: completion + waker, atomic, with drop

Two classic shapes: **poll-based** (Rust; inert futures, executor polls — payoff is
avoiding per-future allocation) and **completion + waker** (JS Promise / C# Task;
the future holds state + result + continuation).

**We choose completion + waker.** Our reactor already dispatches on readiness (a
completion event), we already heap-allocate frames (so poll's allocation savings
don't apply), and completion is simpler to *generate* — suspend points are explicit.

The shape, an ordinary stdlib template (`stdlib/future.esk`):

```eskiu
struct Future<T> {
    int        state;     // ATOMIC. 0 pending, 1 waiting, 2 ready, 3 cancelled  (§3)
    fn()->void waker;     // completion path: schedule the awaiter on its home executor
    fn()->void on_drop;   // cancel path: release own resources + cascade
    T          value;     // valid only when state == ready; LAST (§2.1)
}
```

A **type-erased header** aliasing the first three fields for any `T`:

```eskiu
struct FutureHdr { int state; fn()->void waker; fn()->void on_drop; }
```

`(FutureHdr*)f` lets cascade-drop, the executor, and any thread touch
`state`/`waker`/`on_drop` without knowing `T`.

### 2.1 Why `value` is last

`Future<int>` and `Future<string>` are distinct monomorphizations of different
sizes. If `value` sat before the other fields, `waker`/`on_drop`/`state` would land
at different offsets per `T` and no `FutureHdr` view would be possible — cascade-drop
and cross-thread dispatch would break. `value` **last** fixes the header offsets for
every `T`. Free today, a hard break later — hence locked.

### 2.2 Why `state` is atomic

In multi-thread, a completer (worker thread) and the awaiter (home thread) touch the
same future concurrently. Without atomicity there is a lost-wakeup race: the awaiter
decides to park and the completer signals readiness in between. The fix is a
lock-free handshake on an **atomic `state`** with four values (§3) and
acquire/release ordering so `value`, written before publish, is visible after the
reader observes `ready`. Atomicity is a property of the *accesses*, not an extra
field — but the four-value encoding and the ordering are part of the locked contract.

Why these four fields and only these — the full walk — is §5.

---

## 3. Protocols (the compiler↔generated-code ABI)

`state` is an atomic int: `PENDING=0`, `WAITING=1`, `READY=2`, `CANCELLED=3`.
`READY` and `CANCELLED` are terminal. All transitions are CAS/swap with
acquire/release; `value` is written before the publish to `READY` and read only
after observing `READY`.

### 3.1 Park (awaiter, on its home thread) — `let x = await F;`

```
F.waker = <resume-me>;                       // publish continuation
if (CAS(&F.state, PENDING -> WAITING)) {
    frame.awaiting = (FutureHdr*)F; return;  // parked; executor will resume us
}
// CAS failed -> F is already READY (completer beat us): take the value inline
let x: T = F.value;  frame.awaiting = null;  free_future(F);
// ...continue with x...
```

### 3.2 Complete (producer, any thread)

```
F.value = <result>;                          // write result first
F.on_drop = <just-free F>;                   // external resources already released
old = SWAP(&F.state, READY);                 // release
if (old == WAITING) F.waker();               // awaiter parked -> schedule its resume
// old == PENDING: awaiter not parked yet; it sees READY in its CAS and takes value inline
// old == CANCELLED: dropped first -> producer releases <result> and frees (§3.4 arbitration)
```

**The waker does not run the continuation inline.** It *schedules* the awaiter's
resume on the awaiter's **home executor** (§4 captures that executor in the waker
closure) and wakes that executor. So completion on a worker thread resumes the
coroutine on, e.g., the UI thread — and, as a bonus, resume is never a nested call,
which removes the deep-stack concern entirely.

### 3.3 Drop / cancel (any thread) — `future_drop((FutureHdr*)F)`

```
old = SWAP(&F.state, CANCELLED);
if (old == READY) { free_future(F); }        // completed already: just free memory
else { F.on_drop(); }                        // PENDING/WAITING: release + cascade + free
```

- A **leaf** future's `on_drop` deregisters its fd/timer from the loop, frees itself.
- A **coroutine** future's `on_drop` cascades: `if (frame.awaiting) future_drop(frame.awaiting);`
  then frees the frame. The coroutine is **not** resumed (§7).

### 3.4 Arbitration (the one invariant)

> A future is finalized **exactly once**. The atomic swap to a terminal state
> (`READY` or `CANCELLED`) has a single winner; the loser observes the terminal
> state as its `old` and performs only the free, never a second resource release.
> Memory is freed exactly once, by whichever path reached terminal.

This is the heart of the design and the part step-1 (§9) must prove with a
cross-thread test — the note locks the *requirements* (atomic 4-state, ordering,
single-free); the exact CAS choreography lives in the proven `future.esk`.

---

## 4. Lowering an `async fn` to a state machine (AST transform)

```eskiu
async int fetch_len(EventLoop* lp, string host) {
    int fd = await net_connect_async(lp, host);   // await #1
    int n  = await net_read_async(lp, fd);         // await #2
    return n;
}
```

### 4.1 The frame

One struct per async fn — embeds the return future (one allocation), resume state,
the `awaiting` back-pointer (cascade-drop), the home executor, params, and locals
**live across an await**:

```eskiu
struct __Frame_fetch_len {
    Future<int> ret;       // &frame.ret is the returned Future<int>*
    int         st;        // resume state
    FutureHdr*  awaiting;  // inner future currently parked on (null otherwise)
    Executor*   home;      // where this coroutine's resumes must run (thread-affinity)
    EventLoop*  lp;        // param
    string      host;      // param
    int         fd;        // local live across await #2
}
```

`awaiting`/`home` are *frame* fields, not `Future` fields — free to adjust, no
contract cost. The frame is **confined to its home executor**: only that executor
ever calls `__resume_*` on it, so the frame needs no locking. The only cross-thread
object is the `Future` header, synchronized per §3.

### 4.2 Constructor + resume

- **Constructor** `Future<int>* fetch_len(EventLoop* lp, string host)`: allocs the
  frame, stores params, `st=0`, `awaiting=null`, `home = current_executor()`, sets
  `frame.ret.on_drop = <cascade-drop awaiting, free frame>`, calls
  `__resume_fetch_len(frame)` once, returns `&frame.ret`.
- **Resume** `void __resume_fetch_len(__Frame_fetch_len* f)`:

```
switch (f.st) {
case 0:
  Future<int>* g = net_connect_async(f.lp, f.host);
  f.waker_of_g = <closure capturing f, f.home: schedule "f.st=1; __resume(f)" on f.home>;
  // park via §3.1 against g; if g already ready, fall through with g.value
  ... if parked: f.awaiting=(FutureHdr*)g; f.st=1; return;
  f.fd = g.value; f.awaiting=null; free_future(g);
case 1:
  Future<int>* h = net_read_async(f.lp, f.fd);
  ... (same park/fast-path against h, resume label 2) ...
  int n = h.value; f.awaiting=null; free_future(h);
  // return n: complete our own future (§3.2), then the awaiter's waker frees f
  f.ret.value = n; f.ret.on_drop = <just-free f>;
  old = SWAP(&f.ret.state, READY); if (old==WAITING) f.ret.waker();
  return;   // do not touch f afterward
}
```

The continuations are existing closures capturing `f` and `f.home` by value — no new
machinery. (The §3.1 park sequence is emitted inline at each await; shown abbreviated.)

### 4.3 Fast path

The park's failed-CAS branch means an already-ready inner future does **not**
suspend — read value, fall through. Zero extra round-trips when nothing blocks.

---

## 5. Field walk: why `{state, waker, on_drop, value}` survives everything

| Await source | Completes via | Drops via | New field? |
|---|---|---|---|
| Socket readable (leaf) | loop callback reads, §3.2 | `on_drop`=`el_del`+free | no |
| Another `async fn` | resume §3.2 | `on_drop` cascades `awaiting`, frees frame | no |
| Timer `await sleep(ms)` | loop timeout, §3.2 | `on_drop` cancels timer+free | no |
| `await thread_join(t)` / worker pool | worker completes on its thread; waker schedules resume on home executor (§3.2) | `on_drop` detach+free | no |
| `await chan.recv()` | sender §3.2 | `on_drop` unlinks+free | no |
| **UI: bg work → UI-thread continuation** | worker §3.2; waker enqueues on UI executor | parent `on_drop` | no |
| `select`/`join` (later) | first child §3.2 | parent drops losers | no |

- **No `error` field** — fallible async returns `Future<Result<T,E>>`; error rides
  in `value`, composes with `?`.
- **No `waker_ctx`/`dropctx`/`executor` field in `Future`** — closures capture by
  value, so the resume thunk and the home executor live in the waker's env.
- **No lock field** — the atomic `state` handshake (§3) replaces a per-future lock.

### 5.1 `Future<void>`

`async void f()` lowers internally to `Future<Unit>` (`Unit` = 1-byte `uint8`);
`await f()` discards the value. Invisible at source level.

---

## 6. Executors, the event loop, and thread-affinity

An **Executor** is a thread plus a thread-safe ready-queue and a wakeup mechanism
(self-pipe / `eventfd` registered with that thread's `<eventloop>`, or a condvar).
`current_executor()` returns the one running the calling code.

- A coroutine's `home` executor is captured at construction; its waker enqueues the
  resume onto `home`'s ready-queue and wakes `home`. The executor's run loop pops
  ready entries and calls `__resume_*`.
- **I/O wake source.** `<eventloop>` is one source of completions: when an fd is
  ready, its leaf future completes (§3.2), scheduling the awaiter's resume on the
  awaiter's home executor — which may differ from the loop's thread.
- **UI framework shape.** The UI thread runs an executor; `spawn` background work on
  a worker-pool executor; the worker completes the future and the waker marshals the
  continuation back to the UI executor. This is the JS-main-thread / Swift-MainActor
  / Kotlin-dispatcher model, and it needs *no `Future` change* — only that the waker
  schedules onto `home`.

Cross-thread enqueue + wakeup and the ready-queue are **executor machinery, free to
evolve** (single-thread, fixed pool, later work-stealing). What is locked is only
that completion goes through `waker()` and `waker` is responsible for landing the
resume on the right thread.

---

## 7. Memory and cancellation: one invariant

> **Every future is finalized exactly once** — *awaited to completion* (the awaiter
> frees it), or *dropped* (`future_drop` frees it via `on_drop`). The atomic swap to
> a terminal state picks the single winner (§3.4); the other path only frees.

One allocation per async call (frame + embedded return future), one per leaf future.
A future created but never awaited or dropped is a *preventable* bug, not an accepted
leak: the transform inserts `future_drop` on scope-exit paths where a `Future` local
was created and not consumed.

**Cascade.** Dropping a suspended coroutine drops `frame.awaiting` first, recursively,
then frees the frame — a whole await-chain torn down by dropping its head.

**Accepted v1 semantic:** dropping a *suspended* coroutine frees its frame **without
resuming it**, so statements after the suspend point — including `finally` past an
`await` — do not run (matches Rust async-drop). Guaranteed cleanup-on-cancel uses a
cooperative **cancellation token** the coroutine checks (a normal threaded value, no
contract change) — a deliberate later feature.

---

## 8. Surface syntax and type rules

- `async` is a function modifier: `async T f(...)`, parsed like `volatile let` (a
  leading qualifier before the return type).
- An `async T f(...)` has *declared* return type `T`; its *call expression* has type
  `Future<T>*`. The type checker performs this rewrite.
- `await E`: `E : Future<T>*`, `await E : T`. Legal **only inside an `async fn`**
  (top level uses `future_block`). New keywords: `async`, `await`.
- Calling an `async fn` without `await` yields `Future<T>*` (start now, await later,
  or hand to a combinator). If neither awaited nor handed off, the transform drops it
  on scope exit (§7).
- `future_drop(f)` is the explicit cancel entry; the transform also inserts it
  implicitly (§7).
- `future_block(lp, f)` drives the loop at the top level: sets `f.waker` to stop the
  loop, runs it, returns `f.value`, frees `f`.

---

## 9. Implementation order (each step independently testable)

0. **Atomic intrinsics** — `atomic_load`/`atomic_store`/`atomic_cas`/`atomic_swap`
   with acquire/release, lowering to LLVM atomics. Test in isolation; `<threading>`
   can adopt them too. *Prerequisite for the handshake.*
1. **`stdlib/future.esk`** — `Future<T>` + `FutureHdr` + `free_future` /
   `future_drop`, implementing the §3 atomic protocol. Hand-write a coroutine *by
   hand* in Eskiu against this contract — including a drop path **and a cross-thread
   completion** (worker thread completes; resume scheduled on a different executor)
   — and drive it with `<eventloop>`. Validates the runtime model, cancellation, and
   multi-thread end-to-end **before** touching the compiler. **← the de-risk gate.**
2. **Executor** — ready-queue + self-pipe/`eventfd` wakeup over `<eventloop>`;
   `current_executor()`, `spawn`. Hand-written.
3. **Leaf futures** — `net_connect_async`/`net_read_async` (`stdlib/net_async.esk`)
   with real `on_drop`. Hand-written.
4. **Lexer/parser** — `async` modifier, `await` expression, tokens.
5. **Type checker** — async return → `Future<T>*`; `await` typing; "await only in
   async"; track unconsumed `Future` locals for the drop pass.
6. **AST transform** — state-machine split (§4) + implicit `future_drop` (§7). Bulk
   of the work; inspectable via `--test-parser`.
7. **Codegen** — nearly free: the transform emits structs/switch/closures/casts/
   atomics codegen already handles.
8. **Tests** — the hand-written coroutine (step 1) is the oracle; the same logic in
   `async`/`await` must match, plus: a cancellation test (asserts the leaf fd was
   deregistered and frame freed) and a thread-affinity test (completion on worker,
   resume observed on the home executor's thread).

Step 1 is the gate: if a hand-written state machine over this contract cannot drive a
real non-blocking socket read, be cleanly cancelled, **and** resume on a different
thread than it completed on, the design is wrong — and we learn it before writing the
transform.

---

## 10. What is locked, what is free

**Locked now (compiler↔generated-code ABI — breaking these recompiles/rewrites every
async fn in existence):**

- `Future<T>` field set **and order**: `{ int state; fn()->void waker; fn()->void
  on_drop; T value; }` — `value` **last** so the header is type-erasable (§2.1).
- `FutureHdr` aliases the first three fields.
- `state` is **atomic**, four values `PENDING/WAITING/READY/CANCELLED`, with
  acquire/release ordering and the single-winner terminal swap (§3).
- The completion, drop, and arbitration protocols (§3) and the finalized-once
  invariant (§7).
- Completion goes through `waker()`, and `waker` lands the resume on the awaiter's
  home executor (the contract that makes thread-affinity work).

**Free to change later (touches only stdlib / the executor / the transform's
internals — no recompile of user async code):**

- Await *sources* (timers, joins, channels) — new leaf futures obeying §3.
- Frame layout (`awaiting`, `home`, state numbering, spilled locals).
- Executor internals: thread count, ready-queue, wakeup mechanism, work-stealing.
- `select`/`join` combinators (built on drop + completion).
- A cooperative cancellation token for cleanup-on-cancel.

Getting `value`-last, the atomic four-state handshake, `on_drop`, and the
home-executor waker contract into the locked set *before* any async code exists is
the entire reason for writing this note first.
