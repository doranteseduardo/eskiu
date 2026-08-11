#include "lexer.h"
#include <iostream>
#include <cctype>
#include <map>
#include "preprocessor.h"

std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"let", TokenType::LET},
    {"int", TokenType::INT},
    {"float", TokenType::FLOAT},
    {"double", TokenType::DOUBLE},
    {"bool", TokenType::BOOL},
    {"char", TokenType::CHAR},
    {"string", TokenType::STRING},
    {"void", TokenType::VOID},
    {"struct", TokenType::STRUCT},
    {"packed", TokenType::PACKED},
    {"union",  TokenType::UNION},
    {"interface", TokenType::INTERFACE},
    {"enum", TokenType::ENUM},
    {"fn", TokenType::FN},
    {"operator", TokenType::OPERATOR},
    {"asm", TokenType::ASM},
    {"volatile",       TokenType::VOLATILE},
    {"static",         TokenType::STATIC},
    {"escaping",       TokenType::ESCAPING},
    {"must_use",       TokenType::MUST_USE},
    {"async",          TokenType::ASYNC},
    {"await",          TokenType::AWAIT},
    {"const",          TokenType::CONST},
    {"sizeof",         TokenType::SIZEOF},
    {"free_closure",   TokenType::FREE_CLOSURE},
    {"thread_create",  TokenType::THREAD_CREATE},
    {"thread_join",    TokenType::THREAD_JOIN},
    {"for", TokenType::FOR},
    {"in", TokenType::IN},
    {"while", TokenType::WHILE},
    {"do", TokenType::DO},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"switch", TokenType::SWITCH},
    {"match", TokenType::MATCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK},
    {"return", TokenType::RETURN},
    {"import", TokenType::IMPORT},
    {"extern", TokenType::EXTERN},
    {"intrinsic", TokenType::INTRINSIC},
    {"alloc_with", TokenType::ALLOC_WITH},
    {"null", TokenType::NULL_KW},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"try", TokenType::TRY},
    {"catch", TokenType::CATCH},
    {"finally", TokenType::FINALLY},
    {"throw",    TokenType::THROW},
    {"defer",    TokenType::DEFER},
    {"errdefer", TokenType::ERRDEFER},
    {"continue", TokenType::CONTINUE},
    {"int8",   TokenType::INT8},
    {"int16",  TokenType::INT16},
    {"int32",  TokenType::INT32},
    {"int64",  TokenType::INT64},
    {"uint",   TokenType::UINT},
    {"uint8",  TokenType::UINT8},
    {"uint16", TokenType::UINT16},
    {"uint32", TokenType::UINT32},
    {"uint64", TokenType::UINT64},
};

Lexer::Lexer(const std::string& source, std::map<std::string, Macro>* macros,
             const std::string& filename)
    : current(0), line(1), column(1) {
    std::map<std::string, Macro> local;
    preprocess(source, macros ? *macros : local, this->source, filename, this->hadError);
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source[current];
}

char Lexer::peek_next() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

char Lexer::advance() {
    char c = peek();
    current++;
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

bool Lexer::is_at_end() const {
    return current >= source.length();
}

void Lexer::skip_whitespace() {
    while (!is_at_end() && std::isspace(peek())) {
        advance();
    }
}

void Lexer::skip_comment() {
    if (peek() == '/' && peek_next() == '/') {
        // Single line comment
        while (!is_at_end() && peek() != '\n') {
            advance();
        }
    } else if (peek() == '/' && peek_next() == '*') {
        // Multi-line comment
        int start_line = line, start_col = column;
        advance(); // /
        advance(); // *
        bool closed = false;
        while (!is_at_end()) {
            if (peek() == '*' && peek_next() == '/') {
                advance(); // *
                advance(); // /
                closed = true;
                break;
            }
            advance();
        }
        if (!closed) lexError(start_line, start_col, "unterminated block comment");
    }
}

Token Lexer::read_number() {
    int start_line = line;
    int start_col = column;
    std::string num;

    // Handle floats with leading dot
    if (peek() == '.') {
        num += advance();
        while (!is_at_end() && std::isdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::FLOAT_LIT, num, start_line, start_col);
    }

    // Integer part
    while (!is_at_end() && std::isdigit(peek())) {
        num += advance();
    }

    // Hex literal: 0x... (e.g. 0xFF)
    if (num == "0" && !is_at_end() && (peek() == 'x' || peek() == 'X')) {
        num += advance(); // consume 'x'
        while (!is_at_end() && std::isxdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::INT_LIT, num, start_line, start_col);
    }

    // Check for decimal point or exponent
    if (!is_at_end() && peek() == '.' && std::isdigit(peek_next())) {
        num += advance(); // .
        while (!is_at_end() && std::isdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::FLOAT_LIT, num, start_line, start_col);
    }

    return Token(TokenType::INT_LIT, num, start_line, start_col);
}

void Lexer::lexError(int errLine, int errCol, const std::string& msg) {
    std::cerr << "error: " << errLine << ":" << errCol << ": " << msg << std::endl;
    hadError = true;
}

// Hex value of a digit, or -1 if it is not a hex digit. For `\xNN` escapes.
static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode a backslash escape to the byte it denotes. Shared by string and char
// literals so both accept the same set. An unrecognized escape yields the char
// itself (so `\q` is `q`), matching C's lenient handling. `\xNN` (one or two hex
// digits) is handled separately by the readers, since it consumes extra chars.
static char decodeEscape(char e) {
    switch (e) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case 'f':  return '\f';
        case 'v':  return '\v';
        case '0':  return '\0';
        case '\\': return '\\';
        case '"':  return '"';
        case '\'': return '\'';
        default:   return e;
    }
}

