#include <cstdint>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <climits>
#include <set>
#include <cerrno>
#include <filesystem>
#ifndef _WIN32
  #include <unistd.h>
  #include <sys/wait.h>
#endif
#ifdef __APPLE__
  #include <mach-o/dyld.h>
#elif defined(__linux__)
  #include <unistd.h>
#elif defined(_WIN32)
  // Declare just GetModuleFileNameA instead of including <windows.h>, whose macros
  // (VOID, CONST, TRUE, FALSE, FLOAT, IN, INTERFACE, ...) collide with our TokenType
  // enum. Plain types: HMODULE = void*, LPSTR = char*, DWORD = unsigned long.
  extern "C" __declspec(dllimport) unsigned long __stdcall
      GetModuleFileNameA(void* hModule, char* lpFilename, unsigned long nSize);
#endif
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/FileSystem.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"
#include "sema/type_checker.h"
#include "sema/async_transform.h"
#include "codegen/codegen.h"
#include "main_support.h"

// Definition of the shared stdlib-root global (declared in main_support.h).
std::string stdlibRoot;

// Resolve stdlib root: $ESKIU_ROOT env var, or dirname(argv[0])/../lib/eskiu
std::string resolveStdlibPath() {
    const char* env = std::getenv("ESKIU_ROOT");
    if (env && *env) return std::string(env);

    // Deduce from binary location
    char buf[4096] = {};
#ifdef __APPLE__
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
#elif defined(__linux__)
    if (readlink("/proc/self/exe", buf, sizeof(buf) - 1) > 0) {
#elif defined(_WIN32)
    if (GetModuleFileNameA(nullptr, buf, (unsigned long)sizeof(buf)) > 0) {
#else
    if (false) {
#endif
        std::string binPath(buf);
        // Accept either separator: GetModuleFileNameA returns backslashes.
        size_t slash = binPath.find_last_of("/\\");
        if (slash != std::string::npos) {
            // binary is at <prefix>/bin/eskiuc → look for <prefix>/lib/eskiu
            std::string binDir = binPath.substr(0, slash);
            size_t parentSlash = binDir.find_last_of("/\\");
            if (parentSlash != std::string::npos) {
                std::string prefix = binDir.substr(0, parentSlash);
                std::string candidate = prefix + "/lib/eskiu";
                std::ifstream probe(candidate + "/stdlib/result.esk");
                if (probe.good()) return candidate;
            }
            // Also try sibling directory (dev: build/ next to stdlib/)
            std::string devCandidate = binDir + "/..";
            std::ifstream probe2(devCandidate + "/stdlib/result.esk");
            if (probe2.good()) return devCandidate;
        }
    }
    return "";
}

// Read file contents into string
std::string readFile(const std::string& filename) {
    std::error_code ec;
    if (std::filesystem::is_directory(filename, ec)) {
        std::cerr << "error: '" << filename << "' is a directory, not a source file" << std::endl;
        exit(1);
    }
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "error: could not open file '" << filename << "'" << std::endl;
        exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Directory portion of a path, or "." if there is no slash.
std::string dirOf(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

// ── eskiuc fmt ──────────────────────────────────────────────────────────────
// A conservative, comment-preserving reindenter. It normalizes only what is
// unambiguous and can never change a program's meaning:
//   * leading indentation = 4 spaces per `{`-nesting level
//   * trailing whitespace stripped, except after a trailing `\` (trimming it there
//     would turn the line into a continuation)
//   * blank lines kept, so every line keeps its number (`__LINE__`, diagnostics);
//     only blank lines at the end of the file are dropped
//   * exactly one final newline
//   * a line after one ending in `\` (a continuation) is kept byte for byte
// Each line's *content* (operators, inner spacing, comments, and every byte of a
// string or char literal, including a stray `\r` and every line of a string that
// spans lines) is preserved verbatim; only the
// `\r` of a CRLF line ending is taken off. Braces inside strings, char literals and
// comments are ignored, so formatting is idempotent and safe. Preprocessor lines
// (`#…`) sit at column 0 and do not affect nesting. The file's line ending (LF or
// CRLF, judged by its first line break) is preserved.
std::string formatSource(const std::string& src) {
    size_t firstNl = src.find('\n');
    const std::string eol = (firstNl != std::string::npos && firstNl > 0 && src[firstNl - 1] == '\r')
                                ? "\r\n" : "\n";
    std::vector<std::string> lines;
    { std::string cur;
      for (char c : src) {
          if (c != '\n') { cur += c; continue; }
          if (!cur.empty() && cur.back() == '\r') cur.pop_back();
          lines.push_back(cur); cur.clear();
      }
      lines.push_back(cur); }

    auto rtrim = [](const std::string& s) {
        size_t b = s.find_last_not_of(" \t");
        if (b == std::string::npos) return std::string();
        if (s[b] == '\\') b = s.size() - 1;       // `\ ` must not become a continuation
        return s.substr(0, b + 1);
    };

    std::string out;
    int depth = 0;            // current `{` nesting
    bool inBlock = false;     // inside a /* … */ block comment
    int pendingBlank = 0;     // blank lines buffered (dropped only at the end of the file)
    bool inStr = false;       // a string literal continues onto the next line
    bool prevCont = false;    // the previous line ends with `\`: this one continues it
    bool ppCont = false;      // the continued line is a preprocessor line (no nesting)
    // The preprocessor's view, which decides what the lexer sees: a `#` line inside a
    // multi-line string is string text, only one branch of a conditional reaches the
    // lexer (so each branch, and the code after `#endif`, starts from the string state
    // at the `#if`), and a directive that leaves a `/*` open opens a comment for the
    // following lines. A string opened inside a conditional may be in a branch that is
    // not compiled (a stray quote in `#if 0`), so there a `#` line keeps its bytes but
    // still counts as a directive.
    std::vector<bool> condStr;
    bool strSeen = false;     // inStr was already set at the start of the previous line
    bool strInCond = false;   // the open string began inside a conditional
    std::string ppLine;       // the directive's logical line (continuations joined)
    bool firstLine = true;
    auto directiveOpensComment = [](const std::string& l) {
        size_t n = l.size();
        for (size_t i = 0; i < n; ++i) {
            char c = l[i];
            if (c == '"' || c == '\'') {
                for (++i; i < n && l[i] != c; ++i) if (l[i] == '\\' && i + 1 < n) ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && l[i + 1] == '/') return false;
            if (c == '/' && i + 1 < n && l[i + 1] == '*') {
                size_t e = l.find("*/", i + 2);
                if (e == std::string::npos) return true;
                i = e + 1;
            }
        }
        return false;
    };

    // Update nesting from t[from..] (code state), skipping strings/chars/comments.
    // Strings are checked first, so a `/*` or `}` inside a literal is ignored. A
    // string still open at the end of the line leaves inStr set.
    auto scanNesting = [&](const std::string& t, size_t from) {
        for (size_t i = from; i < t.size(); ++i) {
            char c = t[i];
            if (inStr || c == '"' || c == '\'') {                            // string / char literal
                char q = inStr ? '"' : c;
                if (!inStr) ++i;
                inStr = false;
                while (i < t.size() && t[i] != q) {
                    if (t[i] == '\\' && i + 1 < t.size()) { ++i; }           // skip the escaped char
                    ++i;
                }
                if (i >= t.size() && q == '"') inStr = true;
                continue;
            }
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') break;       // line comment
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {            // block comment
                inBlock = true;
                for (size_t j = i + 2; j + 1 < t.size(); ++j)
                    if (t[j] == '*' && t[j + 1] == '/') { inBlock = false; i = j + 1; break; }
                if (inBlock) break;                                          // runs onto next line
                continue;
            }
            if (c == '{') depth++;
            else if (c == '}') depth--;
        }
        if (depth < 0) depth = 0;
    };

    for (const std::string& raw : lines) {
        bool cont = prevCont;
        bool shebang = firstLine && raw.compare(0, 2, "#!") == 0;
        firstLine = false;
        prevCont = !raw.empty() && raw.back() == '\\';
        if (inStr && !strSeen) strInCond = !condStr.empty();
        strSeen = inStr;
        size_t h0 = raw.find_first_not_of(" \t");
        bool directive = !inBlock && !cont && (!inStr || strInCond) && h0 != std::string::npos && raw[h0] == '#';
        if (inBlock) {                       // verbatim until the comment closes
            out += raw; out += eol;
            for (size_t i = 0; i + 1 < raw.size(); ++i)
                if (raw[i] == '*' && raw[i + 1] == '/') {
                    inBlock = false;
                    scanNesting(raw, i + 2);         // code after the `*/` still nests
                    break;
                }
            continue;
        }
        if (inStr && !directive) {           // inside a multi-line string: bytes verbatim
            std::string t = raw;
            scanNesting(t, 0);
            if (!inStr) t = rtrim(t);
            out += t; out += eol;
            continue;
        }
        if (cont) {                          // a continuation line: bytes verbatim
            out += raw; out += eol;
            if (!ppCont) scanNesting(raw, 0);
            else if (prevCont) ppLine += raw.substr(0, raw.size() - 1);
            else inBlock = directiveOpensComment(ppLine + raw);
            continue;
        }
        size_t a = raw.find_first_not_of(" \t");
        if (a == std::string::npos) { pendingBlank++; continue; }
        std::string t = raw.substr(a);

        for (; pendingBlank > 0; pendingBlank--) out += eol;   // keep every blank line

        ppCont = t[0] == '#';
        if (t[0] == '#') {                   // preprocessor line: column 0, no nesting change
            out += inStr ? raw : rtrim(t); out += eol;
            size_t k = 1;
            while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) k++;
            size_t ks = k;
            while (k < t.size() && (std::isalnum((unsigned char)t[k]) || t[k] == '_')) k++;
            std::string kw = t.substr(ks, k - ks);
            if (kw == "if" || kw == "ifdef" || kw == "ifndef") condStr.push_back(inStr);
            else if ((kw == "elif" || kw == "else") && !condStr.empty()) inStr = condStr.back();
            else if (kw == "endif" && !condStr.empty()) { inStr = condStr.back(); condStr.pop_back(); }
            if (prevCont) ppLine = t.substr(0, t.size() - 1);
            else if (!shebang) inBlock = directiveOpensComment(t);
            continue;
        }

        // This line's indent dedents for each leading `}`.
        int lead = depth;
        for (char c : t) { if (c == '}') lead--; else break; }
        if (lead < 0) lead = 0;
        scanNesting(t, 0);
        if (!inStr) t = rtrim(t);            // trailing blanks inside an open string are its bytes
        out.append((size_t)lead * 4, ' ');
        out += t; out += eol;
    }
    return out;
}

// `eskiuc fmt [--check] file.esk …` — reformat each file in place. With --check,
// don't write; exit non-zero if any file is not already formatted. Returns the
// process exit code.
int runFmt(const std::vector<std::string>& files, bool check) {
    if (files.empty()) { std::cerr << "error: fmt: no input files\n"; return 1; }
    int changed = 0, failed = 0;
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in.is_open()) { std::cerr << "error: fmt: cannot open '" << f << "'\n"; failed++; continue; }
        std::stringstream buf; buf << in.rdbuf(); in.close();
        std::string original = buf.str();
        std::string formatted = formatSource(original);
        if (formatted == original) continue;
        changed++;
        if (check) { std::cout << f << "\n"; continue; }
        std::ofstream outF(f, std::ios::trunc);
        if (!outF.is_open()) { std::cerr << "error: fmt: cannot write '" << f << "'\n"; failed++; continue; }
        outF << formatted; outF.close();
    }
    if (failed) return 1;
    if (check && changed) return 1;     // CI signal: files need formatting
    return 0;
}

void seedPredefinedMacros(std::map<std::string, Macro>& macros, const std::string& triple,
                          bool freestanding) {
    // Predefine a platform macro so stdlib can #ifdef per OS (the event-loop
    // backend and sockaddr_in layout differ between macOS and Linux). It follows
    // the --target triple when cross-compiling, else the build host — otherwise a
    // `--target x86_64-linux-gnu` build on macOS would still select the kqueue
    // path and emit unresolved BSD symbols.
    Macro os; os.body = "1";
    const std::string& tt = triple;
    bool tgtLinux = tt.find("linux") != std::string::npos;
    bool tgtApple = tt.find("apple") != std::string::npos ||
                    tt.find("darwin") != std::string::npos ||
                    tt.find("macos") != std::string::npos;
    bool tgtWindows = tt.find("windows") != std::string::npos ||
                      tt.find("win32") != std::string::npos ||
                      tt.find("mingw") != std::string::npos;
    // _WIN64 accompanies _WIN32 on 64-bit Windows (MSVC keeps _WIN32 defined
    // for both widths and adds _WIN64 only when 64-bit).
    bool tgt64 = tt.find("x86_64") != std::string::npos ||
                 tt.find("amd64") != std::string::npos ||
                 tt.find("aarch64") != std::string::npos ||
                 tt.find("arm64") != std::string::npos;
    if (tgtLinux)        { macros["__linux__"] = os; }
    else if (tgtApple)   { macros["__APPLE__"] = os; }
    else if (tgtWindows) {
        macros["_WIN32"] = os;
        if (tgt64) macros["_WIN64"] = os;
    }
    else if (tt.empty()) {
        // Native build: follow the build host.
#if defined(_WIN32)
        macros["_WIN32"] = os;
#if defined(_WIN64)
        macros["_WIN64"] = os;
#endif
#elif defined(__APPLE__)
        macros["__APPLE__"] = os;
#elif defined(__linux__)
        macros["__linux__"] = os;
#endif
    }
    // else: an explicit bare-metal or otherwise non-hosted triple (e.g. the
    // 3DS's armv6k-none-eabihf) defines no OS macro. Bare metal has no host OS,
    // so portable code guards that path explicitly rather than falling through
    // to the build host's.

    // The architecture macro C compilers predefine (__aarch64__ / __x86_64__ /
    // __arm__), from the --target triple or else the build host. The self-hosted
    // compiler reads it at its own build to pick its C-ABI lowering.
    std::string arch;
    if (tt.rfind("aarch64", 0) == 0 || tt.rfind("arm64", 0) == 0) arch = "__aarch64__";
    else if (tt.rfind("x86_64", 0) == 0 || tt.rfind("amd64", 0) == 0) arch = "__x86_64__";
    else if (tt.rfind("arm", 0) == 0 || tt.rfind("thumb", 0) == 0) arch = "__arm__";
    else if (tt.empty()) {
#if defined(__aarch64__) || defined(_M_ARM64)
        arch = "__aarch64__";
#elif defined(__x86_64__) || defined(_M_X64)
        arch = "__x86_64__";
#elif defined(__arm__)
        arch = "__arm__";
#endif
    }
    if (!arch.empty()) macros[arch] = os;

    // Predefine __ESKIU_FREESTANDING__ under --freestanding so stdlib (e.g.
    // <mem>'s alloc/free) can target esk_alloc/esk_free instead of libc.
    if (freestanding) {
        Macro fs; fs.body = "1";
        macros["__ESKIU_FREESTANDING__"] = fs;
    }
}

// Load → lex → parse every input file and merge their declarations into one Program,
// or nullptr on a lexical or parse error (a diagnostic is printed by the lexer or
// parser; `lexFailed` tells which). Shared by the build and every pipeline mode
// (parse/typecheck/codegen, --hover-at, --definition-at), so they all preprocess alike
// (same predefined macros, one macro table across files and imports, each import
// preprocessed at its import line, __FILE__ = the path) and see the same program.
// `macrosOut` receives the final macro table.
std::shared_ptr<Program> loadProgram(const std::vector<std::string>& inputs, const std::string& triple,
                                     bool freestanding, std::map<std::string, Macro>* macrosOut,
                                     bool* lexFailed) {
    std::map<std::string, Macro> macros;     // shared: #defines propagate across files
    seedPredefinedMacros(macros, triple, freestanding);
    std::set<std::string> importedFiles;     // shared: a common import is parsed once
    ImportCache cache;                       // imports preprocessed at their import line
    cache.stdlibPath = stdlibRoot;
    cache.macros = &macros;
    std::vector<DeclPtr> merged;
    std::vector<std::string> linkLibs;       // `#pragma link` libraries of every input
    if (lexFailed) *lexFailed = false;
    for (const auto& fname : inputs) {
        // Register the root file itself, so an import cycle back to it (or an
        // input that an earlier input already imported) is not parsed twice.
        std::string canon = Parser::canonicalPath(fname);
        if (!importedFiles.insert(canon).second) continue;
        cache.seen.insert(canon);
        std::string source = readFile(fname);
        PPImportHook hook = cache.hookFor(dirOf(fname));
        Lexer lexer(source, &macros, fname, &hook);
        std::vector<Token> tokens;
        Token tok = lexer.next_token();
        while (tok.type != TokenType::EOF_TOKEN) {
            tokens.push_back(tok);
            tok = lexer.next_token();
        }
        tokens.push_back(tok);
        if (lexer.hadError) { if (lexFailed) *lexFailed = true; return nullptr; }

        Parser parser(tokens);
        parser.filename = fname;
        parser.stdlibPath = stdlibRoot;
        parser.basedir = dirOf(fname);
        parser.macros = &macros;
        parser.importedFiles = &importedFiles;
        parser.importCache = &cache;
        auto prog = parser.parse();
        if (!prog) return nullptr;
        merged.insert(merged.end(), prog->declarations.begin(), prog->declarations.end());
        linkLibs.insert(linkLibs.end(), prog->linkLibs.begin(), prog->linkLibs.end());
    }
    if (macrosOut) *macrosOut = macros;
    auto program = std::make_shared<Program>(merged);
    program->linkLibs = linkLibs;
    return program;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Locate a C linker driver: $CC first, then cc / clang / gcc on PATH.
// When `sanitized` is set, prefer the clang from the LLVM toolchain this
// compiler was built against — its compiler-rt matches the ASan instrumentation
// we emit, so the runtime versions agree (Apple's system clang ships a different
// ASan ABI and would fail to link).
std::string findCDriver(bool sanitized, std::vector<std::string>* ccArgs) {
#ifdef ESKIU_LLVM_BINDIR
    if (sanitized) {
        std::string llvmClang = std::string(ESKIU_LLVM_BINDIR) + "/clang";
        if (llvm::sys::fs::exists(llvmClang)) return llvmClang;
    }
#endif
    if (const char* cc = std::getenv("CC"); cc && *cc) {
        // $CC may carry arguments ("clang -m64"): the first word is the program.
        std::vector<std::string> words;
        std::istringstream ws(cc);
        for (std::string w; ws >> w;) words.push_back(w);
        if (!words.empty()) {
            if (auto p = llvm::sys::findProgramByName(words[0])) {
                if (ccArgs) ccArgs->assign(words.begin() + 1, words.end());
                return *p;
            }
            std::cerr << "warning: $CC ('" << cc << "') was not found; "
                         "falling back to cc/clang/gcc" << std::endl;
        }
    }
    for (const char* name : {"cc", "clang", "gcc"}) {
        if (auto p = llvm::sys::findProgramByName(name)) return *p;
    }
    return "";
}

// Link an object file into an executable by invoking the system C driver
// (the same thing rustc/clang do internally). Returns true on success.
bool linkExecutable(const std::string& obj, const std::string& out,
                           const std::vector<std::string>& libs,
                           const std::vector<std::string>& paths,
                           const std::vector<std::string>& extra,
                           bool sanitized) {
    std::vector<std::string> ccArgs;
    std::string driver = findCDriver(sanitized, &ccArgs);
    if (driver.empty()) {
        std::cerr << "error: no C linker driver found (looked for $CC, cc, clang, gcc).\n"
                     "       Install a C toolchain, or use -c to emit an object file "
                     "and link it yourself.\n";
        return false;
    }
    std::vector<std::string> argv = {driver};
    argv.insert(argv.end(), ccArgs.begin(), ccArgs.end());
    argv.insert(argv.end(), {obj, "-o", out});
    for (const auto& p : paths) argv.push_back("-L" + p);
    for (const auto& l : libs)  argv.push_back("-l" + l);
    for (const auto& a : extra) argv.push_back(a);

    std::vector<llvm::StringRef> args(argv.begin(), argv.end());
    std::string errMsg;
    int rc = llvm::sys::ExecuteAndWait(driver, args, /*Env=*/std::nullopt,
                                       /*Redirects=*/{}, /*SecondsToWait=*/0,
                                       /*MemoryLimit=*/0, &errMsg);
    if (rc != 0) {
        std::cerr << "error: linking failed";
        if (!errMsg.empty()) std::cerr << ": " << errMsg;
        else                 std::cerr << " (" << driver << " exited with code " << rc << ")";
        std::cerr << std::endl;
        return false;
    }
    return true;
}

std::vector<std::string> implicitLinkLibs(const std::map<std::string, Macro>& macros,
                                          bool usesExceptions, bool usesThreads) {
    std::vector<std::string> libs;
    bool apple = macros.count("__APPLE__") > 0;
    bool gnu = macros.count("__linux__") > 0 || macros.count("_WIN32") > 0;
    if (usesExceptions) {
        if (apple) libs.push_back("c++");
        else if (gnu) libs.push_back("stdc++");
    }
    if (usesThreads && gnu) libs.push_back("pthread");
    return libs;
}

// Run an executable, forwarding `progArgs`, and return its exit code: the program's
// own status, 128+N when it was killed by signal N (the shell convention), or 1 if it
// could not be launched. Used by `eskiuc run`.
int runExecutable(const std::string& exe, const std::vector<std::string>& progArgs) {
#ifndef _WIN32
    std::vector<char*> cargv;
    cargv.push_back(const_cast<char*>(exe.c_str()));
    for (const auto& a : progArgs) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "error: could not run '" << exe << "': fork failed" << std::endl;
        return 1;
    }
    if (pid == 0) {
        execv(exe.c_str(), cargv.data());
        std::cerr << "error: could not run '" << exe << "'" << std::endl;
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { std::cerr << "error: waiting for '" << exe << "' failed" << std::endl; return 1; }
    }
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
#else
    std::vector<std::string> argv = {exe};
    for (const auto& a : progArgs) argv.push_back(a);
    std::vector<llvm::StringRef> args(argv.begin(), argv.end());
    std::string errMsg;
    int rc = llvm::sys::ExecuteAndWait(exe, args, /*Env=*/std::nullopt,
                                       /*Redirects=*/{}, /*SecondsToWait=*/0,
                                       /*MemoryLimit=*/0, &errMsg);
    if (rc < 0) {
        std::cerr << "error: could not run '" << exe << "'";
        if (!errMsg.empty()) std::cerr << ": " << errMsg;
        std::cerr << std::endl;
        return 1;
    }
    return rc;
#endif
}
