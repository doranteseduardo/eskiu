#pragma once

// Internal helpers shared across the parser's split translation units
// (parser.cpp + parse_{decl,stmt,expr}.cpp). Not part of the public Parser API.

#include <memory>
#include "../lexer/lexer.h"

// Stamp any AST node with position from tok.
template<typename T>
std::shared_ptr<T> withPos(std::shared_ptr<T> node, const Token& tok) {
    node->line = tok.line; node->col = tok.column; return node;
}
