# Eskiu Compiler C++ API Reference

This document describes the public C++ API of the Eskiu compiler components. It
is intended for developers embedding the compiler as a library or building
additional analysis and transformation passes on the existing infrastructure.

The compiler is built with C++17 and links against LLVM. All public headers live
under the project root; include paths are relative to it. The pipeline is a chain
of four passes (lex, parse, type-check, code-generate) with an async-lowering
pass between type checking and code generation. Each pass is an independent class
that can be driven on its own.

---

## Lexer

Header: `lexer/lexer.h`

```cpp
class Lexer {
public:
    explicit Lexer(const std::string& source,
                   std::map<std::string, Macro>* macros = nullptr,
                   const std::string& filename = "");

    Token next_token();          // produce the next token; returns EOF_TOKEN at end
    void  print_all_tokens();    // debugging dump (drives --test-lexer)

    bool hadError = false;       // set on a lexical error; the driver aborts before parsing
};
```

The lexer runs a small preprocessor pass before tokenizing. The optional `macros`
table is shared across files so `#define`s propagate through `import` and
multi-file builds; `filename` is exposed to the preprocessor as `__FILE__`.

A streaming lexer: call `next_token()` until it returns a `Token` of type
`TokenType::EOF_TOKEN`. Callers collect the tokens into a `std::vector<Token>` for
the parser (pushing the final EOF token themselves).

### Token / TokenType

```cpp
enum class TokenType { /* keywords, operators, delimiters, literals, special */ };

struct Token {
    TokenType   type;
    std::string value;
    int         line;
    int         column;

    Token(TokenType type = TokenType::UNKNOWN, const std::string& value = "",
          int line = 0, int column = 0);
};

std::string tokenTypeToString(TokenType type);   // free function
```

`TokenType` enumerates every category the lexer recognizes: keywords (`INT`,
`RETURN`, `ASYNC`, `AWAIT`, `MATCH`, `ENUM`, `INTRINSIC`, …), operators (`EQ`,
`EQEQ`, `LSHIFT`, …), delimiters (`LBRACE`, `LBRACKET`, `ARROW`, `RANGE`, …),
literals (`INT_LIT`, `FLOAT_LIT`, `STRING_LIT`, `CHAR_LIT`, `IDENT`), and the
specials `PRAGMA`, `EOF_TOKEN`, `UNKNOWN`. Every `Token` carries its source
location (1-based line and column).

A `Macro` is the preprocessor's record of a `#define` (object-like or
function-like) with its parameter names and body text.

---

## Parser

Header: `parser/parser.h`

```cpp
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    std::shared_ptr<Program> parse();   // returns nullptr if any declaration failed

    bool hadError = false;              // set when a declaration/import failed to parse

    std::string                basedir;             // dir of the current file (relative imports)
    std::string                stdlibPath;          // stdlib root (<module> imports)
    std::set<std::string>*     importedFiles = nullptr;   // shared dedup set
    std::map<std::string,Macro>* macros = nullptr;        // shared macro table
    std::set<std::string>*     sharedTypeNames = nullptr; // type names across all files
};
```

A hand-written recursive-descent parser that builds the AST. `parse()` consumes
the token vector and returns a `std::shared_ptr<Program>`; on a parse failure it
prints diagnostics to stderr, sets `hadError`, and returns `nullptr`, so callers
fail loudly rather than silently dropping code.

The public fields wire up multi-file compilation: `basedir` and `stdlibPath`
resolve `import "file.esk"` and `import <module>` respectively; `importedFiles`,
`macros`, and `sharedTypeNames` are shared with the sub-parsers spawned for
imported files so deduplication, macros, and declared type names propagate across
the whole compilation.

---

## Type Checker

Header: `sema/type_checker.h`

```cpp
class TypeChecker : public ASTVisitor {
public:
    TypeChecker();

    bool check(Program* program);          // entry point; returns false if errors were found

    std::string getExpressionType(Expr* expr);

    bool warnAll   = false;                // -Wall: lint warnings
    bool warnExtra = false;                // -Wextra: signed/unsigned mismatch, etc.

    // LSP / tooling
    std::string sourceFile = "unknown";    // file name for diagnostics
    std::string getTypeAtPosition(int line, int col) const;   // --hover-at
    std::string getDefinitionAt(int line, int col) const;     // --definition-at
};
```

`check()` takes a raw `Program*` (it does not own the AST) and runs a two-pass
semantic analysis: a first pass registers top-level declarations (functions,
structs, templates, interfaces, enums, type aliases) so order is irrelevant, and
a second pass type-checks bodies. It returns `false` if any error was reported,
and accumulates errors with `file:line:col: message` locations. `warnAll` and
`warnExtra` enable lint diagnostics.

