#pragma once

// The text preprocessing pass run before lexing: object-/function-like
// #define/#undef and #ifdef/#ifndef/#else/#endif. Split out of lexer.cpp;
// the lexer calls preprocess() before tokenizing. See preprocessor.cpp.

#include <map>
#include <string>
#include "lexer.h"   // Macro

void preprocess(const std::string& src,
                std::map<std::string, Macro>& defines,
                std::string& result,
                const std::string& filename,
                bool& hadErr);
