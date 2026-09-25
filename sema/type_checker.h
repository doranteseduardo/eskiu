#pragma once

#include "../ast/ast.h"
#include "../lexer/lexer.h"
#include "type.h"
#include <map>
#include <unordered_map>
#include <set>
#include <vector>
#include <string>
#include <memory>

// Can executing `s` fall through to the following statement? (Definite-return analysis,
// typecheck_decl.cpp; also drives null-narrowing after an early-exit guard.)
bool stmtCanCompleteNormally(Stmt* s);

class TypeChecker : public ASTVisitor {
public:
    TypeChecker();

    // Main entry point
    bool check(Program* program);

    // Get inferred type of an expression
    std::string getExpressionType(Expr* expr);

    // Resolved per-expression type table (Expr* → normalized type). Re-running
    // check() on the post-AsyncTransform AST and handing this to codegen makes the
    // type checker the single resolver of expression types.
    const std::map<Expr*, std::string>& expressionTypeMap() const { return expressionTypes; }

    // Visitor methods
    void visit(Program* node) override;
    void visit(FunctionDecl* node) override;
    void visit(VarDecl* node) override;
    void visit(StructDecl* node) override;
    void visit(ExternDecl* node) override;
    void visit(IntrinsicDecl* node) override;
    void visit(InterfaceDecl* node) override;
    void visit(ContinueStmt* node) override;
    void visit(SwitchStmt* node) override;
    void visit(MatchStmt* node) override;
    void visit(TemplateCallExpr* node) override;

    void visit(BlockStmt* node) override;
    void visit(IfStmt* node) override;
    void visit(WhileStmt* node) override;
    void visit(DoWhileStmt* node) override;
    void visit(ForStmt* node) override;
    void visit(ForInStmt* node) override;
    void visit(ReturnStmt* node) override;
    void visit(BreakStmt* node) override;
    void visit(ExprStmt* node) override;

    void visit(BinaryExpr* node) override;
    void visit(UnaryExpr* node) override;
    void visit(IncDecExpr* node) override;
    void visit(QuestionExpr* node) override;
    void visit(TernaryExpr* node) override;
    void visit(CallExpr* node) override;
    void visit(IndexExpr* node) override;
    void visit(MemberExpr* node) override;
    void visit(CastExpr* node) override;
    void visit(LiteralExpr* node) override;
    void visit(IdentExpr* node) override;
    void visit(StructInitExpr* node) override;
    void visit(ArrayLitExpr* node) override;
    void visit(AllocWithExpr* node) override;
    void visit(LambdaExpr* node) override;
    void visit(AsmStmt* node) override;
    void visit(UnionDecl* node) override;
    void visit(SizeofExpr* node) override;
    void visit(FreeClosureExpr* node) override;
    void visit(AwaitExpr* node) override;
    void visit(ThreadCreateExpr* node) override;
    void visit(ThreadJoinStmt* node) override;
    void visit(ThrowStmt* node) override;
    void visit(TryStmt* node) override;
    void visit(DeferStmt* node) override;
    void visit(EnumDecl* node) override;
    void visit(TypeAliasDecl* node) override;

    // -Wall: emit lint-style warnings (unused vars/params/functions, etc.)
    bool warnAll = false;
    bool warnExtra = false;   // -Wextra: signed/unsigned comparison mismatches, etc.

