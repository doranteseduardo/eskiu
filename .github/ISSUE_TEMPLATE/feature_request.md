---
name: Feature Request
about: Suggest a new language feature or compiler improvement
title: "[FEATURE] Brief description"
labels: enhancement
assignees: ''

---

## Description
Clear description of the feature. What problem does it solve?

## Use Case
Real-world scenario where this feature would be useful. Example:

```esk
// What you'd like to write:
let x = 5;  // Instead of: let x: i32 = 5;
```

## Proposed Solution
How should this feature work? What would the syntax look like?

## Alternatives Considered
Other approaches you've considered and why this one is better.

## Impact
- **Breaks existing code?** (Yes/No)
- **Phase impact:** Which phase(s) would this affect? (Lexer, Parser, Type Checker, Codegen, etc.)
- **Examples:** Show before/after

## Related Issues
Link to related discussions or issues

---

**Note:** Before opening a feature request, check:
1. [LANGUAGE_SPEC.md](../../docs/LANGUAGE_SPEC.md) — Is this already documented?
2. [FAQ.md](../../docs/FAQ.md) — Has this been answered?
3. [PHASES.md](../../docs/PHASES.md) — Is this planned?
4. Existing issues — Has someone suggested this?

Feature requests are prioritized based on:
- Alignment with [DESIGN_DECISIONS.md](../../docs/DESIGN_DECISIONS.md)
- Impact on the QR decoder use case
- Phase roadmap
