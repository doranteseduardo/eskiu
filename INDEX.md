# Eskiu Documentation Index

Complete guide to all Eskiu documentation. Use this to find what you need.

## Getting Started (For New Users)

Start here if you're new to Eskiu:

1. **[QUICKSTART.md](QUICKSTART.md)** — 5-minute first program (read this first)
2. **[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)** — Complete tutorial with examples
3. **[examples/README.md](examples/README.md)** — Real programs you can run and modify

**Time to first compile:** 5 minutes  
**Time to understand basics:** 30 minutes

---

## Building and Development

For developers setting up the environment:

1. **[docs/BUILD.md](docs/BUILD.md)** — Installation and build for all platforms
2. **[docs/DEBUGGING.md](docs/DEBUGGING.md)** — Compiler test modes and troubleshooting
3. **[docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)** — How to contribute to Eskiu

---

## Language Reference

Complete language documentation:

1. **[docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)** — Full syntax specification
2. **[docs/GLOSSARY.md](docs/GLOSSARY.md)** — Technical terms defined
3. **[docs/FAQ.md](docs/FAQ.md)** — Common questions and answers

---

## Advanced Topics

For systems programmers and architects:

1. **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — Compiler design and phases
2. **[docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md)** — Why certain language choices
3. **[docs/PERFORMANCE.md](docs/PERFORMANCE.md)** — Optimization and profiling guide
4. **[docs/PHASES.md](docs/PHASES.md)** — Development roadmap

---

## Quick Reference Table

| Document | Audience | Purpose | Time |
|----------|----------|---------|------|
| **[QUICKSTART.md](QUICKSTART.md)** | Everyone | First program | 5 min |
| **[RELEASES.md](RELEASES.md)** | Everyone | Version history | Reference |
| **[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)** | New users | Learn the language | 30 min |
| **[examples/README.md](examples/README.md)** | Learners | Real code | varies |
| **[README.md](README.md)** | Everyone | Project overview | 5 min |
| **[docs/BUILD.md](docs/BUILD.md)** | Developers | Installation | 10 min |
| **[docs/DEBUGGING.md](docs/DEBUGGING.md)** | Developers | Troubleshooting | Reference |
| **[docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)** | Contributors | How to contribute | 15 min |
| **[docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)** | Programmers | Full syntax | Reference |
| **[docs/GLOSSARY.md](docs/GLOSSARY.md)** | Everyone | Terms explained | Reference |
| **[docs/FAQ.md](docs/FAQ.md)** | Everyone | Common Q&A | Reference |
| **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** | Developers | How compiler works | 20 min |
| **[docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md)** | Architects | Why these choices | 15 min |
| **[docs/PERFORMANCE.md](docs/PERFORMANCE.md)** | Systems devs | Optimization | 20 min |
| **[docs/PHASES.md](docs/PHASES.md)** | Contributors | Roadmap | Reference |
| **[docs/API.md](docs/API.md)** | C++ users | Public API | Reference |
| **[docs/SETUP.md](docs/SETUP.md)** | Developers | Legacy: see BUILD.md | Deprecated |

---

## By Role

### I'm a Programmer Who Wants to Learn Eskiu

