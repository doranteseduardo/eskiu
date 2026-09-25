#pragma once

// Self-contained driver-support utilities split out of main.cpp: filesystem /
// path helpers, the `fmt` reindenter, the C-linker driver + executable runner,
// and the lex+parse program loader. None of these touch the CLI option globals
// (those stay in main.cpp). See main_support.cpp.

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "lexer/lexer.h"

class Program;

// stdlib root, set once at startup (main) and read by loadProgram.
extern std::string stdlibRoot;

std::string resolveStdlibPath();
std::string readFile(const std::string& filename);
std::string dirOf(const std::string& path);
std::string formatSource(const std::string& src);
int runFmt(const std::vector<std::string>& files, bool check);
// Seed the predefined macros every compile mode shares: the platform macro that
// follows `triple` (or the build host when empty) and __ESKIU_FREESTANDING__.
void seedPredefinedMacros(std::map<std::string, Macro>& macros, const std::string& triple,
                          bool freestanding);
std::shared_ptr<Program> loadProgram(const std::string& filename, const std::string& triple,
                                     bool freestanding);
bool endsWith(const std::string& s, const std::string& suffix);
std::string findCDriver(bool sanitized = false);
bool linkExecutable(const std::string& obj, const std::string& out,
                    const std::vector<std::string>& libs,
                    const std::vector<std::string>& paths,
                    const std::vector<std::string>& extra,
                    bool sanitized = false);
int runExecutable(const std::string& exe, const std::vector<std::string>& progArgs);