    // --- LSP / tooling interface (consumed by --hover-at / --definition-at) ---
    std::string sourceFile = "unknown";   // the primary input (fallback for diagnostics)
    // The file of the top-level declaration being checked: diagnostics name it, so
    // an error in an imported module or a second input points at that file.
    std::string curFile;
    const std::string& diagFile() const { return curFile.empty() ? sourceFile : curFile; }
    std::string getTypeAtPosition(int line, int col) const;
    struct DefLocation { int line; int col; std::string file; };
    std::map<std::string, DefLocation> definitionLocations;
    // Use-site map: (line,col) → symbol name of a global (function, enum member,
    // global variable), resolved through definitionLocations.
    std::map<std::pair<int,int>, std::string> useLocations;
    // Use-site map for names that resolved to a local/parameter: (line,col) → the
    // definition of the exact symbol that scope lookup found, so a local `n` in f()
    // never jumps to another function's `n`.
    struct UseDef { int width; DefLocation def; };
    std::map<std::pair<int,int>, UseDef> useDefs;
    // Tooling maps only describe the primary input (an imported file's nodes share
    // line/col coordinates with it).
    bool inPrimaryFile() const { return !inInstance && (curFile.empty() || curFile == sourceFile); }
    std::string getDefinitionAt(int line, int col) const;
    // Declared-name hover spans (variables, parameters): cursor on the declared
    // name → its type, even though the name is not an expression node.
    struct HoverSym { int line; int col; int width; std::string type; };
    std::vector<HoverSym> hoverSyms;

private:
    // Symbol table: maps name -> type
    struct Symbol {
        std::string type;
        bool isDeclared;
        bool used = false;     // -Wall: referenced at least once
        int  line = 0, col = 0;
        std::string file;      // file of the declaration (go-to-definition)
        bool isParam = false;
        bool isConst = false;  // declared with `const` — reassignment is an error
        bool isStatic = false; // `static` local: one global cell, referenced (not captured) by lambdas
        Expr* constInit = nullptr; // initializer of a `const` (lets case labels fold `const int K`)
    };

    // True if `name` resolves to a symbol declared `const` (searches scopes).
    bool isConstSymbol(const std::string& name) const;
    // Is `e` a compile-time constant initializer (C semantics) that codegen's constant
    // folder emits? Used for global and `static` initializers.
    bool isConstInit(const ExprPtr& e) const;

    // The symbol `name` resolves to in the current scopes (innermost first), or null.
    const Symbol* findSymbol(const std::string& name) const;
    // If assigning to `lhs` would mutate a `const` value in place (the binding
    // itself, or a field/element of a const aggregate), returns true and sets
    // `nameOut` to the constant's name. Stops at pointer dereferences: writing
    // *through* a const pointer mutates the pointee, not the binding.
    bool assignsToConst(Expr* lhs, std::string& nameOut);

    // Struct information: name -> fields
    struct StructInfo {
        std::string name;
        std::vector<StructDecl::Field> fields;
    };

    // Scope management
    std::vector<std::map<std::string, Symbol>> scopes;
    // name -> indices of the scopes that define it, innermost last, so a lookup costs
    // the same at any nesting depth instead of a walk over every enclosing scope.
    // Kept in step with `scopes` by defineSymbol and popScope.
    std::unordered_map<std::string, std::vector<int>> scopeIndex;
    // Index of the innermost scope defining `name`, or -1.
    int scopeOf(const std::string& name) const;

    // Struct registry: name -> StructInfo  (concrete structs only)
    std::map<std::string, StructInfo> structs;

    // Template registry: template name -> StructDecl (not yet instantiated)
    std::map<std::string, StructDecl*> templateDecls;
    // Template function registry
    std::map<std::string, FunctionDecl*> funcTemplateDecls;
    // Reverse map: mangled instance name -> (template name, concrete type args),
    // for inferring a type parameter from a composite argument like List<T>*.
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> templateInstanceArgs;
    // Structural unification of a parameter type pattern against a concrete type.
    void unifyTypeParam(std::string pattern, std::string concrete,
                        const std::set<std::string>& tps,
                        std::map<std::string, std::string>& subs);
    // Per-instantiation checking of generic bodies. A template body is checked once
    // for each distinct set of concrete type arguments it is instantiated with (a
    // generic function call, or a generic struct instance for its inline methods),
    // after the main pass. The body's AST is shared by every instance, so while one
    // is checked the per-expression type table is swapped out and no node is stamped.
    struct PendingInstance {
        FunctionDecl* fn;
        std::map<std::string, std::string> subs;   // type param -> concrete type
        std::string display;                       // "pick<Box>", "Box<int>.get"
        std::string mangled;                       // the instance's own function name
        std::string selfType;                      // "*Box_int" for a generic struct method
        std::string file;
        int depth;
    };
    std::vector<PendingInstance> pendingInstances;
    // A generic struct instance's inline methods, by their function name (`Box_int_get`):
    // queued for checking when first called.
    struct GenericMethodInst { FunctionDecl* fn; StructDecl* owner; std::map<std::string, std::string> subs; };
    std::map<std::string, GenericMethodInst> genericMethodInsts;
    std::set<std::string> queuedInstances;
    std::map<std::string, std::string> instSubs;  // substitutions of the instance being checked
    std::string instContext;                      // its display name (appended to diagnostics)
    int instDepth = 0;
    bool inInstance = false;
    bool substituting = false;                    // normalizeType re-entry guard
    // Queue an instance of `fn` (keyed and displayed as `name<args>suffix`), unless already queued.
    void queueInstance(FunctionDecl* fn, const std::vector<std::string>& typeParams,
                       const std::map<std::string, std::string>& subs, const std::string& name,
                       const std::string& suffix, const std::string& mangled, const std::string& selfType, const std::string& file);
    void checkPendingInstances();
    // A type argument written inside the instance being checked, resolved to concrete.
    std::string resolveInstType(const std::string& t) const;
    // Interface registry
    std::map<std::string, InterfaceDecl*> interfaceDecls;

