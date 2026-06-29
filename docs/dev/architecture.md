# Eskiu Compiler — Architecture Reference

Internal reference for compiler contributors. Assumes familiarity with C++17 and LLVM IRBuilder.

---

## Pipeline Overview

```
Source (.esk)
    │
    ▼
┌─────────┐   vector<Token>
│  Lexer  │──────────────────────────────────────────►
└─────────┘                                           │
 lexer/lexer.cpp                                      ▼
 lexer/preprocessor.cpp                        ┌──────────┐   shared_ptr<Program>
                                               │  Parser  │──────────────────────►
                                               └──────────┘                       │
                                       parser/parser.cpp                          ▼
                                       parse_{decl,stmt,expr}.cpp          ┌──────────────┐
                                                                          │ Type Checker  │── errors → stderr
                                                                          └──────────────┘
                                                          sema/type_checker.cpp + typecheck_{decl,stmt,expr,type}.cpp
                                                          sema/type.{h,cpp}  (ty::Type IR)
                                                                                  │ validated AST
                                                                                  ▼
                                                                          ┌────────────────┐
                                                                          │ Async transform │  async fn → frame + resume + Future ctor
                                                                          └────────────────┘
                                                                          sema/async_transform.cpp
                                                                                  │ transformed AST
                                                                                  ▼
                                                                          ┌──────────────┐
                                                                          │ Type Checker  │  RE-RUN: single resolver →
                                                                          │   (re-run)    │  per-expression ty::Type table
                                                                          └──────────────┘
                                                                                  │ AST + resolved type table
                                                                                  ▼
                                                                          ┌─────────┐   llvm::Module*
                                                                          │ Codegen │────────────────► .o file → Binary
                                                                          └─────────┘   (consumes the type table; does NOT
                                                                        codegen/codegen_*.cpp  re-derive expression types)
```

| Stage | File(s) | Responsibility | Status |
|---|---|---|---|
| Preprocessor | `lexer/preprocessor.cpp` (`preprocess`, declared in `preprocessor.h`) | Text pass run inside the `Lexer` constructor: object-/function-like `#define`, `#ifdef`/`#ifndef`/`#else`/`#endif`; shared macro table propagates across `import`/multi-file; blanks directive/skipped lines to preserve line numbers | Complete |
| Lexer | `lexer/lexer.cpp`, `lexer/lexer.h` | Converts the preprocessed source into a flat `vector<Token>` stream with line/column positions | Complete |
| Parser | `parser/` — `parser.cpp` (core) + `parse_{decl,stmt,expr}.cpp` | Recursive-descent; produces a `shared_ptr<Program>` AST; resolves `import` inline | Complete |
| Type Checker | `sema/type_checker.cpp` + `typecheck_{decl,stmt,expr,type}.cpp`; the structured `ty::Type` IR in `sema/type.{h,cpp}` | Two-pass visitor; registers structs/interfaces/functions then validates types and scopes; resolves every expression's type into a table | Complete |
| Async transform | `sema/async_transform.cpp` | Rewrites each `async fn` into a frame struct + `__name_resume` + a `*Future<T>` constructor — ordinary AST that normal codegen handles | Complete |
| Codegen | `codegen/codegen_{module,type,scope,decl,stmt,expr,call,closure,adt}.cpp`, `codegen.h` | Visitor over the validated AST; emits LLVM IR via `IRBuilder<>`; emits native `.o` | Complete |

**How they compose.** `main.cpp` runs each stage in sequence. The lexer is driven to exhaustion first — the full token stream is materialized into `std::vector<Token>` and handed to `Parser`. The parser returns `shared_ptr<Program>`. The type checker takes `Program*` and walks the tree through the visitor interface. The async transform then rewrites the AST. The type checker is **re-run on the transformed AST** and its resolved per-expression types are handed to codegen — so the type checker is the single resolver and codegen consumes its types rather than re-deriving them. Codegen takes `shared_ptr<Program>`, generates IR, verifies it with `llvm::verifyModule`, and calls `emitObjectFile()` which uses `llvm::TargetMachine` + `legacy::PassManager` to produce the `.o`. None of the stages modify the AST after the transform; they only read it and produce output.

**Tooling CLI flags.** `--hover-at LINE:COL` runs the full pipeline through the type checker, then walks `program->declarations` to find the innermost AST node whose source range contains the given position and prints its inferred Eskiu type (from `expressionTypes`). `--definition-at LINE:COL` similarly finds the symbol at the given position and prints the `file:line:col` where that symbol was declared (from `functionSignatures`, `structs`, or the scope where the `VarDecl` was registered). Both flags are consumed by the VS Code extension for hover tooltips and go-to-definition navigation.

---

## Visitor Pattern

### Why all passes implement ASTVisitor

Every analysis or code generation pass implements `ASTVisitor`. This keeps traversal logic out of the AST nodes themselves and allows passes (type checker, codegen, AST printer) to be added without modifying `ast/ast.h`. The base class declares one pure-virtual `visit()` overload per concrete node type (42 today), so a new node type forces every pass to handle it or fail to compile.

### How accept()/visit() dispatch works

Each concrete AST node overrides `accept(ASTVisitor* v)` and calls `v->visit(this)`. This is classic double-dispatch: the node's static type selects the `visit()` overload at compile time via the node's own `accept` implementation.

```cpp
// Every node follows this pattern in ast/ast.cpp:
void BinaryExpr::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}
```

