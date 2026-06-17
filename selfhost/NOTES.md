# Self-hosting dogfood notes

Pain points and compiler findings surfaced while writing the lexer in Eskiu.
Per the milestone plan we record them here (feature freeze holds — these are for a
later hardening/ergonomics pass, not new features). Ordered worst-first.

## 1. Silent miscompile: a helper's `return <param>` fall-through yields 0  ⚠️ correctness

**Severity: high (silent wrong codegen, no diagnostic).**

In the full `selfhost/lex_main.esk` module, a small helper

```
int decode_char_escape(int e) {
    if (e == 110) { return 10; }   // these literal-return branches WORK
    if (e == 116) { return 9; }
    if (e == 92)  { return 92; }
    return e;                      // <-- this fall-through returns 0, not e
}
```

returns **0** for any input that reaches the final `return e` (e.g. 114='r',
120='x', 48='0'), while inputs that hit a literal-return branch are correct
(`decode_char_escape(110)==10`). Confirmed by probing from both `main` and the
caller (`emit_value`).

Not name-specific (renaming to `decode_chr_escape` reproduces) and not
caller-specific (fails when called directly from `main`). It is NOT reproduced by:
- the same body as a standalone program,
- the same body as a 1st helper (the sibling `decode_str_escape`, defined just
  above with more branches, works — including ITS `return e` fall-through, e.g.
  `decode_str_escape(120)==120`),
- the same body added as a *3rd* helper/clone (works),
- a 2-helper toy that imports `lexer.esk`.

So the trigger needs the full module shape (both decode helpers + `emit_value`'s
3 branches that call them + the imported lexer/ctype/tokens function set). Not yet
minimized to a small standalone repro — the real file is the reliable reproducer
(see git history at the stage-4 commit, before the inline workaround).

**Workaround in use:** inline the char-escape decode into `emit_value` with a `dv`
accumulator (`int dv = e; if (...) dv = ...; putchar(dv)`) — i.e. never return the
parameter through a helper. The string path keeps `decode_str_escape` (it works).

**Investigate:** smells like a codegen aliasing / wrong-slot issue keyed by the
function's position/role in the module, surfacing only past some module-shape
threshold. A target for the fuzzer (parameter-passthrough return across many
small `int(int)` functions in one module) and the golden-IR oracle.

## 2. Parser: an `if`/`else` nested inside an `else { ... }` block fails to parse

```
if (a) { ... } else { if (b) { ... } else { ... } }   // -> "Expected expression, got ELSE"
```

A *braced* `else` block whose body is itself an `if`-with-`else` errors out at the
inner `else`. Note `else if (...)` (no braces) parses fine — so this is specific to
the `else { if ... else ... }` shape, not chained conditionals in general.
Workaround: use `else if`, flat sequential `if`s with early `return` (see
`keyword_type`), or an accumulator variable.