    // Enum registry: member name -> integer value; and the set of enum type names
    std::map<std::string, long long> enumConstants;
    std::set<std::string> enumTypes;
    // Algebraic (payload-bearing) enums: distinct value types, not ints.
    std::set<std::string> adtEnums;                              // ADT enum names
    std::map<std::string, EnumDecl*> enumDecls;                  // name -> decl (for payloads)
    std::map<std::string, EnumDecl*> plainEnumDecls;             // name -> decl for classic int enums (match exhaustiveness)
    // Variant name -> (enum name, tag index). Concrete (non-generic) ADT enums.
    std::map<std::string, std::pair<std::string, int>> adtVariants;
    // Generic ADT enums (e.g. Option<T>): name -> decl, and variant -> (enum, tag).
    // Instances (Option_int) are recorded in adtEnums + templateInstanceArgs and
    // resolved back to the generic decl + type args for match / construction.
    std::map<std::string, EnumDecl*> genericEnumDecls;
    std::map<std::string, std::pair<std::string, int>> genericVariants;
    // Type aliases: alias name -> underlying type string
    std::map<std::string, std::string> typeAliases;

    // Function signatures: name -> (return type, parameter types)
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> functionSignatures;
    // Operator overloads, indexed by op ("+","[]","u-",...). Each candidate keeps its param
    // type spellings so `a op b` resolves by operand types (with the usual numeric coercions).
    struct OperatorOverload { std::vector<std::string> params; std::string retType; std::string fnName; };
    std::map<std::string, std::vector<OperatorOverload>> operatorOverloads;
    // Resolve `op` over operand types to a user overload; returns its fn name (+ret via outRet), or "".
    std::string resolveOperator(const std::string& op, const std::vector<std::string>& argTypes, std::string& outRet);
    // Per-function `escaping` flags for each parameter (closure-retention).
    std::map<std::string, std::vector<bool>> functionParamEscaping;
    std::set<std::string> mustUseFuncs;   // functions whose result may not be discarded
    std::set<std::string> externFnNames;  // `extern` C functions (fn-typed params take a C fn pointer)
    // `?*T` variables currently known non-null, keyed by narrowKey ("name@scope") so a
    // shadowing declaration is a different variable. A narrowed identifier's expression
    // type drops the `?`, so it may be dereferenced and used as a `*T`.
    std::set<std::string> narrowedNonNull;
    std::string narrowKey(const std::string& name) const;
    // Keys of the `?*T` variables proven non-null when `cond` evaluates to `whenTrue`
    // (`p != null`, `p == null` false, `p`, `!c`, `a && b` true, `a || b` false).
    void condNarrowings(Expr* cond, bool whenTrue, std::vector<std::string>& keys);
    // Insert `keys` into narrowedNonNull; returns the ones newly inserted (to undo).
    std::vector<std::string> applyNarrowings(const std::vector<std::string>& keys);
    void undoNarrowings(const std::vector<std::string>& inserted);
    // Forget narrowing of every variable assigned (or address-taken) anywhere in `s`/`e`,
    // before checking a loop whose later iterations would observe the assignment.
    void dropAssignedIn(Stmt* s);
    void dropAssignedIn(Expr* e);
    // Argument count + types of a call against `paramTypes` (a trailing "..." = variadic);
    // `what` names the callee in diagnostics.
    bool checkGenericMethodCall(class CallExpr* node, class MemberExpr* member, const std::string& baseType);
    void checkCallArgs(class CallExpr* node, const std::string& what, const std::vector<std::string>& paramTypes);
    // Check a call through a fn-typed value (`fn(T,...)->R`) and return its result type R.
    std::string checkFnValueCall(class CallExpr* node, const std::string& what, const std::string& fnType);
    // Reject deref/index/member of a nullable `?*T` that hasn't been null-checked.
    void checkNullableDeref(Expr* operand, const char* how);
    // Escape-soundness state for the function currently being checked: the names
    // of its non-escaping fn-typed params, those found to escape, and the name
    // currently in callee position (a call of the param does NOT make it escape).
    std::set<std::string> nonEscapingFnParams;
    std::set<std::string> escapedFnParams;
    std::string calleeContext;
    // True while checking the body of an `async fn` — gates `await`.
    bool inAsyncFn = false;
    // Set when an `await` is seen in the current function body — an `async fn`
    // with none is rejected (the transform needs at least one suspend point).
    bool awaitSeenInFn = false;