The visitor declares a pure-virtual `visit()` overload for every concrete node type. Any new concrete node must add an overload to `ASTVisitor` and stub it in all existing implementors.

### How to add a new node (5-step checklist)

1. Declare the class in `ast/ast.h`, inheriting from `Expr`, `Stmt`, or `Decl`. Add the `accept` override declaration and `int line = 0; int col = 0;` (inherited from `ASTNode`).
2. Implement `accept` in `ast/ast.cpp`: `visitor->visit(this);`.
3. Add `virtual void visit(MyNewNode* node) = 0;` to `ASTVisitor` in `ast/ast.h`.
4. Add an implementation to every `ASTVisitor` subclass: `ASTPrinter`, `TypeChecker`, `CodeGen`.
5. Wire the parse rule in `parser/parser.cpp` to construct and return the new node. Stamp the position with `withPos(node, tok)` so `node->line` and `node->col` are populated.

---

## Lexer (`lexer/`)

### What `Lexer::next_token()` returns

A single `Token` struct with four fields:

```cpp
struct Token {
    TokenType type;    // enum class value
    std::string value; // raw text ("42", "0xFF", "hello", "+", ...)
    int line;          // 1-based
    int column;        // 1-based
};
```

The caller drives the lexer by calling `next_token()` in a loop until `TokenType::EOF_TOKEN` is returned. There is no internal lookahead buffer; each call consumes exactly one token. Comments (`//` and `/* */`) are skipped transparently inside `next_token()`.

### TokenType enum categories

| Category | Tokens |
|---|---|
| Type keywords | `INT`, `INT8`, `INT16`, `INT32`, `INT64`, `UINT`, `UINT8`, `UINT16`, `UINT32`, `UINT64`, `FLOAT`, `DOUBLE`, `BOOL`, `CHAR`, `STRING`, `VOID` |
| Control keywords | `FOR`, `WHILE`, `IF`, `ELSE`, `SWITCH`, `CASE`, `DEFAULT`, `BREAK`, `CONTINUE`, `RETURN` |
| Declaration keywords | `LET`, `STRUCT`, `INTERFACE`, `EXTERN`, `FN`, `IMPORT`, `ENUM` |
| Memory keywords | `ALLOC`, `FREE` |
| Literal keywords | `TRUE`, `FALSE`, `NULL_KW` |
| Exception keywords | `TRY`, `CATCH`, `FINALLY`, `THROW` |
| Concurrency keywords | `THREAD_CREATE`, `THREAD_JOIN` |
| Other keywords | `SIZEOF`, `VOLATILE`, `ASM` |
| Reserved (future) | `THREAD`, `SPAWN`, `MUTEX`, `IN` |
| Arithmetic operators | `PLUS`, `MINUS`, `STAR`, `SLASH`, `PERCENT` |
| Compound assignments | `PLUS_EQ`, `MINUS_EQ`, `STAR_EQ`, `SLASH_EQ`, `PERCENT_EQ`, `AMP_EQ`, `PIPE_EQ`, `CARET_EQ`, `LSHIFT_EQ`, `RSHIFT_EQ` |
| Comparison | `EQEQ`, `NE`, `LT`, `GT`, `LE`, `GE` |
| Assignment | `EQ` |
| Logical | `AND` (`&&`), `OR` (`\|\|`), `NOT` (`!`) |
| Bitwise binary | `AMPERSAND` (`&`), `PIPE` (`\|`), `CARET` (`^`), `LSHIFT` (`<<`), `RSHIFT` (`>>`) |
| Bitwise unary | `TILDE` (`~`) |
| Delimiters | `LBRACE`, `RBRACE`, `LPAREN`, `RPAREN`, `LBRACKET`, `RBRACKET`, `SEMICOLON`, `COMMA`, `DOT`, `COLON`, `ARROW`, `ELLIPSIS` |
| Literals | `INT_LIT`, `FLOAT_LIT`, `STRING_LIT`, `CHAR_LIT` |
| Names | `IDENT` |
| Special | `EOF_TOKEN`, `UNKNOWN` |

The keywords map is a `static std::unordered_map<std::string, TokenType>` with 45 entries initialized at program start. After `read_identifier()` accumulates an alphanumeric/underscore run, it does a single map lookup; any identifier not in the map becomes `IDENT`.

### Hex literal tokenization

`read_number()` detects the `0x`/`0X` prefix after consuming the leading `0` digit. It then accumulates `std::isxdigit()` characters and returns `TokenType::INT_LIT` with the full raw string (e.g. `"0xFF"`). `visit(LiteralExpr*)` in codegen calls `std::stoll(value, nullptr, 0)` (base 0 = auto-detect) to parse both decimal and hex correctly.

### Line/column tracking

`advance()` increments `column` on every character consumed. On `\n` it resets `column = 1` and increments `line`. Both are initialized to `1`. Each token captures `line`/`column` before any characters of that token are consumed.

---

## Parser (`parser/`)

### Recursive descent with controlled backtracking via savePos/catch

The parser is purely recursive descent with no grammar table and no PEG memoization. Lookahead is limited to `peek()` (current token) and `peek_ahead(1)` (one token ahead). Backtracking occurs in three controlled places, each marked `size_t savePos = current`:

- `parseDeclaration()` — speculatively parses the type and checks for `LPAREN` or `LT` after the name to distinguish function declaration from variable declaration; resets `current = savePos` and re-parses if it is a function.
- `parseBlockStatement()` — tries `parseDeclaration()` in a `try/catch(...)`; resets `current = savePos` on throw and falls through to `parseStatement()`.
- `parseUnary()` — attempts a cast `(TYPE) expr` speculatively; resets on failure (only triggered when the token inside the parenthesis is an unambiguous type keyword).
- `parsePostfix()` — speculatively parses template call `ident<T,...>(args)` by trying to consume `<`, type args, `>`, `(`; resets on failure to avoid misinterpreting `<` as less-than.

### `parseType()`: leading `*T`, trailing `T*`, array `T[N]`, template `Name<T,...>`

```
parseType()
  1. consume any leading STAR tokens, count them (leading_pointers)
  2. consume one type keyword or IDENT
  3. if IDENT and next is '<': recursively parse comma-separated type args until '>'
     → type += "<arg1,arg2,...>"
  4. consume any trailing STAR tokens, append "*" for each
  5. prepend "*" for each leading star
  6. consume "[" size_tokens "]" sequences, append "[N]" to type string
     → array size is captured (not discarded) e.g. "uint8[858]"
```

The resulting type string examples: `"*Point"`, `"int**"`, `"uint8[858]"`, `"Result<int,string>"`, `"List<int>*"`.

### Template parsing

**Template struct:** `struct Result<T, E> { ... }` — `parseStructDecl()` checks for `LT` after the struct name and accumulates `typeParams` as a `vector<string>` of identifier names. Stored in `StructDecl::typeParams`.

**Template function:** `T max<T>(T a, T b) { ... }` — `parseFunctionDecl()` checks for `LT` after the function name and accumulates `typeParams`. Stored in `FunctionDecl::typeParams`.

**Template call:** `max<int>(3, 5)` — parsed in `parsePostfix()`: when the current expression is an `IdentExpr` and the next token is `LT`, the parser speculatively tries to consume `<TypeArgs...>(args)` and builds a `TemplateCallExpr`. Falls back on failure.

**Template type in `parseType()`:** `Result<int, string>` in type position is handled directly by `parseType()` which recurses for each type argument.

### Struct init syntax: `IDENT { [field: expr | expr] }`

`parsePrimary()` detects `IDENT` followed by `LBRACE` and calls `parseStructInit(name)`. Inside:
- Named: `IDENT COLON expr` → field_name is non-empty
- Positional: `expr` → field_name is `""`

Both forms produce `StructInitExpr` with `vector<pair<string, ExprPtr>>`.

### Operator precedence chain

From lowest to highest:

```
parseAssignment      =  +=  -=  *=  /=  %=     (right-associative; compound ops desugar)
  parseLogicalOr     ||
    parseLogicalAnd  &&
      parseBitwiseOr     |
        parseBitwiseXor  ^
          parseBitwiseAnd    &
            parseEquality    ==  !=
              parseComparison    <  >  <=  >=
                parseShift       <<  >>
                  parseAddition  +  -
                    parseMultiplication  *  /  %
                      parseUnary         -  ~  !  &  *  (TYPE)
                        parsePostfix     ()  []  .  <T>(args)
                          parsePrimary
```

Compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`) are desugared in `parseAssignment()` into `BinaryExpr(lhs, "=", BinaryExpr(lhs, op, rhs))`.

### `import` handling: inline parsing in `parseProgram()`

When `parseProgram()` encounters `IMPORT STRING_LIT SEMICOLON`, it:
1. Resolves the path relative to `basedir` (the directory of the importing file).
2. Checks `importedFiles` (`set<string>`) for deduplication; skips if already imported.
3. Reads and lexes the imported file, creates a sub-`Parser` with the sub-file's `basedir` and the shared `importedFiles` set.
4. Calls `sub.parse()` and splices the resulting declarations directly into the current `declarations` vector.

This means import is purely textual flattening — all declarations from imported files land in the same `Program` node as if they were written inline.

### For-loop declaration init

`parseForStatement()` tries `parseDeclaration()` for the init clause. On success it wraps the `DeclPtr` in a `BlockStmt` containing a single `BlockItem`. On failure it resets `current` and tries `parseExpressionStatement()`. In the type checker, `visit(ForStmt*)` detects this `BlockStmt` wrapper and iterates its items directly in the for-loop's scope so the init variable is visible in the condition, step, and body.

---

## AST (`ast/`)

### Node hierarchy

```
ASTNode                    (line:int, col:int)
├── Decl                   (name:string)
│   ├── FunctionDecl       (returnType, params[(type,name)], body:StmtPtr, typeParams[], isAsync)
│   ├── VarDecl            (type:string, initializer:ExprPtr?)
│   ├── StructDecl         (fields[{type,name}], methods[DeclPtr], typeParams[], isPacked)
│   ├── ExternDecl         (returnType, params[(type,name)])
│   ├── IntrinsicDecl      (returnType, params[(type,name)])  — lowers to inline IR, no C symbol
│   ├── InterfaceDecl      (methods[{returnType,name,params}])
│   ├── EnumDecl           (members[{name,value?}], variants[payload fields]) — int enums + ADTs
│   ├── UnionDecl          (fields[{type,name}])
│   └── TypeAliasDecl      (name, underlying:string)
├── Stmt
│   ├── BlockStmt          (items: vector<BlockItem>)
│   ├── IfStmt             (condition, thenBranch, elseBranch?)
│   ├── ForStmt            (init:StmtPtr?, condition:ExprPtr?, step:ExprPtr?, body)
│   ├── ForInStmt          (var, iterable, body)  — for-each / range; desugared to ForStmt
│   ├── WhileStmt          (condition, body)
│   ├── ReturnStmt         (value:ExprPtr?)
│   ├── BreakStmt
│   ├── ContinueStmt
│   ├── SwitchStmt         (subject:ExprPtr, cases[{value:ExprPtr?,stmts[StmtPtr]}])
│   ├── MatchStmt          (subject:ExprPtr, arms[{variant,bindings,body}], default?)
│   ├── ExprStmt           (expr)
│   ├── AsmStmt            (template, outputs, inputs, clobbers)
│   ├── ThreadJoinStmt     (handle:ExprPtr)
│   ├── ThrowStmt          (value:ExprPtr)
│   └── TryStmt            (body, catches[{type,name,body}], finallyBody?)
└── Expr
    ├── BinaryExpr         (left, op:string, right)
    ├── UnaryExpr          (op:string, operand)
    ├── QuestionExpr       (operand)  — `?` error-propagation for Result<T,E>
    ├── CallExpr           (callee:ExprPtr, args[ExprPtr])
    ├── IndexExpr          (base, index)
    ├── MemberExpr         (base, member:string)
    ├── CastExpr           (targetType:string, expr)
    ├── LiteralExpr        (kind:{INT,FLOAT,STRING,CHAR,BOOL,NULL_VAL}, value:string)
    ├── IdentExpr          (name:string)
    ├── StructInitExpr     (structName, fieldInits[(name,ExprPtr)])
    ├── AllocWithExpr      (allocator:ExprPtr, elemType:string, count:ExprPtr)
    ├── TemplateCallExpr   (templateName, typeArgs[], args[])
    ├── LambdaExpr         (returnType:string, params[(type,name)], body:StmtPtr, escapes, captures[])
    ├── SizeofExpr         (type:string)
    ├── ThreadCreateExpr   (fn:ExprPtr)
    ├── FreeClosureExpr    (operand:ExprPtr)  — frees an escaping closure's heap env
    └── AwaitExpr          (operand:ExprPtr)  — requires *Future<T>, yields T