// Decode one escape sequence, assuming the leading '\' has already been consumed:
// \xNN (one or two hex digits) yields that byte; a single-char escape resolves via
// decodeEscape (an unknown escape is the character itself).
char Lexer::readEscape() {
    char e = advance();
    if (e == 'x' && hexDigit(peek()) >= 0) {
        int b = hexDigit(advance());
        if (hexDigit(peek()) >= 0) b = b * 16 + hexDigit(advance());
        return (char)b;
    }
    return decodeEscape(e);
}

Token Lexer::read_string() {
    int start_line = line;
    int start_col = column;
    std::string str;

    advance(); // consume opening "

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\' && peek_next() != '\0') {
            advance();                       // consume '\'
            str += readEscape();
        } else {
            str += advance();
        }
    }

    if (is_at_end()) {
        lexError(start_line, start_col, "unterminated string literal");
    } else {
        advance(); // consume closing "
    }

    return Token(TokenType::STRING_LIT, str, start_line, start_col);
}

Token Lexer::read_char() {
    int start_line = line;
    int start_col = column;
    advance(); // consume opening '

    std::string ch;
    if (is_at_end()) {
        lexError(start_line, start_col, "unterminated character literal");
        return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
    }
    if (peek() == '\'') {
        lexError(start_line, start_col, "empty character literal");
        advance(); // consume closing '
        return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
    }
    if (peek() == '\\') {
        advance();                           // consume '\'
        ch += readEscape();
    } else {
        ch += advance();
    }

    if (!is_at_end() && peek() == '\'') {
        advance(); // consume closing '
    } else {
        // Unterminated, or more than one character before the closing quote.
        bool unterminated = is_at_end() || peek() == '\n';
        lexError(start_line, start_col,
                 unterminated ? "unterminated character literal"
                              : "character literal must contain a single character");
        // Recover: skip to the closing ' (or end of line) so the rest of the
        // malformed literal does not mis-lex into stray tokens.
        while (!is_at_end() && peek() != '\'' && peek() != '\n') advance();
        if (!is_at_end() && peek() == '\'') advance();
    }

    return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
}

Token Lexer::read_identifier() {
    int start_line = line;
    int start_col = column;
    std::string ident;

    while (!is_at_end() && (std::isalnum(peek()) || peek() == '_')) {
        ident += advance();
    }

    // Check if it's a keyword
    auto it = keywords.find(ident);
    if (it != keywords.end()) {
        return Token(it->second, ident, start_line, start_col);
    }

    return Token(TokenType::IDENT, ident, start_line, start_col);
}

