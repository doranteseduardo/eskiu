#pragma once

#include <string>
#include <vector>

// `eskiuc repl [file.esk ...]`: an interactive session over the LLVM ORC JIT. Each input
// is type-checked against the declarations accepted so far, compiled into its own module
// and run in this process. The files given are loaded first, as with `:load`. See repl.cpp.
int runRepl(const std::vector<std::string>& files);