The tooling interface backs the editor integration: after a successful `check()`,
`getTypeAtPosition(line, col)` returns the inferred type of the expression (or
declared name) under a 1-based cursor, and `getDefinitionAt(line, col)` returns
the definition location of the symbol there. `getExpressionType(Expr*)` returns
the cached type of an already-visited expression node.

As an `ASTVisitor`, the type checker overrides a `visit` method for every node
kind (declarations, statements, expressions); these are the traversal mechanism
and are not normally called directly.

---

## Async Transform

Header: `sema/async_transform.h`

```cpp
class AsyncTransform {
public:
    void run(Program* program);
};
```

Lowers `async` functions and `await` into a resumable state machine. It runs after type
checking and before code generation: each async function is replaced by a frame
struct, a `__<name>_resume` function (an if-chain over the resume state), and a
constructor that returns `*Future<T>`. Awaits are lowered across all control flow
(`if`/`else`, `while`, C-style `for`, `switch`, and `for`-`in`), including
`break`/`continue` and early `return`. The output is ordinary Eskiu AST that
normal code generation handles, so the pass mutates the `Program` in place and
returns nothing.

---

## Code Generation

Header: `codegen/codegen.h`

```cpp
class CodeGen : public ASTVisitor {
public:
    CodeGen();
    ~CodeGen();

    llvm::Module* generateCode(std::shared_ptr<Program> program);  // null on failure
    llvm::Module* getModule() const;                               // non-owning

    std::string targetTriple;     // target triple override (empty = native)
    bool freestanding = false;    // alloc/free → esk_alloc/esk_free instead of malloc/free
    bool asan  = false;           // AddressSanitizer instrumentation
    bool ubsan = false;           // trapping bounds checks
    unsigned optLevel = 0;        // -O level; 1..3 run the LLVM middle-end (0 = none)

    void printIR() const;                            // print IR to stdout (--test-codegen)
    void optimizeModule();                           // run the middle-end pipeline at optLevel
    bool emitObjectFile(const std::string& filename); // write a native object file
};
```

`generateCode()` walks the AST via the visitor pattern and emits LLVM IR through
an `IRBuilder`, returning the populated `llvm::Module*` (owned by the `CodeGen`
instance; `nullptr` on failure). `getModule()` returns the same module
non-owningly.

Configure the run before calling `generateCode()`:

- `targetTriple`: set to cross-compile for a given triple; empty targets the host.
- `freestanding`: predefines `__ESKIU_FREESTANDING__` and routes heap allocation
  to user-supplied `esk_alloc`/`esk_free`.
- `asan` / `ubsan`: apply the corresponding LLVM instrumentation pass to the
  module before object emission.
- `optLevel` / `optimizeModule()`: `-O1`..`-O3` run the LLVM middle-end (mem2reg, SROA, inlining, GVN) over the module before object emission; `0` skips it (the default, naive IR straight to the backend).

`printIR()` prints the module's IR to stdout (valid before the module's ownership
is transferred away). `emitObjectFile(filename)` lowers the module to a native
object file for `targetTriple` (or the host) and writes it to `filename`,
returning `true` on success.

---

## Pipeline Example

The passes compose as the driver (`main.cpp`) uses them:

```cpp
// 1. Lex
Lexer lexer(source, &macros, filename);
std::vector<Token> tokens;
for (Token t = lexer.next_token(); ; t = lexer.next_token()) {
    tokens.push_back(t);
    if (t.type == TokenType::EOF_TOKEN) break;
}
if (lexer.hadError) return 1;

// 2. Parse
Parser parser(tokens);
parser.basedir = ...; parser.stdlibPath = ...;
std::shared_ptr<Program> program = parser.parse();
if (!program) return 1;   // parse failure

// 3. Type-check
TypeChecker checker;
checker.sourceFile = filename;
if (!checker.check(program.get())) return 1;   // raw pointer; checker does not own the AST

// 4. Lower async → state machine
AsyncTransform().run(program.get());

// 5. Code-generate
CodeGen codegen;
codegen.targetTriple = ...;          // optional
codegen.freestanding = freestanding; // optional
if (!codegen.generateCode(program)) return 1;   // shared_ptr
codegen.emitObjectFile("out.o");     // or codegen.printIR();
```

Note the ownership boundary: the parser produces a `std::shared_ptr<Program>`;
`TypeChecker::check` and `AsyncTransform::run` take a raw `Program*` (they do not
own it); `CodeGen::generateCode` takes the `shared_ptr`.