    // -Wall function-usage tracking: top-level functions defined vs. referenced
    std::map<std::string, std::pair<int,int>> definedFns; // name -> (line,col)
    std::set<std::string> calledFns;
    // Source spelling of an operator function for diagnostics (`__op_add_V_V` ->
    // `operator +(V, V)`); other names display as themselves.
    std::map<std::string, std::string> fnDisplayNames;
    std::string fnDisplay(const std::string& name) const {
        auto it = fnDisplayNames.find(name);
        return it != fnDisplayNames.end() ? it->second : name;
    }

    // Current function context for return type checking
    std::string currentFunctionReturnType;

    // Error tracking
    std::vector<std::string> errors;
    std::vector<std::string> loopLabelStack;   // enclosing loop labels ("" for unlabeled), for labeled break/continue validation
    int switchDepth = 0;                       // enclosing switches (a bare `break` may exit one)
    // An expression that denotes storage (a variable, field, element, or `*p`), so it may be
    // assigned to or have its address taken.
    bool isLvalueExpr(Expr* e);
    void checkAssignment(class BinaryExpr* node);
    // Type one operator whose operands were already visited (see visit(BinaryExpr)).
    void finishBinary(class BinaryExpr* node);
    // A switch `case` label codegen can fold to an integer constant.
    bool isConstIntExpr(Expr* e);
    bool foldConstInt(Expr* e, long long& out);
    static bool foldConstBinaryOp(const std::string& op, long long x, long long y, long long& out);
    int foldDepth = 0;
    bool hasErrors = false;

    // Helper methods
    void pushScope();
    void popScope();
    void defineSymbol(const std::string& name, const std::string& type);
    void defineSymbol(const std::string& name, const std::string& type,
                      int line, int col, bool isParam);
    std::string lookupSymbol(const std::string& name);
    void defineFunction(const std::string& name, const std::string& returnType,
                       const std::vector<std::string>& paramTypes);

    // Type inference
    std::string inferBinaryExprType(const std::string& leftType, const std::string& op,
                                    const std::string& rightType);
    std::string inferUnaryExprType(const std::string& op, const std::string& operandType);

    // Type validation
    void validateStructType(const std::string& type, ASTNode* at = nullptr);
    // A `void` (or array of void) value type: only a function result may be void.
    bool isVoidValueType(const std::string& type);
    std::set<std::string> unknownTypes;   // names already reported as unknown (no cascades)
    // A bitfield must have an integer type at least `bitWidth` bits wide.
    void checkBitfield(ASTNode* at, const std::string& owner, const StructDecl::Field& f);
    // Reject a struct/union that contains itself by value (no finite layout).
    void checkValueCycles(Program* program);
    // One top-level namespace: duplicate/conflicting functions, globals, types, members.
    void checkTopLevelNames(Program* program);