Program                    (declarations: vector<DeclPtr>)  — root node
```

`ASTVisitor` declares 42 pure-virtual `visit()` overloads (one per concrete class: `Program`, 9 `Decl` subtypes, 15 `Stmt` subtypes, 17 `Expr` subtypes). The `alloc<T>(n)` / `free` primitives are stdlib generic functions (`<mem>`), not AST nodes; the explicit-allocator form `alloc_with(&a, T, n)` is the `AllocWithExpr` node.

`LambdaExpr` carries the full function signature and body inline. During codegen, `visit(LambdaExpr*)` saves the current insert point, emits a new private `llvm::Function` with a synthesized unique name (e.g. `__lambda_0`, `__lambda_1`), restores the insert point, and pushes the `llvm::Function*` onto `exprValueStack` as the expression value. The `fn(T,...)->R` type is stored in `expressionTypes` for that node so the type checker can validate assignments and call sites.

### `BlockItem = std::variant<DeclPtr, StmtPtr>` — why unified

C allows variables to be declared at any point in a block, not just at the top. Rather than forcing declarations to precede statements (Pascal-style) or treating `VarDecl` as a `Stmt` subtype, `BlockStmt` uses `std::variant<DeclPtr, StmtPtr>`. Visitors iterate over items and dispatch with `std::holds_alternative` / `std::get`. This preserves the semantic distinction between declarations and statements throughout all passes.

### Source location on AST nodes

`ASTNode` carries `int line = 0; int col = 0;`. The parser populates these via the `withPos(node, tok)` helper, which assigns `node->line = tok.line; node->col = tok.column`. Positions are stamped on expressions at operator/delimiter tokens. Nodes constructed internally (e.g. the desugared `BinaryExpr` inside a compound assignment) are stamped with the compound-operator token. Nodes without an explicit `withPos` call retain `0/0`.

### Smart pointer conventions

All AST nodes are heap-allocated and managed through `shared_ptr`. Type aliases: `ASTNodePtr`, `ExprPtr`, `StmtPtr`, `DeclPtr`. `shared_ptr` is used (rather than `unique_ptr`) because some node subtrees are genuinely **co-owned**: codegen synthesizes a `StructName_methodName` `FunctionDecl` that shares the original method's `body`, and template instantiation creates one `FunctionDecl` per concrete type that shares the template's `body` (`make_shared<FunctionDecl>(..., fd->body)`). Single ownership would require deep-cloning those bodies first. To keep reference counting cheap given this design, node constructors take their `Ptr` arguments by value and `std::move` them into members, and read-only helpers take `const ExprPtr&` rather than `ExprPtr` by value.

---

## Type Checker (`sema/`)

### Two-pass strategy

`TypeChecker::check(Program*)` runs two sequential passes over `program->declarations`:

**Pass 1 — registration only, no type checking:**
- `InterfaceDecl` → insert into `interfaceDecls` map
- `StructDecl` (template) → insert into `templateDecls` map
- `StructDecl` (concrete) → insert into `structs` map (`map<string, StructInfo>`); register methods as mangled functions `StructName_methodName` with implicit `*StructName` self parameter
- `FunctionDecl` (template) → insert into `funcTemplateDecls`; skip normal registration
- `FunctionDecl` (concrete) → insert into `functionSignatures`
- `ExternDecl` → insert into `functionSignatures`

**Pass 2 — full visitor traversal:**
Each declaration calls `decl->accept(this)` which dispatches to the appropriate `visit()`. Template function bodies are skipped at declaration time (`visit(FunctionDecl*)` returns immediately if `typeParams` is non-empty); they are type-checked lazily when instantiated via `visit(TemplateCallExpr*)`.

This two-pass design lets the type checker accept forward references: functions can call functions declared later, and struct fields can reference structs declared in any order. Codegen upholds the same guarantee through its three-phase program lowering (see *Three-phase program lowering* under Codegen) — both passes must agree, or a program that type-checks would fail to generate.

### Scope stack: `vector<map<string,Symbol>>`

```cpp
struct Symbol { std::string type; bool isDeclared; };
std::vector<std::map<std::string, Symbol>> scopes;
```

`pushScope()` appends an empty map. `popScope()` removes the last. `defineSymbol(name, type)` inserts into `scopes.back()`. `lookupSymbol(name)` iterates `scopes.rbegin()` to `scopes.rend()` — innermost scope first. Returns `""` on miss.

Scope boundaries: global scope in constructor; function body pushed/popped in `visit(FunctionDecl*)`; block pushed/popped in `visit(BlockStmt*)`; for-loop init scope pushed/popped in `visit(ForStmt*)`.

### Template instantiation via `normalizeType()`

`normalizeType(type)` is the entry point for lazy template struct instantiation:

1. If `type` has a trailing `*`: strip, normalize base recursively, re-append `*`.
2. If `type` starts with `struct:` or `interface:`: return as-is.
3. If `type` contains `<`: split into template name + args via `splitTemplateType()`. Look up `templateDecls`. If found and not already in `structs`, create a substituted `StructInfo` (applying `substType()`) and insert it as `structs[mangled]`. Return `"struct:" + mangled` where `mangled` = `"Result_int_string"` for `"Result<int,string>"`.
4. If `type` is a known concrete struct name: return `"struct:" + type`.
5. Otherwise return as-is (primitive).

Template function instantiation happens in `visit(TemplateCallExpr*)`: type args are bound to type params via `substType()`, and the return type is computed. Bodies are not re-type-checked; only argument types are validated.

### Interface structural satisfaction check

`isValidAssignment(lhs, rhs)` includes an interface check: if `lhs` is a registered interface name, it calls `structSatisfiesInterface(functionSignatures, structName, iface)` which verifies that for every method in the interface, a function named `structName_methodName` exists in `functionSignatures`. No `implements` keyword is required. This is purely structural.

### Error format: `sourceFile:line:col: message`

```cpp
void TypeChecker::error(int line, int col, const std::string& message) {
    hasErrors = true;
    std::stringstream ss;
    ss << sourceFile << ":" << line << ":" << col << ": " << message;
    errors.push_back(ss.str());
}
```

`sourceFile` is set by the caller (`main.cpp`) to the actual input filename before calling `check()`. Errors are accumulated and printed after the full second pass completes. When an error is reported via `errorAt(node, msg)`, it uses `node->line` and `node->col` — populated by the parser's `withPos()` stamp. Nodes without position stamps report `0:0`.

### Pointer type conventions in the type checker

Two pointer notations coexist in `expressionTypes`:
- **Leading `*T`**: used by inference results from unary `&` (`"*" + operandType`) and `isPointerType()` (`type[0] == '*'`).
- **Trailing `T*`**: used by `normalizeType()` for struct pointer forms (`"struct:Point*"`) via `hasPointerSuffix` / `extractBaseType` / `addPointerSuffix`.

`MemberExpr` auto-dereferences both forms: it strips trailing `*` via `hasPointerSuffix`, then strips a leading `*` via `type.substr(1)`, then calls `normalizeType()` to get the canonical `struct:Name` form.

### The `ty::Type` IR (`sema/type.{h,cpp}`)

Type spellings are interpreted through a structured IR, `ty::Type`, rather than ad-hoc string manipulation. It is the single grammar interpreter for the type language, used by both the type checker and codegen:

- `ty::Type::parse(spelling)` — the one grammar interpreter: turns a surface type string (`"Result<int,string>"`, `"List<int>*"`, `"*Point"`, `"uint8[858]"`) into structured form (nominal name, type arguments, pointer levels, array extents).
- `str()` — render a `ty::Type` back to its canonical spelling.
- `substitute(subs)` — apply a type-parameter → concrete-type map (template/generic instantiation), recursing through type arguments and pointer levels.
- `nominalName()` — extract the underlying nominal name, peeling pointers/qualifiers; the canonical replacement for the older ad-hoc strip sites (these ad-hoc strips were the origin of the two-evaluator divergence).

Because both phases share `ty::Type::parse`, there is exactly one interpretation of any type spelling. Codegen's `getTypeFromString` dispatches on `ty::Type::parse`; it does not implement a second, independent type evaluator. This is what makes the type checker the single resolver (see *Pipeline Overview* and Decision 14 in `design.md`).

---

## Codegen (`codegen/`)

### IRBuilder usage pattern

`CodeGen` owns three LLVM objects:

```cpp
std::unique_ptr<llvm::LLVMContext> context;
std::unique_ptr<llvm::Module>      module;   // named "eskiu"
std::unique_ptr<llvm::IRBuilder<>> builder;
```

`generateCode()` sets the target triple and data layout early (needed for `sizeof` in `AllocExpr`), then visits the program. `visit(FunctionDecl*)` creates an `"entry"` `BasicBlock` and calls `builder->SetInsertPoint(entryBlock)`. Control-flow visitors create named basic blocks (`"then"`, `"merge"`, `"while"`, `"for_body"`, `"for_step"`, `"for_exit"`, `"switch.end"`, etc.) and move the insert point with `SetInsertPoint`. After all declarations are visited, `generateCode()` calls `llvm::verifyModule`; on failure it returns `nullptr`.

### Three-phase program lowering (forward references)

`visit(Program*)` walks `declarations` **three times** so a body may reference any declaration regardless of source order — matching the type checker's two-pass guarantee end to end:

1. **Type shells + leaf declarations** — `declareStructType()` for each struct (registers template structs, creates concrete `StructType`s); `visit()` for unions, interfaces, and externs.
2. **Function prototypes** — `declareFunction(name, returnType, params)` for every free function and every `StructName_methodName` method. This creates the `llvm::Function` (and registers `funcSretTypes` / `funcEskiuParamTypes`) **without a body**. It is idempotent: it reuses an existing prototype.
3. **Bodies + globals** — `decl->accept(this)` for everything except the externs/unions/interfaces already finalized in phase 1. `visit(FunctionDecl*)` reuses the phase-2 prototype, skips body-less forward declarations (`int b(int n);`), and skips any function whose body was already emitted.

Because all prototypes exist before any body is generated, a call to a function defined later (or a forward declaration, or mutual recursion) resolves cleanly via `module->getFunction(name)` instead of throwing `Undefined variable or function`.

### `evaluateExpr` / `evaluateLValue` — the lvalue/rvalue split

**`evaluateExpr(const ExprPtr&)`**: calls `expr->accept(this)`, which pushes an `llvm::Value*` onto `exprValueStack`. Pops and returns it. For `IdentExpr`, if the symbol is an `AllocaInst`, it loads from it and returns the loaded value. For function values it returns the `llvm::Function*` directly.

**`evaluateLValue(const ExprPtr&)`**: returns the storage pointer without loading. Handles `IdentExpr` (returns the `AllocaInst*` directly), `MemberExpr` (resolves field index, returns `CreateStructGEP` result), and `IndexExpr` (returns `CreateGEP` into array or pointer). Used for the left side of `=`, for `&expr`, and for method call dispatch (`self` pointer).

### `varTypeStack` and consuming the resolved type table

`varTypeStack` is a `vector<map<string,string>>` that parallels `scopeStack`, storing the Eskiu type string for each named variable. It is managed by `pushScope()`/`popScope()` alongside the LLVM value table.

The type checker is the **single resolver** (see *Pipeline Overview*): on its re-run over the transformed AST it resolves every expression's type into a per-expression table, and codegen **consumes that table** rather than independently re-deriving expression types. `varTypeStack` is the codegen-side mirror of that resolved information for named variables, and `getTypeFromString` interprets every type spelling through the one shared grammar interpreter, `ty::Type::parse` — codegen never runs a second, divergent type evaluator.

`getExprEskiuType(const ExprPtr&)` is the codegen-side accessor that returns an expression's resolved Eskiu type from this shared information:
- `IdentExpr` → `lookupVarType(name)`
- `MemberExpr` → resolves base type from `varTypeStack`, looks up the field in `structFields`
- `UnaryExpr` with `&` → prepends `"*"` to the operand's type
- `UnaryExpr` with `*` → strips leading `"*"` from the operand's type
- `IndexExpr` → strips `[N]` or leading/trailing `*` from the base type

This is used by `visit(MemberExpr*)`, `visit(IndexExpr*)`, and `visit(CallExpr*)` for method and interface dispatch. All of these are consistent with the resolver's table by construction, because both phases interpret type spellings through `ty::Type::parse`.

### Template struct instantiation: `ensureTemplateInstantiated()`

Called whenever a template type appears in codegen (in `getTypeFromString`, `visit(MemberExpr*)`, `visit(CallExpr*)`). If `structTypes[mangled]` does not yet exist:
1. Looks up `templateDecls[tname]` (the original `StructDecl*`).
2. Builds a `map<string,string>` substitution from `typeParams` to concrete args.
3. Calls `substType(field.type, subs)` for each field.
4. Creates `llvm::StructType::create(*context, fieldTypes, mangled)`.
5. Stores in `structTypes[mangled]` and `structFields[mangled]`.

### Template function instantiation: `typeParamOverride` map

`visit(TemplateCallExpr*)` mangles the instantiated name as `templateName_arg1_arg2`, saves/restores the current insert point and `currentFunction`, sets `typeParamOverride = subs`, and re-visits the `FunctionDecl` AST node with the mangled name. `getTypeFromString()` consults `typeParamOverride` via `substType()` at the start of every type resolution, so all parameter and local variable types are correctly substituted. After the body is emitted, `typeParamOverride` is cleared.

### Interface vtable layout

`visit(InterfaceDecl* node)` builds:
- `ifaceVtableTypes[name]` — `llvm::StructType` with one `ptr` per method: `%I_vtable = type { ptr, ptr, ... }`
- `ifaceMethodOrder[name]` — `vector<string>` of method names in declaration order (for index lookup)
- `ifaceFatPtrTypes[name]` — `%I_fat = type { ptr, ptr }` (data pointer + vtable pointer)

`boxAsInterface(ifaceName, structName, structPtr)` creates a fat pointer on the stack:
1. Looks up or creates a `private` global constant `%I_vtable_S` holding `{ &S_method1, &S_method2, ... }`.
2. Allocates `%I_fat` on the stack.
3. Stores `structPtr` in field 0 (data) and the vtable global in field 1.
4. Returns the alloca pointer (pointer to the fat struct).

Interface vtable dispatch in `visit(CallExpr*)`: loads `data_ptr` and `vtable_ptr` from the fat struct, computes the method index from `ifaceMethodOrder`, `CreateStructGEP` into the vtable, loads the function pointer, and calls it with a `FunctionType` of all-`ptr` parameters returning `void`. This correctly dispatches `void` methods. Methods with non-void return types share the same dispatch mechanism — the return value is pushed to `exprValueStack`.

`funcEskiuParamTypes` stores the Eskiu parameter type strings per function name. At a call site, if a parameter's type matches an interface registered in `ifaceFatPtrTypes`, the argument is automatically boxed via `boxAsInterface()`.

### Pointer arithmetic

`ptr + n` and `ptr - n` in `visit(BinaryExpr*)`:
```cpp
// ptr + n
result = builder->CreateGEP(ptrElemType(), left, idx, "ptr.add");
// ptr - n: negate offset then GEP
llvm::Value* neg = builder->CreateNeg(idx, "neg");
result = builder->CreateGEP(ptrElemType(), left, neg, "ptr.sub");
```
The GEP uses the *pointee* type (`ptrElemType()`), so `±n` advances by `n` elements — i.e. `n * sizeof(*p)` bytes — matching typed C pointer arithmetic (`p + 1` points at the next element, not the next byte).

### Exception lowering

`try`/`catch`/`throw` lower to the Itanium C++ ABI. Every call inside a `try` body is emitted as `invoke` (not `call`) so it can branch to a `landingpad`; `throw` calls `__cxa_throw` (via `invoke` when inside a try) and the catch dispatch uses `__cxa_begin_catch`/`__cxa_end_catch`. The module gets a `__gxx_personality_v0` declaration. Unhandled exceptions are re-thrown with `resume`. Because this uses the C++ runtime, the final binary must link `-lc++` (macOS) / `-lstdc++` (Linux) — surfaced to users in the language spec, with the mechanics kept here.

### Bitfield layout

A struct with any `: N` field gets a packed layout in `structLayout[name]`: consecutive bitfields are placed into storage words of their declared type, and each field records `{physIndex, bitOffset, bitWidth, storageType, isSigned}`. Reads `lshr`+mask (and sign-extend signed fields); writes are read-modify-write (`storeBitfieldInto`). Structs without bitfields keep the trivial 1:1 field layout, so nothing else changes.

### Enums and type aliases

Enums register member→value constants and the enum type name (→ `i32`); `visit(IdentExpr*)` resolves a bare member to a `ConstantInt`. Type aliases register `name → underlying`; `getTypeFromString` and `getExprEskiuType` expand aliases (peeling pointers) before any other resolution, so the rest of codegen never sees the alias name.

### Bitwise operations

| Eskiu op | LLVM instruction |
|---|---|
| `&` | `CreateAnd` |
| `\|` | `CreateOr` |
| `^` | `CreateXor` |
| `<<` | `CreateShl` |
| `>>` | `CreateAShr` (arithmetic) for signed operands; `CreateLShr` (logical) for unsigned |
| `~` (unary) | `CreateNot` |

### Comparison operations

`visit(BinaryExpr*)` checks `isFloatingPointTy()` first; if true, uses ordered float comparisons (`FCmpOEQ`, `FCmpONE`, `FCmpOLT`, etc.). Otherwise it emits integer comparisons, selecting signed (`ICmpSLT`/`SGT`/…) or unsigned (`ICmpULT`/`UGT`/…) relational predicates from the operands' Eskiu signedness; `==`/`!=` are sign-agnostic (`ICmpEQ`/`ICmpNE`). Pointer comparisons fall through the integer path since LLVM opaque pointers satisfy `!isFloatingPointTy()`.

### `emitObjectFile()`: LLVM TargetMachine + legacy PassManager

```cpp
bool CodeGen::emitObjectFile(const std::string& filename) {
    // InitializeNativeTarget + InitializeNativeTargetAsmPrinter
    // getDefaultTargetTriple → lookupTarget → createTargetMachine (PIC relocation)
    // setDataLayout from TargetMachine
    // raw_fd_ostream to filename
    // legacy::PassManager pm; tm->addPassesToEmitFile(pm, dest, nullptr, ObjectFile)
    // pm.run(*module); dest.flush();
}
```

The data layout is set twice: once in `generateCode()` (for `sizeof` queries during codegen) and once in `emitObjectFile()` (for the final emission pass). Both use `getHostCPUName()` and `Reloc::PIC_`. The function returns `true` on success, `false` on any error (unknown target, cannot open file, target cannot emit object file).

### VarDecl type coercion

`visit(VarDecl*)` allocates an `AllocaInst` for the declared type, evaluates the initializer, then coerces if types differ:

| Source → Destination | Instruction |
|---|---|
| `i32` → `i8` (wider → narrower integer) | `CreateTrunc` |
| `i8` → `i32` (narrower → wider integer) | `CreateSExt` for signed sources, `CreateZExt` for unsigned (chosen from the source's signedness) |
| `i1` → `i32` (bool → wider integer) | `CreateZExt` (zero-extend, not sign-extend) |
| integer → float/double | `CreateSIToFP` |
| float/double → integer | `CreateFPToSI` |
| float → double or vice versa | `CreateFPCast` |

The same coercion logic appears in `emitStructInitInto()` for struct field initialization.

---

## Symbol Table

### Scope management in TypeChecker vs CodeGen

**TypeChecker**: `scopes` is a `vector<map<string,Symbol>>`. `pushScope()` appends an empty map; `popScope()` removes the last. `lookupSymbol()` searches from innermost to outermost. Symbols visible in inner scopes shadow outer ones. Functions are stored separately in `functionSignatures`, not in `scopes`.

**CodeGen**: `symbolTable` is a flat `map<string,llvm::Value*>`. `scopeStack` is a `vector<map<string,llvm::Value*>>` used for snapshots. `pushScope()` copies the current `symbolTable` onto `scopeStack` and pushes a new layer onto `varTypeStack`. `popScope()` restores `symbolTable` from the snapshot and pops `varTypeStack`. This means inner-scope symbols are automatically invisible after `popScope()`, and inner-scope modifications to outer variables survive only within the scope (inner scope gets a copy, not a reference).

`lookupSymbol()` in CodeGen is a single flat `symbolTable.find(name)`. When the lookup returns `nullptr`, `visit(IdentExpr*)` falls back to `module->getFunction(name)` to find functions declared in other scopes.

### `break` and `continue` targets

`breakTarget` and `continueTarget` are `llvm::BasicBlock*` members of `CodeGen`, initialized to `nullptr`. `visit(WhileStmt*)` and `visit(ForStmt*)` save/restore these around the body:
- `breakTarget = exitBlock`
- `continueTarget = loopBlock` (while) or `stepBlock` (for, so `continue` jumps to the step expression)

`visit(BreakStmt*)` calls `builder->CreateBr(breakTarget)` or throws if `breakTarget` is null. `visit(ContinueStmt*)` calls `builder->CreateBr(continueTarget)` or throws if null. `visit(SwitchStmt*)` also sets `breakTarget = endBlock` around its cases.

---

## Type Mappings

| Eskiu type | LLVM type | Notes |
|---|---|---|
| `int`, `int32` | `i32` | |
| `int8` | `i8` | |
| `int16` | `i16` | |
| `int64` | `i64` | |
| `uint`, `uint32` | `i32` | Same LLVM type as `int`; signedness is carried in the instruction (`udiv`/`urem`/`lshr`/`ICmpULT` for unsigned), and integer widening zero- vs sign-extends per the source's signedness |
| `uint8` | `i8` | |
| `uint16` | `i16` | |
| `uint64` | `i64` | |
| `float` | `float` (f32) | |
| `double` | `double` (f64) | |
| `bool` | `i1` | |
| `char` | `i8` | |
| `string` | `ptr` (opaque, addrspace 0) | |
| `void` | `void` | Return type only |
| `*T` or `T*` | `ptr` (opaque, addrspace 0) | All pointer types are opaque in LLVM 15+ |
| `struct:Name` (concrete) | `%Name` (`llvm::StructType`) | Created by `visit(StructDecl*)` |
| `struct:Result_int_string` (template instance) | `%Result_int_string` | Created lazily by `ensureTemplateInstantiated()` |
| Interface name (e.g. `Greeter`) | `ptr` (opaque) | Interface values are `ptr` to `%Greeter_fat` |
| `T[N]` (fixed-size array) | `[N x T]` (`llvm::ArrayType`) | e.g. `uint8[858]` → `[858 x i8]` |

Integer literals are emitted as `i32` (64-bit literals widen to `i64` without truncation). Float literals are emitted as `double` (`f64`). A float literal assigned to a `float` (`f32`) variable is coerced down via `CreateFPCast`. Signedness is tracked through codegen from the Eskiu type: unsigned types use the unsigned LLVM instructions (`CreateUDiv`, `CreateURem`, `CreateLShr`, `CreateICmpULT`, etc.) while signed types use the signed forms, and integer widening picks `CreateZExt` vs `CreateSExt` from the source operand's signedness at every coercion site.

---

## Hardening Apparatus

Beyond the `--test-*` modes, the compiler is guarded by an automated harness (run in CI; see `debugging.md` for how to use each as a diagnosis tool):

- **Golden-IR oracle** — `tests/type_zoo/snapshot.sh` + `tests/type_zoo/golden/`: emits IR for the type-zoo corpus and diffs it against the checked-in baseline. A behavior-preserving change must produce byte-identical IR; this is the codegen-regression guard.
- **Generative + mutation fuzzer** — `tests/fuzz/eskiu_fuzz.py` with an **O0-vs-O2 differential oracle**: synthesized programs are run at both optimization levels and any divergence is a miscompile. This catches accidentally-`-O0`-correct IR (undef, wrong width, missing extension).
- **`--asan` / `--ubsan` gates** — AddressSanitizer and trapping UB checks run over the test corpus for runtime memory errors and undefined behavior.
- **Formatter idempotency** — `eskiuc fmt --check` over every test.

---

## Known Limitations

- **Source line numbers at `0:0`**: nodes not stamped by `withPos()` in the parser (including internally synthesized nodes like desugared compound assignments' outer `BinaryExpr`, some `BlockStmt` wrappers) report `line=0, col=0` in error messages. All primary expressions, operators, and control-flow statements are stamped correctly.

(Two former limitations are resolved: float-literal handling — literals are `double` and coerce to `float` on assignment, which is correct, see "Type lowering" above — and non-`void` interface-method dispatch, which uses the method's real return type via `ifaceMethodReturnTypes` / `ifaceMethodParamEskiuTypes` to build the indirect-call `FunctionType`.)
