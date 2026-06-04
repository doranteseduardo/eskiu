#include "lexer.h"
#include <iostream>
#include <cctype>

std::string tokenTypeToString(TokenType type) {
    switch (type) {
        // Keywords
        case TokenType::LET: return "LET";
        case TokenType::INT: return "INT";
        case TokenType::FLOAT: return "FLOAT";
        case TokenType::DOUBLE: return "DOUBLE";
        case TokenType::BOOL: return "BOOL";
        case TokenType::CHAR: return "CHAR";
        case TokenType::STRING: return "STRING";
        case TokenType::VOID: return "VOID";
        case TokenType::STRUCT: return "STRUCT";
        case TokenType::INTERFACE: return "INTERFACE";
        case TokenType::ENUM: return "ENUM";
        case TokenType::FN: return "FN";
        case TokenType::ASM: return "ASM";
        case TokenType::VOLATILE:      return "VOLATILE";
        case TokenType::THREAD_CREATE: return "THREAD_CREATE";
        case TokenType::THREAD_JOIN:   return "THREAD_JOIN";
        case TokenType::FOR: return "FOR";
        case TokenType::IN: return "IN";
        case TokenType::WHILE: return "WHILE";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::SWITCH: return "SWITCH";
        case TokenType::CASE: return "CASE";
        case TokenType::DEFAULT: return "DEFAULT";
        case TokenType::BREAK: return "BREAK";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::ALLOC: return "ALLOC";
        case TokenType::FREE: return "FREE";
        case TokenType::NULL_KW: return "NULL";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::THREAD: return "THREAD";
        case TokenType::SPAWN: return "SPAWN";
        case TokenType::MUTEX: return "MUTEX";
        case TokenType::TRY: return "TRY";
        case TokenType::CATCH: return "CATCH";
        case TokenType::FINALLY: return "FINALLY";
        case TokenType::THROW: return "THROW";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::INT8: return "INT8";
        case TokenType::INT16: return "INT16";
        case TokenType::INT32: return "INT32";
        case TokenType::INT64: return "INT64";
        case TokenType::UINT: return "UINT";
        case TokenType::UINT8: return "UINT8";
        case TokenType::UINT16: return "UINT16";
        case TokenType::UINT32: return "UINT32";
        case TokenType::UINT64: return "UINT64";
        // Operators
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::PLUS_EQ: return "PLUS_EQ";
        case TokenType::MINUS_EQ: return "MINUS_EQ";
        case TokenType::STAR_EQ: return "STAR_EQ";
        case TokenType::SLASH_EQ: return "SLASH_EQ";
        case TokenType::PERCENT_EQ: return "PERCENT_EQ";
        case TokenType::EQ: return "EQ";
        case TokenType::EQEQ: return "EQEQ";
        case TokenType::NE: return "NE";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::LE: return "LE";
        case TokenType::GE: return "GE";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::PIPE: return "PIPE";
        case TokenType::CARET: return "CARET";
        case TokenType::TILDE: return "TILDE";
        case TokenType::LSHIFT: return "LSHIFT";
        case TokenType::RSHIFT: return "RSHIFT";
        // Delimiters
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::COLON: return "COLON";
        case TokenType::ARROW: return "ARROW";
        case TokenType::ELLIPSIS: return "ELLIPSIS";
        // Literals
        case TokenType::INT_LIT: return "INT_LIT";
        case TokenType::FLOAT_LIT: return "FLOAT_LIT";
        case TokenType::STRING_LIT: return "STRING_LIT";
        case TokenType::CHAR_LIT: return "CHAR_LIT";
        case TokenType::IDENT: return "IDENT";
        // Special
        case TokenType::EOF_TOKEN: return "EOF";
        case TokenType::UNKNOWN: return "UNKNOWN";
        default: return "???";
    }
}

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
    {"interface", TokenType::INTERFACE},
    {"enum", TokenType::ENUM},
    {"fn", TokenType::FN},
    {"asm", TokenType::ASM},
    {"volatile",       TokenType::VOLATILE},
    {"thread_create",  TokenType::THREAD_CREATE},
    {"thread_join",    TokenType::THREAD_JOIN},
    {"for", TokenType::FOR},
    {"in", TokenType::IN},
    {"while", TokenType::WHILE},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK},
    {"return", TokenType::RETURN},
    {"import", TokenType::IMPORT},
    {"extern", TokenType::EXTERN},
    {"alloc", TokenType::ALLOC},
    {"free", TokenType::FREE},
    {"null", TokenType::NULL_KW},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"thread", TokenType::THREAD},
    {"spawn", TokenType::SPAWN},
    {"mutex", TokenType::MUTEX},
    {"try", TokenType::TRY},
    {"catch", TokenType::CATCH},
    {"finally", TokenType::FINALLY},
    {"throw",    TokenType::THROW},
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

Lexer::Lexer(const std::string& source)
    : source(source), current(0), line(1), column(1) {}

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
        advance(); // /
        advance(); // *
        while (!is_at_end()) {
            if (peek() == '*' && peek_next() == '/') {
                advance(); // *
                advance(); // /
                break;
            }
            advance();
        }
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

Token Lexer::read_string() {
    int start_line = line;
    int start_col = column;
    std::string str;

    advance(); // consume opening "

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\' && peek_next() != '\0') {
            advance();
            char escaped = advance();
            // Handle escape sequences
            switch (escaped) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '"': str += '"'; break;
                default: str += escaped;
            }
        } else {
            str += advance();
        }
    }

    if (!is_at_end()) {
        advance(); // consume closing "
    }

    return Token(TokenType::STRING_LIT, str, start_line, start_col);
}

Token Lexer::read_char() {
    int start_line = line;
    int start_col = column;
    advance(); // consume opening '

    std::string ch;
    if (peek() == '\\') {
        advance();
        char escaped = advance();
        switch (escaped) {
            case 'n': ch += '\n'; break;
            case 't': ch += '\t'; break;
            case '\\': ch += '\\'; break;
            default: ch += escaped;
        }
    } else {
        ch += advance();
    }

    if (!is_at_end() && peek() == '\'') {
        advance(); // consume closing '
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

    // Operators and delimiters
    advance();

    switch (c) {
        case '+':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::PLUS_EQ,    "+=", start_line, start_col); }
            return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-':
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
                return Token(TokenType::RSHIFT, ">>", start_line, start_col);
            }
            return Token(TokenType::GT, ">", start_line, start_col);
        case '&':
            if (!is_at_end() && peek() == '&') {
                advance();
                return Token(TokenType::AND, "&&", start_line, start_col);
            }
            return Token(TokenType::AMPERSAND, "&", start_line, start_col);
        case '|':
            if (!is_at_end() && peek() == '|') {
                advance();
                return Token(TokenType::OR, "||", start_line, start_col);
            }
            return Token(TokenType::PIPE, "|", start_line, start_col);
        case '^': return Token(TokenType::CARET, "^", start_line, start_col);
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
        case '.':
            if (!is_at_end() && peek() == '.' && peek_next() == '.') {
                advance();
                advance();
                return Token(TokenType::ELLIPSIS, "...", start_line, start_col);
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
