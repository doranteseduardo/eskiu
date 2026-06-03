# Glossary

Reference glossary of terms used in Eskiu documentation and compiler source code.

## A

**Abstract Syntax Tree (AST)**
Tree-structured representation of a program's syntactic form after parsing. Each node corresponds to a language construct such as a function declaration, binary expression, or loop. The type checker and code generator both traverse the AST using the visitor pattern. See also: ASTVisitor, parser.

**alloca**
LLVM IR instruction that allocates space on the stack frame of the current function and returns a pointer to it. Every local variable in Eskiu compiles to an `alloca` in the function's entry block, followed by `store`/`load` instructions. Stack allocations are automatically reclaimed when the function returns.

**ASTVisitor**
Abstract base class that defines a `visit` method for each AST node type. Concrete subclasses (e.g., `TypeChecker`, `CodeGen`) implement these methods to traverse the tree without embedding logic inside the node classes themselves. See also: visitor pattern.

## B

**basic block**
Maximal sequence of LLVM IR instructions with no branches except at the end. Every function body is a graph of basic blocks. Control-flow constructs (if, while, for) cause the code generator to create new basic blocks and wire them with branch instructions. See also: IRBuilder.

**binary operator**
An operator that takes exactly two operands. Eskiu supports arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`), and logical (`&&`, `||`) binary operators. Precedence rules determine evaluation order when operators appear without parentheses. See also: unary operator, precedence.

**BlockItem**
Union type used internally in the parser and AST to represent a single item inside a block statement — either a declaration (VarDecl, StructDecl) or an executable statement. Storing both as `BlockItem` allows the parser to handle declaration-in-block uniformly. See also: BlockStmt, declaration, statement.

**BlockStmt**
AST node representing a `{ ... }` block. Contains an ordered list of `BlockItem` entries and introduces a new nested scope. Functions, if-branches, loops, and standalone braces all produce a `BlockStmt`. See also: scope, BlockItem.

## C

**codegen**
Short form of "code generation." Refers to Phase 3 of the Eskiu compiler, implemented in `codegen/codegen.cpp`, which walks the AST and emits LLVM IR instructions via `IRBuilder`. Also used informally to describe any pass that produces output code. See also: IRBuilder, LLVM IR.

**declaration**
A language construct that introduces a new named entity into the current scope. Eskiu declaration forms are: `FunctionDecl`, `VarDecl`, `StructDecl`, and `ExternDecl`. Declarations are distinct from statements in that they bind a name; statements execute logic. See also: statement, scope.

## E

**expression**
A syntactic form that evaluates to a value and has a type. Examples: `3 + 4`, `add(5, 2)`, `point.x`, `*ptr`. Expressions form the leaves and internal nodes of most AST subtrees. See also: lvalue, rvalue, statement.

**ExternDecl**
AST node representing a declaration of a function whose implementation lives in an external C library. Syntax: `extern int printf(string fmt, ...);`. The type checker records the function signature; the code generator emits an LLVM `declare` instruction so the linker can resolve it. See also: variadic, declaration.

## F

**FunctionDecl**
AST node representing a function definition, including its name, parameter list, return type, and body (`BlockStmt`). During type checking, parameters are pushed into a new scope and the body is validated against the declared return type. During code generation, a new LLVM `Function` object is created and populated. See also: declaration, scope.

## G

**GEP (getelementptr)**
LLVM IR instruction that computes the address of a sub-element of an aggregate type (array or struct) without loading data. Used by the Eskiu code generator to access struct fields and array elements. GEP does pointer arithmetic at the IR level and produces a typed pointer to the target element.

## I

**identifier**
A name chosen by the programmer to label a variable, function, struct, or parameter. In Eskiu, identifiers must begin with a letter or underscore and may contain letters, digits, and underscores. The lexer emits an `IDENTIFIER` token; the parser stores the raw string in `IdentExpr` or declaration nodes.

**IRBuilder**
LLVM C++ API class (`llvm::IRBuilder<>`) that provides a cursor-based interface for inserting IR instructions into a basic block. Eskiu's code generator holds a single `IRBuilder` instance and repositions it as it enters new blocks. See also: basic block, codegen.

## L

**lexer**
Phase 1 of the Eskiu compiler, implemented in `lexer/`. Reads the raw source text character by character and produces a flat sequence of tokens annotated with type, value, line, and column. The lexer handles whitespace, comments, string literals, numeric literals, keywords, and punctuation. See also: token, TokenType, parser.

**literal**
A compile-time constant value written directly in source code. Eskiu literal kinds: integer literals (`42`), floating-point literals (`3.14`), string literals (`"hello"`), boolean literals (`true`, `false`), and character literals (`'a'`). Represented in the AST as `LiteralExpr`. See also: expression.

**LLVM IR**
The text and in-memory intermediate representation used by the LLVM compiler infrastructure. Eskiu compiles to LLVM IR, which is then lowered to machine code by LLVM's optimization and backend passes. IR is strongly typed, uses Static Single Assignment (SSA) form, and is human-readable in `.ll` files. See also: codegen, basic block, IRBuilder.

**lvalue**
An expression that refers to a storage location and can appear on the left side of an assignment. In Eskiu, variable names and pointer dereferences (`*ptr`) are lvalues. The code generator resolves lvalues to their `alloca` address (emitting a GEP for struct fields), then uses `store` to write the value. See also: rvalue, alloca.

## M

**module**
The top-level LLVM IR container (`llvm::Module`) that holds all function definitions, global variables, and external declarations produced for a single compilation unit. The code generator creates one module per `.esk` file. The module is printed as LLVM IR text when `--test-codegen` is passed.

## O

**opaque pointer**
LLVM pointer type in LLVM 15+ (`ptr`) that carries no element-type information; replaced typed pointers (`i32*`, `i8*`, etc.). Eskiu targets opaque-pointer mode and uses GEP with explicit type hints to navigate aggregate structures. See also: GEP, pointer type.

## P

**parser**
Phase 2 of the Eskiu compiler, implemented in `parser/`. Consumes the token stream produced by the lexer and builds the AST using recursive descent. Handles declarations, statements, and expressions with explicit precedence climbing. See also: recursive descent, AST, precedence.

**phase**
A numbered stage in the Eskiu compiler roadmap. Phases 0-4 are complete (build/CLI, lexer, parser, codegen, type checker). Phase 5 (structs/interfaces/templates), Phase 6 (heap), and Phase 7 (stdlib/Result<T,E>) are upcoming. The term is also used informally within a phase to label sub-milestones. See also: codegen, type checker, semantic analysis.

**pointer type**
A type that holds the memory address of a value of another type. Eskiu accepts both leading-star notation (`*T`) and trailing-star notation (`T*`) in source code; both are normalized to the same internal representation. Pointer arithmetic and dereferencing are supported; pointer safety is the programmer's responsibility. See also: opaque pointer, GEP, lvalue.

**precedence**
The binding strength of an operator relative to others. Higher-precedence operators bind their operands before lower-precedence ones. Eskiu follows C operator precedence: multiplicative (`*`, `/`, `%`) before additive (`+`, `-`), which is before relational, then equality, then logical-and, then logical-or. The parser implements precedence via recursive descent helper functions. See also: binary operator, parser.

**program**
A complete Eskiu source file (`.esk`) that defines or declares all entities needed for compilation. At the AST level a program is a `ProgramNode` containing a list of top-level declarations. See also: declaration, module.

## R

**recursive descent**
Parsing strategy in which each grammar rule is implemented as a mutually recursive function. Eskiu's parser is a hand-written recursive-descent parser; `parseDecl`, `parseStmt`, and `parseExpr` call each other according to the grammar's structure. See also: parser.

**rvalue**
An expression whose value can be read but that does not itself designate a storage location. Literals, arithmetic results, and function-call results are rvalues in Eskiu. The code generator evaluates rvalues with `load` instructions (when reading a variable) or directly as IR values (for computed results). See also: lvalue.

## S

**scope**
The region of source code in which a declared name is visible. Eskiu uses lexical (block) scoping: each `BlockStmt` introduces a new scope. The symbol table supports nested scopes via a scope stack; inner scopes can shadow outer ones. Names go out of scope when their enclosing block ends. See also: symbol table, BlockStmt.

**semantic analysis**
The compiler phase (Phase 4) that validates program meaning beyond syntactic correctness. In Eskiu this is the type checker: it verifies type compatibility, resolves identifiers, checks struct-field existence, validates function call arities and types, and enforces return-type consistency. See also: type checker, scope.

**statement**
A language construct that performs an action but does not itself produce a value. Eskiu statement kinds: `BlockStmt`, `IfStmt`, `ForStmt`, `WhileStmt`, `ReturnStmt`, `BreakStmt`, `ExprStmt`. Statements are sequenced inside `BlockStmt`. See also: expression, declaration.

**struct**
A composite type composed of named fields, each with its own type. Declared with `struct Name { ... }`. Fields are accessed via the `.` member operator. Methods can be defined on structs (Phase 5). The type checker validates field access; the code generator lays fields out sequentially in an LLVM struct type and uses GEP to address them. See also: StructDecl, GEP.

**StructDecl**
AST node representing a struct type definition, including the struct's name, its field list (name + type pairs), and any method declarations. Registered in the struct registry during semantic analysis so that `MemberExpr` nodes can look up field types. See also: struct, declaration.

**symbol table**
A data structure mapping identifier names to their resolved types and declarations within a scope. Eskiu's type checker maintains a stack of symbol-table frames, pushing a new frame on block entry and popping it on block exit. Used to resolve variable references and detect undeclared identifiers. See also: scope, identifier.

## T

**token**
The smallest meaningful unit produced by the lexer. Each token carries a `TokenType`, its raw lexeme string, and a source location (line, column). Examples: keyword `int`, identifier `result`, punctuation `{`, integer literal `42`, string literal `"hello"`. See also: TokenType, lexer.

**TokenType**
Enumeration of all token categories recognized by the Eskiu lexer. Includes keywords (`INT`, `RETURN`, `IF`, `WHILE`, ...), literals (`INTEGER_LITERAL`, `FLOAT_LITERAL`, `STRING_LITERAL`), identifiers (`IDENTIFIER`), operators, punctuation, and `END_OF_FILE`. Used in the parser's `expect()` and `match()` helpers to drive grammar rules. See also: token, lexer.

**type checker**
The compiler component (Phase 4, `sema/type_checker.cpp`) that traverses the AST, infers or verifies the type of every expression, validates declarations, and reports semantic errors. It maintains the symbol table and struct registry. See also: semantic analysis, type inference, symbol table.

**type coercion**
Implicit or explicit conversion of a value from one type to another. Eskiu requires explicit casts for most conversions (e.g., `(int)myFloat`). The type checker identifies cases where implicit promotion is safe (e.g., widening integer types in expressions) and emits the appropriate LLVM extension or truncation instructions. See also: type inference, CastExpr.

**type inference**
The compiler's ability to deduce the type of an expression or variable from context without an explicit annotation. Eskiu performs limited local type inference — for example, the right-hand side of a declaration can constrain the variable's type — but does not perform full Hindley-Milner inference. Explicit type annotations are generally required. See also: type checker, VarDecl.

**type normalization**
The process of canonicalizing type names to a single internal representation before comparison or storage. Eskiu maps source-level aliases to canonical forms (e.g., `int` -> `i32`, `uint8` -> `u8`, `*T` and `T*` both -> pointer-to-T) so that the type checker and code generator operate on a consistent set of type descriptors. See also: type checker, pointer type.

## U

**unary operator**
An operator that takes a single operand. Eskiu unary operators: `-` (arithmetic negation), `!` (logical not), `*` (pointer dereference), `&` (address-of). Represented in the AST as `UnaryExpr`. See also: binary operator.

## V

**variadic**
A function that accepts a variable number of arguments after a fixed parameter list, indicated by `...` in its signature. Used primarily for C-interop functions such as `printf`. The type checker allows variadic calls to accept any arguments beyond the fixed parameters; LLVM emits a variadic function declaration. See also: ExternDecl.

**VarDecl**
AST node representing the declaration of a local or global variable. Stores the variable's name, declared type (may be inferred), and optional initializer expression. The type checker validates that the initializer type matches the declared type; the code generator emits an `alloca` and an optional `store`. Supports both C-style (`int x = 5;`) and `let`-style (`let x: int = 5;`) syntax. See also: alloca, declaration, type inference.

**visitor pattern**
Object-oriented design pattern in which an external object (the visitor) defines operations on the nodes of a data structure without modifying those nodes. Eskiu's AST nodes expose an `accept(ASTVisitor&)` method; the type checker and code generator each implement `ASTVisitor` to walk the tree. This separates compiler passes from AST node definitions. See also: ASTVisitor, AST.

---

- [architecture.md](dev/architecture.md) — Compiler architecture and pass pipeline
- [spec.md](lang/spec.md) — Full language specification
- [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) — Rationale for key design choices
- [phases.md](dev/phases.md) — Compiler development roadmap
