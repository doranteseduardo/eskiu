#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>

// A preprocessor macro: object-like (#define MAX 100) or function-like
// (#define SQ(x) ((x)*(x))). The table is shared across files so that a
// #define propagates through import / multi-file compilation.
struct Macro {
    bool isFunction = false;
    std::vector<std::string> params;
    std::string body;
};

enum class TokenType {
    // Keywords
    LET,
    INT,
    FLOAT,
    DOUBLE,
    BOOL,
    CHAR,
    STRING,
    VOID,
    STRUCT,
    PACKED,
    UNION,
    INTERFACE,
    ENUM,
    FN,
    ASM,
    VOLATILE,
    STATIC,
    ESCAPING,
    ASYNC,
    AWAIT,
    CONST,
    THREAD_CREATE,
    THREAD_JOIN,
    FOR,
    IN,
    WHILE,
    DO,
    IF,
    ELSE,
    SWITCH,
    MATCH,
    CASE,
    DEFAULT,
    BREAK,
    RETURN,
    IMPORT,
    EXTERN,
    INTRINSIC,
    ALLOC_WITH,
    NULL_KW,
    TRUE,
    FALSE,
    SIZEOF,
    FREE_CLOSURE,
    TRY,
    CATCH,
    FINALLY,
    THROW,
    DEFER,
    ERRDEFER,
    CONTINUE,
    INT8,
    INT16,
    INT32,
    INT64,
    UINT,
    UINT8,
    UINT16,
    UINT32,
    UINT64,

    // Operators
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    PLUS_PLUS,
    MINUS_MINUS,
    PLUS_EQ,
    MINUS_EQ,
    STAR_EQ,
    SLASH_EQ,
    PERCENT_EQ,
    EQ,
    EQEQ,
    NE,
    LT,
    GT,
    LE,
    GE,
    AND,
    OR,
    NOT,
    AMPERSAND,
    PIPE,
    CARET,
    TILDE,
    LSHIFT,
    RSHIFT,
    AMP_EQ,
    PIPE_EQ,
    CARET_EQ,
    LSHIFT_EQ,
    RSHIFT_EQ,

    // Delimiters
    LBRACE,
    RBRACE,
    LPAREN,
    RPAREN,
    LBRACKET,
    RBRACKET,
    SEMICOLON,
    COMMA,
    DOT,
    RANGE,         // `..` — half-open range (e.g. `for (i in 0..10)`)
    COLON,
    ARROW,
    ELLIPSIS,
    QUESTION,

    // Literals
    INT_LIT,
    FLOAT_LIT,
    STRING_LIT,
    CHAR_LIT,
    IDENT,

    // Special
    PRAGMA,        // a `#pragma ...` directive line (value = text after '#')
    EOF_TOKEN,
    UNKNOWN,
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;

    Token(TokenType type = TokenType::UNKNOWN, const std::string& value = "",
          int line = 0, int column = 0)
        : type(type), value(value), line(line), column(column) {}
};

// Helper function to convert TokenType to string
std::string tokenTypeToString(TokenType type);

class Lexer {
public:
    // `macros` is an optional shared macro table: when provided, #defines from
    // earlier files persist so they propagate across import / multi-file builds.
    // `filename` (when known) is exposed to the preprocessor as `__FILE__`.
    explicit Lexer(const std::string& source,
                   std::map<std::string, Macro>* macros = nullptr,
                   const std::string& filename = "");

    Token next_token();
    void print_all_tokens();

    // Set when a lexical error (unterminated literal/comment, malformed char)
    // was reported. The driver checks this and aborts before parsing.
    bool hadError = false;

private:
    std::string source;
    size_t current;
    int line;
    int column;

    // Report a lexical error at the given position and set hadError.
    void lexError(int errLine, int errCol, const std::string& msg);

    char peek() const;
    char peek_next() const;
    char advance();
    void skip_whitespace();
    void skip_comment();
    bool is_at_end() const;

    Token read_number();
    Token read_string();
    Token read_char();
    Token read_identifier();

    static std::unordered_map<std::string, TokenType> keywords;
};
