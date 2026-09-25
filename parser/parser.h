#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../lexer/lexer.h"
#include "../ast/ast.h"

// The import prescan shared by one compilation. The preprocessor reports each import
// at its line (PPImportHook); hookFor() preprocesses the imported file right there,
// with the macro table as it stands at that point, and keeps the text here until the
// parser reaches the import. So a #define before `import "x.esk"` reaches x.esk, one
// after it does not, and x.esk's own #defines reach the importer's later lines (C's
// textual #include order).
struct ImportCache {
    std::string stdlibPath;
    std::map<std::string, Macro>* macros = nullptr;
    struct Entry { std::string text; bool ppErr = false; };
    std::map<std::string, Entry> files;   // canonical path → preprocessed text
    std::set<std::string> seen;           // canonical paths preprocessed (incl. roots)
    // The hook for a file whose relative imports resolve against `basedir`.
    PPImportHook hookFor(const std::string& basedir);
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    // Parse the token stream. Returns nullptr (and prints diagnostics to stderr)
    // if any declaration failed to parse.
    std::shared_ptr<Program> parse();

    // Set true when any declaration/import failed to parse. parse() returns
    // nullptr in that case so callers fail loudly instead of silently dropping code.
    bool hadError = false;

    // Directory of the current source file — used to resolve relative imports
    std::string basedir;
    // The source file being parsed; labels diagnostics ("<input>" when empty).
    std::string filename;
    // Root of the Eskiu installation — used to resolve <stdlib> imports
    // Set from $ESKIU_ROOT env var or dirname(argv[0])/../lib/eskiu
    std::string stdlibPath;
    // Shared set of already-imported canonical paths (prevents re-importing). The
    // driver registers each root input too, so a file importing its importer (or
    // a diamond reaching one file by two spellings) parses it once.
    std::set<std::string>* importedFiles = nullptr;
    // The key importedFiles uses: an absolute path with `.`/`..`/symlinks resolved
    // (the path itself when it cannot be resolved).
    static std::string canonicalPath(const std::string& path);
    // Shared preprocessor macro table — lets #defines propagate into imports
    std::map<std::string, Macro>* macros = nullptr;
    // Shared import prescan (see ImportCache); null → an import is preprocessed when
    // the parser reaches it.
    ImportCache* importCache = nullptr;
    // Where an import resolves: `<name>` (no '/') is stdlib/name.esk under the stdlib
    // root, a relative "path" is under `basedir`.
    static std::string resolveImport(const std::string& spec, bool isStdlib,
                                     const std::string& basedir, const std::string& stdlibPath);
    // Shared across all sub-parsers (like importedFiles): type names declared in
    // ANY file, so a cast to a type stays a cast even when that type's defining
    // import was deduplicated via a different path. Without sharing, a file that
    // imports an already-imported module never learned its type names and
    // misparsed `(Type*)x` casts (e.g. `(Future<T>*)0` after `import <future>`).
    std::set<std::string>* sharedTypeNames = nullptr;

private:
    // Backing store for sharedTypeNames in the root parser; sub-parsers point
    // sharedTypeNames at the root's. Names of declared types (structs, enums,
    // unions, aliases) — lets the cast parser recognize (TypeName)expr.
    std::set<std::string> declaredTypeNames;
    // Consume a template-closing '>'. Handles a lexed '>>' (right-shift) at the
    // close of nested templates (List<List<int>>) by splitting it: the inner
    // close turns '>>' into a single '>' left for the outer close.
    void consumeTemplateClose(const char* ctx);
    std::vector<Token> tokens;
    size_t current;
    // Safety net against pathological input: nested expressions/statements beyond
    // kMaxNesting levels are rejected with an error instead of overflowing the stack
    // here or in the later passes, which recurse once per level. The compiler runs on
    // a large stack (see main.cpp), so the limit sits far above realistic code. Long
    // operator chains and statement lists are loops and do not count.
    static constexpr int kMaxNesting = 100000;
    int nesting = 0;
    void enterNesting() {
        if (++nesting > kMaxNesting) {
            --nesting;
            fail("nesting too deep (more than " + std::to_string(kMaxNesting) + " levels)");
        }
    }
    struct NestGuard {
        Parser& p;
        explicit NestGuard(Parser& parser) : p(parser) { p.enterNesting(); }
        ~NestGuard() { --p.nesting; }
    };
    // A `>>` whose first `>` consumeTemplateClose has consumed: while splitActive,
    // the token at splitPos reads as a single `>` (the second half). A speculative
    // parse that backtracks past it restores the whole `>>`, so a later
    // `x < y >> 1` still sees the shift it really is.
    bool splitActive = false;
    size_t splitPos = 0;
    // Backtrack to a saved position, undoing a `>>` split made at or after it.
    void rewindTo(size_t pos);