    // Type checking utilities
    bool isValidAssignment(const std::string& lhsType, const std::string& rhsType);
    // Would storing a `rhs` pointer into a `lhs` pointer drop the pointee's const
    // (`const P*` into `*P`, or into `*void`)? Shapes are compared normalized.
    bool dropsConstQual(const std::string& lhs, const std::string& rhs);
    // "" if `structName` structurally satisfies `iface` (names + signatures), else why not.
    std::string interfaceMismatch(const std::string& structName, InterfaceDecl* iface);
    // A generic free `S_m<T..>(S<T..>* self, ...)` implementing interface method `m` for
    // the instance `instName` of S: its type arguments are bound from the receiver (then
    // the interface's parameter types), the instance is queued for checking, and `sig`
    // receives its concrete (return, [self, params...]) signature. Null when none applies.
    FunctionDecl* genericFreeMethodFor(const std::string& instName, const InterfaceDecl::MethodSig& m,
                                       std::pair<std::string, std::vector<std::string>>& sig);
    // Bounded generics: check that each constrained type param's concrete arg
    // (in `subs`) satisfies its interface constraint(s).
    void checkConstraints(ASTNode* node,
                          const std::map<std::string, std::vector<std::string>>& constraints,
                          const std::map<std::string, std::string>& subs);
    bool isNumericType(const std::string& type);
    bool isIntType(const std::string& type);
    bool isFloatType(const std::string& type);
    // A numeric assignment `lhs = rhs` loses information: float/double into an int,
    // or a wider numeric into a narrower one. Same-width signedness changes are not
    // narrowing; int into float is widening.
    bool isNarrowingNumeric(const std::string& lhsType, const std::string& rhsType);
    // If `e` is an integer literal, whether its value fits `targetType`'s range
    // (so a narrowing assignment from a literal that fits is still allowed).
    bool intLiteralFits(const std::string& targetType, Expr* e);
    // Central assignability check for init / `=` / return / call-argument sites.
    // Returns "" when `srcExpr` (of type `srcType`) may be assigned to `targetType`,
    // else a diagnostic message. A narrowing numeric conversion is rejected unless
    // `srcExpr` is an integer literal that fits the target. (const-drop is reported
    // by the caller's own const-specific check, so it is not repeated here.)
    std::string assignabilityError(const std::string& targetType,
                                   const std::string& srcType, Expr* srcExpr);
    // Conservative definite-assignment over a function body's straight-line prefix:
    // flags a read of a scalar local declared without an initializer and not yet
    // assigned (`int x; return x;`, calling an unassigned fn-pointer). Stops at the
    // first control-flow statement, so branchy code is never a false positive.
    void checkUninitPrefix(class BlockStmt* body);
    bool isPrimitiveType(const std::string& type);
    bool isPointerType(const std::string& type);
    std::string getPointeeType(const std::string& pointerType);

    // Type promotion
    std::string promoteType(const std::string& type1, const std::string& type2);
    static std::string intPromoted(const std::string& type);

    // Type normalization
    std::string normalizeType(const std::string& type);

    // Pointer type handling
    bool hasPointerSuffix(const std::string& type) const;
    std::string extractBaseType(const std::string& pointerType) const;
    std::string addPointerSuffix(const std::string& baseType) const;

    // Error reporting
    void error(int line, int col, const std::string& message);
    void warning(int line, int col, const std::string& message);
    void warnAssignInCondition(Expr* cond);  // -Wall: `if (x = 0)`
    // Type-check a loop/if condition: must resolve to bool or numeric. Shared by
    // if/while/do-while/for (the assign-in-condition warning stays at the call site,
    // since `for` intentionally omits it).
    void checkCondition(ASTNode* node, Expr* cond);
    // Convenience: report error at an AST node's position
    void errorAt(ASTNode* node, const std::string& message) {
        error(node->line, node->col, message);
    }

    // Cache for expression types
    std::map<Expr*, std::string> expressionTypes;

    // Capture detection: when non-empty, we are inside a lambda body.
    // Each entry is the set of (name, type) pairs captured so far.
    // IdentExpr visitor adds to the top entry when it finds an outer-scope var.
    std::vector<std::map<std::string, std::string>> captureStack;
    // Parallel to captureStack: the scope count at each lambda's entry. A name
    // is captured when it resolves to a scope index below this boundary (i.e. an
    // enclosing function's param/local), regardless of any same-named global.
    std::vector<int> captureBoundary;
};
