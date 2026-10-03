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

```eskiu
// What you'd like to write:
let x: int = 5;
```

## Proposed Solution
How should this feature work? What would the syntax look like?

## Alternatives Considered
Other approaches you've considered and why this one is better.

## Impact
- **Breaks existing code?** (Yes/No)
- **Compiler stages:** Which ones would this affect? (Lexer, Parser, Type Checker, Codegen, stdlib)
- **Examples:** Show before/after

## Related Issues
Link to related discussions or issues

---

**Note:** Before opening a feature request, check:
1. [Language specification](https://github.com/doranteseduardo/eskiu/blob/develop/docs/lang/spec.md): Is this already documented?
2. [Design decisions](https://github.com/doranteseduardo/eskiu/blob/develop/docs/dev/design.md): Does it align with the language's goals?
3. [Roadmap](https://github.com/doranteseduardo/eskiu/blob/develop/docs/dev/phases.md): Is this already planned?
4. Existing issues: Has someone suggested this?

Feature requests are weighed against the design decisions in `docs/dev/design.md` and the
roadmap. A proposal that fills a gap people hit in real code is easier to take on than new
language surface.
