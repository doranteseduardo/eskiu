#pragma once

#include <string>
#include <unordered_map>
#include <memory>

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
    INTERFACE,
    ENUM,
    FN,
    FOR,
    IN,
    WHILE,
    IF,
    ELSE,
    SWITCH,
    CASE,
    DEFAULT,
    BREAK,
    RETURN,
    IMPORT,
    EXTERN,
    ALLOC,
    FREE,
    NULL_KW,
    TRUE,
    FALSE,
    THREAD,
    SPAWN,
    MUTEX,
    TRY,
    CATCH,
    FINALLY,
    THROW,
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
    COLON,
    ARROW,
    ELLIPSIS,

    // Literals
    INT_LIT,
    FLOAT_LIT,
    STRING_LIT,
    CHAR_LIT,
    IDENT,

    // Special
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
    explicit Lexer(const std::string& source);

    Token next_token();
    void print_all_tokens();

private:
    std::string source;
    size_t current;
    int line;
    int column;

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