    // Helper methods
    Token peek() const;
    Token peek_ahead(int n = 1) const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(const std::vector<TokenType>& types);
    Token consume(TokenType type, const std::string& message);
    // Report a syntax error at the current token (or at `at`): records its position
    // for the diagnostic, then throws. Speculative parses catch and discard it.
    [[noreturn]] void fail(const std::string& message);
    [[noreturn]] void fail(const std::string& message, const Token& at);
    int errLine = 0, errCol = 0;
    // Panic-mode recovery: skip to the next token that plausibly starts a top-level
    // declaration (at column 1), so one error does not cascade.
    void skipToNextDecl(size_t declStart);
    bool is_at_end() const;

    // Parsing methods
    std::vector<DeclPtr> parseProgram();

    DeclPtr parseDeclaration();
    DeclPtr parseFunctionDecl();
    std::string parseOperatorToken();   // reads the op after `operator` → "+", "[]", "u-", ...
    DeclPtr parseStructDecl();
    DeclPtr parseExternDecl();
    DeclPtr parseIntrinsicDecl();

    // #pragma pack state: structs declared while currentPack==1 are packed.
    int currentPack = 0;
    std::vector<int> packStack;
    void applyPragma(const Token& tok);
    // `#pragma link("name")` libraries, in first-seen order without duplicates
    // (this file's plus its imports'); the driver links each as -l<name>.
    std::vector<std::string> linkLibs;
    void addLinkLib(const std::string& name);

    StmtPtr parseStatement();
    StmtPtr parseBlockStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseForStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseDoWhileStatement();
    StmtPtr parseReturnStatement();
    StmtPtr parseBreakStatement();
    StmtPtr parseContinueStatement();
    StmtPtr parseSwitchStatement();
    StmtPtr parseMatchStatement();
    // When set, a bare `Name {` is not parsed as a struct literal (so a match
    // subject's trailing `{` opens the match body). Restored after the subject.
    bool noStructLiteral = false;
    // Does a lambda whose return type is not a type keyword start here
    // (`S() {`, `*S(int k) {`, `Box<int>(T x) {`)? A token scan, no parse.
    bool lambdaAhead() const;
    ExprPtr tryParseLambda();
    StmtPtr parseExpressionStatement();

    ExprPtr parseStructInit(const std::string& structName);
    ExprPtr parseExpression();
    ExprPtr parseAssignment();
    ExprPtr parseTernary();
    bool ternaryColonAhead() const;   // disambiguate `cond ? a : b` from postfix `expr?`
    // Binary operators by precedence climbing: parse a unary operand, then fold every
    // operator binding at least `minPrec` (same-precedence operators in a loop, so a
    // long `a + b + c ...` chain costs no recursion; a tighter operator on the right
    // recurses at most once per precedence level).
    ExprPtr parseBinary(int minPrec);
    // Parse an optional `<T, U: Iface + Other>` type-parameter list (shared by fn and
    // struct decls); fills the params and per-param constraints, a no-op with no '<'.
    void parseTypeParams(std::vector<std::string>& typeParams,
                         std::map<std::string, std::vector<std::string>>& typeConstraints);
    ExprPtr parseUnary();
    // At `(`: does a `*`-led parenthesized form open a cast? `(*T)x` is a cast, but
    // `(*p)`, `(*p) - 1`, `(*p)++` and `(*sp).a` dereference a variable.
    bool starParenIsCast() const;
    // Type parameters of the generic function/struct being parsed: `(*T)x` inside
    // `alloc<T>` names a type even though T is not a declared type name.
    std::vector<std::string> typeParamScope;
    bool isTypeName(const std::string& name) const;
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();

    std::string parseType();
    std::vector<std::pair<std::string, std::string>> parseParameterList(
        std::vector<bool>* escaping = nullptr,
        std::vector<std::pair<int, int>>* positions = nullptr);
};