1. Read [QUICKSTART.md](QUICKSTART.md)
2. Follow [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
3. Run examples in [examples/](examples/)
4. Reference [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)

### I'm Setting Up the Compiler

1. Follow [docs/BUILD.md](docs/BUILD.md)
2. Verify build with [docs/DEBUGGING.md](docs/DEBUGGING.md)
3. Run [examples/](examples/) to test

### I'm Debugging a Compilation Error

1. Check [docs/DEBUGGING.md](docs/DEBUGGING.md)
2. Run `--test-lexer`, `--test-parser`, `--test-typechecker`
3. Check [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md) for syntax
4. See [docs/FAQ.md](docs/FAQ.md) for common errors

### I Want to Optimize My Code

1. Read [docs/PERFORMANCE.md](docs/PERFORMANCE.md)
2. Profile with `perf` or `instruments`
3. Reference [docs/GLOSSARY.md](docs/GLOSSARY.md) for terms

### I Want to Contribute to Eskiu

1. Read [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)
2. Understand [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
3. Check [docs/PHASES.md](docs/PHASES.md) for what needs doing
4. Read [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md) for philosophy

### I'm Curious About Design Choices

1. Read [README.md](README.md) for philosophy
2. Deep-dive [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md)
3. Understand [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## Search by Topic

### Memory Management
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#working-with-memory)
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md#stack-vs-heap)
- [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md#why-no-garbage-collection)
- [docs/FAQ.md](docs/FAQ.md#is-eskiu-memory-safe)

### Functions and Control Flow
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#understanding-the-language)
- [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)
- [examples/fibonacci.esk](examples/fibonacci.esk)

### Structs and Types
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#understanding-the-language)
- [examples/struct_usage.esk](examples/struct_usage.esk)
- [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)

### C Interoperability
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#understanding-the-language)
- [examples/hello.esk](examples/hello.esk)
- [docs/FAQ.md](docs/FAQ.md#can-i-call-c-functions-from-eskiu)

### Compilation and Testing
- [docs/DEBUGGING.md](docs/DEBUGGING.md#test-modes)
- [README.md](README.md#compiler-testing-modes)
- [docs/BUILD.md](docs/BUILD.md)

### Performance
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md)
- [docs/FAQ.md](docs/FAQ.md#how-fast-is-eskiu)
- [README.md](README.md#why-eskiu)

### Project Status
- [docs/PHASES.md](docs/PHASES.md) — Development roadmap
- [README.md](README.md#development-roadmap)

---

## Navigation Tips

1. **Use this index** to find the right doc
2. **Cross-links** between docs help you explore
3. **Glossary** ([docs/GLOSSARY.md](docs/GLOSSARY.md)) explains unfamiliar terms
4. **FAQ** ([docs/FAQ.md](docs/FAQ.md)) answers quick questions
5. **Examples** ([examples/](examples/)) show real code

---

## Document Map (File Structure)

```
├── INDEX.md                         ← You are here
├── README.md                        ← Project overview
├── QUICKSTART.md                    ← 5-min first program
├── RELEASES.md                      ← Version history & roadmap
│
├── .github/
│   └── ISSUE_TEMPLATE/
│       ├── bug_report.md            ← Bug report template
│       └── feature_request.md       ← Feature request template
│
├── docs/
│   ├── GETTING_STARTED.md          ← Full tutorial
│   ├── BUILD.md                     ← Installation guide
│   ├── DEBUGGING.md                 ← Troubleshooting
│   ├── LANGUAGE_SPEC.md             ← Full specification
│   ├── API.md                       ← C++ public API
│   ├── ARCHITECTURE.md              ← Compiler design
│   ├── PHASES.md                    ← Roadmap
│   ├── CONTRIBUTING.md              ← How to contribute
│   ├── FAQ.md                       ← Common questions
│   ├── GLOSSARY.md                  ← Terms defined
│   ├── DESIGN_DECISIONS.md          ← Why these choices
│   ├── PERFORMANCE.md               ← Optimization guide
│   └── SETUP.md                     ← Legacy (see BUILD.md)
│
└── examples/
    ├── README.md                    ← Example index
    ├── hello.esk
    ├── fibonacci.esk
    └── struct_usage.esk
```

---

## Latest Documentation Updates

- **June 2026:** Complete documentation overhaul (this version)
  - Added QUICKSTART.md for 5-minute onboarding
  - Added GETTING_STARTED.md with complete tutorial
  - Added BUILD.md with platform-specific instructions
  - Added DEBUGGING.md with test modes and troubleshooting
  - Added DESIGN_DECISIONS.md explaining language choices
  - Added GLOSSARY.md with 60+ terms
  - Added PERFORMANCE.md with optimization guide
  - Updated README.md to link to new docs
  - Updated CONTRIBUTING.md with current status

---

## Questions?

- Check [docs/FAQ.md](docs/FAQ.md) for quick answers
- Read [docs/DEBUGGING.md](docs/DEBUGGING.md) if something breaks
- File an issue on GitHub with context and what you've tried
- Reference this INDEX.md when asking for help

---

**Start with [QUICKSTART.md](QUICKSTART.md) — you'll have your first Eskiu program running in 5 minutes.**
