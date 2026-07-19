#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <climits>
#include <set>
#ifdef __APPLE__
  #include <mach-o/dyld.h>
#elif defined(__linux__)
  #include <unistd.h>
#elif defined(_WIN32)
  #include <windows.h>
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
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) > 0) {
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
//   * trailing whitespace stripped
//   * runs of blank lines collapsed to one; leading/trailing blank lines removed
//   * exactly one final newline
// Each line's *content* (operators, inner spacing, comments) is preserved
// verbatim. Braces inside strings, char literals and comments are ignored, so
// formatting is idempotent and safe. Preprocessor lines (`#…`) sit at column 0
// and do not affect nesting.
std::string formatSource(const std::string& src) {
    std::vector<std::string> lines;
    { std::string cur; for (char c : src) { if (c == '\n') { lines.push_back(cur); cur.clear(); }
                                            else if (c != '\r') cur += c; }
      lines.push_back(cur); }

    auto trim = [](const std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    };

    std::string out;
    int depth = 0;            // current `{` nesting
    bool inBlock = false;     // inside a /* … */ block comment
    int pendingBlank = 0;     // blank lines buffered (for collapsing)
    bool wroteAny = false;

    for (const std::string& raw : lines) {
        if (inBlock) {                       // verbatim until the comment closes
            out += raw; out += "\n";
            for (size_t i = 0; i + 1 < raw.size(); ++i)
                if (raw[i] == '*' && raw[i + 1] == '/') { inBlock = false; break; }
            wroteAny = true;
            continue;
        }
        std::string t = trim(raw);
        if (t.empty()) { pendingBlank++; continue; }

        if (wroteAny && pendingBlank > 0) out += "\n";   // collapse to one blank
        pendingBlank = 0;

        if (t[0] == '#') {                   // preprocessor line: column 0, no nesting change
            out += t; out += "\n"; wroteAny = true; continue;
        }

        // This line's indent dedents for each leading `}`.
        int lead = depth;
        for (char c : t) { if (c == '}') lead--; else break; }
        if (lead < 0) lead = 0;
        out.append((size_t)lead * 4, ' ');
        out += t; out += "\n";
        wroteAny = true;

        // Update nesting from this line's code, skipping strings/chars/comments.
        // Strings are checked first, so a `/*` or `}` inside a literal is ignored.
        for (size_t i = 0; i < t.size(); ++i) {
            char c = t[i];
            if (c == '"' || c == '\'') {                                     // string / char literal
                char q = c; ++i;
                while (i < t.size() && t[i] != q) {
                    if (t[i] == '\\' && i + 1 < t.size()) { ++i; }           // skip the escaped char
                    ++i;
                }
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

// Load → lex → parse a single source file. Returns the parsed Program, or
// nullptr on a lexical or parse error (a diagnostic is printed by the lexer or
// parser). Shared by every single-file pipeline mode (parse/typecheck/codegen,
// --hover-at, --definition-at).
std::shared_ptr<Program> loadProgram(const std::string& filename) {
    std::string source = readFile(filename);
    Lexer lexer(source);
    std::vector<Token> tokens;
    Token tok = lexer.next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
        tok = lexer.next_token();
    }
    tokens.push_back(tok);
    if (lexer.hadError) return nullptr;

    Parser parser(tokens);
    parser.stdlibPath = stdlibRoot;
    parser.basedir = dirOf(filename);
    return parser.parse();
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
std::string findCDriver(bool sanitized) {
#ifdef ESKIU_LLVM_BINDIR
    if (sanitized) {
        std::string llvmClang = std::string(ESKIU_LLVM_BINDIR) + "/clang";
        if (llvm::sys::fs::exists(llvmClang)) return llvmClang;
    }
#endif
    if (const char* cc = std::getenv("CC")) {
        if (auto p = llvm::sys::findProgramByName(cc)) return *p;
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
    std::string driver = findCDriver(sanitized);
    if (driver.empty()) {
        std::cerr << "error: no C linker driver found (looked for $CC, cc, clang, gcc).\n"
                     "       Install a C toolchain, or use -c to emit an object file "
                     "and link it yourself.\n";
        return false;
    }
    std::vector<std::string> argv = {driver, obj, "-o", out};
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

// Run an executable, forwarding `progArgs`, and return its exit code (or 1 if it
// could not be launched). Used by `eskiuc run`.
int runExecutable(const std::string& exe, const std::vector<std::string>& progArgs) {
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
}