Token Lexer::next_token() {
    skip_whitespace();

    // Handle comments
    while (!is_at_end() && peek() == '/' && (peek_next() == '/' || peek_next() == '*')) {
        skip_comment();
        skip_whitespace();
    }

    if (is_at_end()) {
        return Token(TokenType::EOF_TOKEN, "", line, column);
    }

    int start_line = line;
    int start_col = column;
    char c = peek();

    // Numbers
    if (std::isdigit(c)) {
        return read_number();
    }

    // Strings
    if (c == '"') {
        return read_string();
    }

    // Characters
    if (c == '\'') {
        return read_char();
    }

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return read_identifier();
    }

    // Compiler directive surviving the preprocessor (e.g. `#pragma pack(1)`).
    // The whole line after '#' becomes the token value, e.g. "pragma pack(1)".
    if (c == '#') {
        advance();  // consume '#'
        std::string text;
        while (!is_at_end() && peek() != '\n') text += advance();
        return Token(TokenType::PRAGMA, text, start_line, start_col);
    }

    // Operators and delimiters
    advance();

    switch (c) {
        case '+':
            if (!is_at_end() && peek() == '+') { advance(); return Token(TokenType::PLUS_PLUS,   "++", start_line, start_col); }
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::PLUS_EQ,    "+=", start_line, start_col); }
            return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-':
            if (!is_at_end() && peek() == '-') { advance(); return Token(TokenType::MINUS_MINUS, "--", start_line, start_col); }
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::MINUS_EQ,   "-=", start_line, start_col); }
            if (!is_at_end() && peek() == '>') { advance(); return Token(TokenType::ARROW,       "->", start_line, start_col); }
            return Token(TokenType::MINUS, "-", start_line, start_col);
        case '*':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::STAR_EQ,    "*=", start_line, start_col); }
            return Token(TokenType::STAR, "*", start_line, start_col);
        case '/':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::SLASH_EQ,   "/=", start_line, start_col); }
            return Token(TokenType::SLASH, "/", start_line, start_col);
        case '%':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::PERCENT_EQ, "%=", start_line, start_col); }
            return Token(TokenType::PERCENT, "%", start_line, start_col);
        case '=':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::EQEQ, "==", start_line, start_col);
            }
            return Token(TokenType::EQ, "=", start_line, start_col);
        case '!':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::NE, "!=", start_line, start_col);
            }
            return Token(TokenType::NOT, "!", start_line, start_col);
        case '<':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::LE, "<=", start_line, start_col);
            }
            if (!is_at_end() && peek() == '<') {
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    return Token(TokenType::LSHIFT_EQ, "<<=", start_line, start_col);
                }
                return Token(TokenType::LSHIFT, "<<", start_line, start_col);
            }
            return Token(TokenType::LT, "<", start_line, start_col);
        case '>':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::GE, ">=", start_line, start_col);
            }
            if (!is_at_end() && peek() == '>') {
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    return Token(TokenType::RSHIFT_EQ, ">>=", start_line, start_col);
                }
                return Token(TokenType::RSHIFT, ">>", start_line, start_col);
            }
            return Token(TokenType::GT, ">", start_line, start_col);
        case '&':
            if (!is_at_end() && peek() == '&') {
                advance();
                return Token(TokenType::AND, "&&", start_line, start_col);
            }
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::AMP_EQ, "&=", start_line, start_col);
            }
            return Token(TokenType::AMPERSAND, "&", start_line, start_col);
        case '|':
            if (!is_at_end() && peek() == '|') {
                advance();
                return Token(TokenType::OR, "||", start_line, start_col);
            }
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::PIPE_EQ, "|=", start_line, start_col);
            }
            return Token(TokenType::PIPE, "|", start_line, start_col);
        case '^':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::CARET_EQ, "^=", start_line, start_col);
            }
            return Token(TokenType::CARET, "^", start_line, start_col);
        case '~': return Token(TokenType::TILDE, "~", start_line, start_col);
        case '{': return Token(TokenType::LBRACE, "{", start_line, start_col);
        case '}': return Token(TokenType::RBRACE, "}", start_line, start_col);
        case '(': return Token(TokenType::LPAREN, "(", start_line, start_col);
        case ')': return Token(TokenType::RPAREN, ")", start_line, start_col);
        case '[': return Token(TokenType::LBRACKET, "[", start_line, start_col);
        case ']': return Token(TokenType::RBRACKET, "]", start_line, start_col);
        case ';': return Token(TokenType::SEMICOLON, ";", start_line, start_col);
        case ',': return Token(TokenType::COMMA, ",", start_line, start_col);
        case ':': return Token(TokenType::COLON, ":", start_line, start_col);
        case '?': return Token(TokenType::QUESTION, "?", start_line, start_col);
        case '.':
            if (!is_at_end() && peek() == '.' && peek_next() == '.') {
                advance();
                advance();
                return Token(TokenType::ELLIPSIS, "...", start_line, start_col);
            }
            if (!is_at_end() && peek() == '.') {          // `..` half-open range
                advance();
                return Token(TokenType::RANGE, "..", start_line, start_col);
            }
            return Token(TokenType::DOT, ".", start_line, start_col);
        default:
            return Token(TokenType::UNKNOWN, std::string(1, c), start_line, start_col);
    }
}

void Lexer::print_all_tokens() {
    current = 0;
    line = 1;
    column = 1;

    Token tok = next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        std::cout << "Line " << tok.line << ", Col " << tok.column << ": "
                  << "TokenType::" << static_cast<int>(tok.type)
                  << " Value: '" << tok.value << "'" << std::endl;
        tok = next_token();
    }
}
