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
#endif
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/FileSystem.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"
#include "sema/type_checker.h"
#include "sema/async_transform.h"
#include "codegen/codegen.h"

// Command line options
static llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional,
                                                 llvm::cl::desc("<input .esk file>"));

// Additional .esk files: `eskiuc a.esk b.esk -o prog` compiles them together.
static llvm::cl::list<std::string> ExtraInputs(llvm::cl::Positional,
                                               llvm::cl::desc("[additional .esk files]"));

static llvm::cl::opt<std::string> OutputFilename("o",
                                                  llvm::cl::desc("Output filename"),
                                                  llvm::cl::value_desc("filename"));

static llvm::cl::opt<bool> TestLexer("test-lexer",
                                     llvm::cl::desc("Tokenize input and print token stream"));

static llvm::cl::opt<bool> TestParser("test-parser",
                                      llvm::cl::desc("Parse input and print AST"));

static llvm::cl::opt<bool> TestCodegen("test-codegen",
                                       llvm::cl::desc("Generate LLVM IR and print it"));

static llvm::cl::opt<bool> TestTypeChecker("test-typechecker",
                                           llvm::cl::desc("Type check input and report errors"));

static llvm::cl::opt<std::string> TargetTriple("target",
    llvm::cl::desc("Override target triple (e.g. x86_64-pc-none, aarch64-unknown-none)"),
    llvm::cl::value_desc("triple"));

static llvm::cl::opt<bool> Freestanding("freestanding",
    llvm::cl::desc("Compile without libc — alloc/free use esk_alloc/esk_free"));

static llvm::cl::opt<bool> Wall("Wall",
    llvm::cl::desc("Enable lint-style warnings: unused variables, parameters, "
                   "and functions, and assignment used as a condition"));

static llvm::cl::opt<bool> Wextra("Wextra",
    llvm::cl::desc("Extra warnings: signed/unsigned comparison mismatches"));

static llvm::cl::opt<std::string> HoverAt("hover-at",
    llvm::cl::desc("Print the Eskiu type at LINE:COL (e.g. --hover-at 8:12)"),
    llvm::cl::value_desc("LINE:COL"));

static llvm::cl::opt<std::string> DefinitionAt("definition-at",
    llvm::cl::desc("Print the definition location of the symbol at LINE:COL"),
    llvm::cl::value_desc("LINE:COL"));

static llvm::cl::opt<bool> CompileOnly("c",
    llvm::cl::desc("Compile to an object file only; do not link"));

static llvm::cl::list<std::string> LinkLibs("l", llvm::cl::Prefix,
    llvm::cl::desc("Link against a library, e.g. -lpthread (passed to the linker)"));

static llvm::cl::list<std::string> LinkPaths("L", llvm::cl::Prefix,
    llvm::cl::desc("Add a library search path (passed to the linker)"));

static llvm::cl::list<std::string> LinkArgs("link-arg",
    llvm::cl::desc("Pass an extra argument to the linker (repeatable)"),
    llvm::cl::value_desc("arg"));

// Sanitizers: instrument the module (real LLVM passes) and link the runtime.
static llvm::cl::opt<bool> Asan("asan",
    llvm::cl::desc("Instrument with AddressSanitizer (detects memory errors)"));
static llvm::cl::opt<bool> Ubsan("ubsan",
    llvm::cl::desc("Instrument with bounds checking (traps on out-of-bounds access)"));

const char* VERSION = "0.2.0";
static std::string stdlibRoot; // set once at startup via resolveStdlibPath()

// `eskiuc run`: set when argv[1] == "run". The program is compiled to a
// temporary executable, run with g_runArgs, then deleted (see main()).
static bool g_runMode = false;
static std::vector<std::string> g_runArgs;

// Resolve stdlib root: $ESKIU_ROOT env var, or dirname(argv[0])/../lib/eskiu
static std::string resolveStdlibPath() {
    const char* env = std::getenv("ESKIU_ROOT");
    if (env && *env) return std::string(env);

    // Deduce from binary location
    char buf[4096] = {};
#ifdef __APPLE__
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
#elif defined(__linux__)
    if (readlink("/proc/self/exe", buf, sizeof(buf) - 1) > 0) {
#else
    if (false) {
#endif
        std::string binPath(buf);
        size_t slash = binPath.rfind('/');
        if (slash != std::string::npos) {
            // binary is at <prefix>/bin/eskiuc → look for <prefix>/lib/eskiu
            std::string binDir = binPath.substr(0, slash);
            size_t parentSlash = binDir.rfind('/');
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
static std::string readFile(const std::string& filename) {
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
static std::string dirOf(const std::string& path) {
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
static std::string formatSource(const std::string& src) {
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
        for (size_t i = 0; i < t.size(); ++i) {
            char c = t[i];
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') break;       // line comment
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {            // block comment
                inBlock = true;
                for (size_t j = i + 2; j + 1 < t.size(); ++j)
                    if (t[j] == '*' && t[j + 1] == '/') { inBlock = false; i = j + 1; break; }
                if (inBlock) break;                                          // runs onto next line
                continue;
            }
            if (c == '"' || c == '\'') {                                     // string / char literal
                char q = c; ++i;
                while (i < t.size() && t[i] != q) { if (t[i] == '\\') ++i; ++i; }
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
static int runFmt(const std::vector<std::string>& files, bool check) {
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
static std::shared_ptr<Program> loadProgram(const std::string& filename) {
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

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Locate a C linker driver: $CC first, then cc / clang / gcc on PATH.
// When `sanitized` is set, prefer the clang from the LLVM toolchain this
// compiler was built against — its compiler-rt matches the ASan instrumentation
// we emit, so the runtime versions agree (Apple's system clang ships a different
// ASan ABI and would fail to link).
static std::string findCDriver(bool sanitized = false) {
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
static bool linkExecutable(const std::string& obj, const std::string& out,
                           const std::vector<std::string>& libs,
                           const std::vector<std::string>& paths,
                           const std::vector<std::string>& extra,
                           bool sanitized = false) {
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
static int runExecutable(const std::string& exe, const std::vector<std::string>& progArgs) {
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

// Test lexer: tokenize and print all tokens
static void testLexer(const std::string& filename) {
    std::string source = readFile(filename);
    Lexer lexer(source);

    std::cout << "Tokenizing: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    Token tok = lexer.next_token();
    int tokenCount = 0;

    while (tok.type != TokenType::EOF_TOKEN) {
        std::string typeStr = tokenTypeToString(tok.type);
        std::cout << "  Line " << std::string(3 - std::to_string(tok.line).length(), ' ') << tok.line
                  << ", Col " << std::string(3 - std::to_string(tok.column).length(), ' ') << tok.column
                  << "  " << std::string(15 - typeStr.length(), ' ') << typeStr
                  << "  '" << tok.value << "'" << std::endl;
        tok = lexer.next_token();
        tokenCount++;
    }

    std::cout << "========================================================" << std::endl;
    std::cout << "Total tokens: " << tokenCount << std::endl;
}

// Test type checker: tokenize, parse, type check, and report errors
static int testTypeChecker(const std::string& filename) {
    auto program = loadProgram(filename);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return 1;
    }

    std::cout << "Type checking: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Type check
        TypeChecker typeChecker;
        typeChecker.sourceFile = filename;
        typeChecker.warnAll = Wall;
        typeChecker.warnExtra = Wextra;
        bool success = typeChecker.check(program.get());

        std::cout << "========================================================" << std::endl;
        if (success) {
            std::cout << "Type checking succeeded!" << std::endl;
            return 0;
        } else {
            std::cout << "Type checking failed!" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

// Test codegen: tokenize, parse, generate LLVM IR, and print it
static void testCodegen(const std::string& filename) {
    auto program = loadProgram(filename);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return;
    }

    std::cout << "Generating LLVM IR: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        AsyncTransform().run(program.get());
        // Codegen
        CodeGen codegen;
        if (!TargetTriple.empty()) codegen.targetTriple = std::string(TargetTriple);
        codegen.freestanding = Freestanding;
        llvm::Module* module = codegen.generateCode(program);

        if (!module) {
            std::cerr << "Code generation failed!" << std::endl;
            return;
        }

        llvm::raw_os_ostream out(std::cout);
        module->print(out, nullptr);
        out.flush();

        std::cout << "========================================================" << std::endl;
        std::cout << "Code generation succeeded!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}

// Test parser: tokenize, parse, and print AST
static void testParser(const std::string& filename) {
    auto program = loadProgram(filename);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return;
    }

    std::cout << "Parsing: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    ASTPrinter printer;
    printer.print(program);

    std::cout << "========================================================" << std::endl;
    std::cout << "Parse succeeded!" << std::endl;
}

int main(int argc, char** argv) {
    llvm::InitLLVM X(argc, argv);

    // Set version string for LLVM's built-in --version
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream& os) {
        os << "Eskiu " << VERSION << " (LLVM " << LLVM_VERSION_MAJOR << "."
           << LLVM_VERSION_MINOR << "." << LLVM_VERSION_PATCH << ")\n";
    });

    // `eskiuc fmt [--check] file.esk …` — reformat files in place. Handled before
    // option parsing; it does not use the compiler pipeline.
    if (argc >= 2 && std::string(argv[1]) == "fmt") {
        bool check = false;
        std::vector<std::string> files;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--check") check = true;
            else files.push_back(a);
        }
        return runFmt(files, check);
    }

    // `eskiuc run script.esk [args...]` — compile to a temporary executable, run
    // it forwarding [args...], then delete it. Enables shebang scripts
    // (`#!/usr/bin/env eskiuc run`). Rewritten here before option parsing: any
    // leading flags and the first non-flag token (the script) go to the option
    // parser; everything after the script becomes the program's argv.
    if (argc >= 2 && std::string(argv[1]) == "run") {
        g_runMode = true;
        std::vector<char*> clArgv = { argv[0] };
        bool gotScript = false;
        for (int i = 2; i < argc; ++i) {
            if (!gotScript) {
                clArgv.push_back(argv[i]);
                if (argv[i][0] != '-') gotScript = true;   // first non-flag = the script
            } else {
                g_runArgs.push_back(argv[i]);
            }
        }
        int newArgc = (int)clArgv.size();
        llvm::cl::ParseCommandLineOptions(newArgc, clArgv.data(), "Eskiu Language Compiler\n");
    } else {
        llvm::cl::ParseCommandLineOptions(argc, argv, "Eskiu Language Compiler\n");
    }

    // Resolve stdlib root once — used by all parsers for import <name>
    stdlibRoot = resolveStdlibPath();

    // Check input file provided
    if (InputFilename.empty()) {
        std::cerr << "error: no input file specified" << std::endl;
        return 1;
    }

    // Handle --test-lexer
    if (TestLexer) {
        testLexer(InputFilename);
        return 0;
    }

    // Handle --test-parser
    if (TestParser) {
        testParser(InputFilename);
        return 0;
    }

    // Handle --test-typechecker
    if (TestTypeChecker) {
        return testTypeChecker(InputFilename);
    }

    // Handle --hover-at LINE:COL
    if (!HoverAt.empty()) {
        int line = 0, col = 0;
        if (sscanf(HoverAt.c_str(), "%d:%d", &line, &col) != 2) {
            std::cerr << "error: --hover-at expects LINE:COL format\n"; return 1;
        }
        auto program = loadProgram(std::string(InputFilename));
        if (!program) { std::cout << "(parse error)\n"; return 0; }
        try {
            TypeChecker tc;
            tc.sourceFile = std::string(InputFilename);
            tc.check(program.get());
            std::string type = tc.getTypeAtPosition(line, col);
            if (type.empty()) std::cout << "(no type at " << line << ":" << col << ")\n";
            else              std::cout << type << "\n";
        } catch (...) { std::cout << "(error)\n"; }
        return 0;
    }

    // Handle --definition-at LINE:COL
    if (!DefinitionAt.empty()) {
        int line = 0, col = 0;
        if (sscanf(DefinitionAt.c_str(), "%d:%d", &line, &col) != 2) {
            std::cerr << "error: --definition-at expects LINE:COL format\n"; return 1;
        }
        auto program = loadProgram(std::string(InputFilename));
        if (!program) { std::cout << "(parse error)\n"; return 0; }
        try {
            TypeChecker tc;
            tc.sourceFile = std::string(InputFilename);
            tc.check(program.get());
            std::string loc = tc.getDefinitionAt(line, col);
            if (loc.empty()) std::cout << "(no definition at " << line << ":" << col << ")\n";
            else             std::cout << loc << "\n";
        } catch (...) { std::cout << "(error)\n"; }
        return 0;
    }

    // Handle --test-codegen
    if (TestCodegen) {
        testCodegen(InputFilename);
        return 0;
    }

    // Full compilation pipeline — parse every input file and merge their
    // top-level declarations into a single program (`eskiuc a.esk b.esk ...`).
    std::vector<std::string> inputs = { std::string(InputFilename) };
    for (const auto& f : ExtraInputs) inputs.push_back(f);

    try {
        std::vector<DeclPtr> mergedDecls;
        std::set<std::string> importedFiles;     // shared: a common import is parsed once
        std::map<std::string, Macro> macros;     // shared: #defines propagate across files

        // Predefine a host-OS macro so stdlib can #ifdef per platform (e.g. the
        // sockaddr_in layout differs between macOS and Linux).
        {
            Macro os; os.body = "1";
#if defined(__APPLE__)
            macros["__APPLE__"] = os;
#elif defined(__linux__)
            macros["__linux__"] = os;
#endif
        }
        // Predefine __ESKIU_FREESTANDING__ under --freestanding so stdlib (e.g.
        // <mem>'s alloc/free) can target esk_alloc/esk_free instead of libc.
        if (Freestanding) {
            Macro fs; fs.body = "1";
            macros["__ESKIU_FREESTANDING__"] = fs;
        }

        for (const auto& fname : inputs) {
            std::string source = readFile(fname);
            Lexer lexer(source, &macros, fname);
            std::vector<Token> tokens;
            Token tok = lexer.next_token();
            while (tok.type != TokenType::EOF_TOKEN) {
                tokens.push_back(tok);
                tok = lexer.next_token();
            }
            tokens.push_back(tok);
            if (lexer.hadError) return 1;

            Parser parser(tokens);
            parser.stdlibPath = stdlibRoot;
            parser.basedir = dirOf(fname);
            parser.importedFiles = &importedFiles;
            parser.macros = &macros;
            auto prog = parser.parse();
            if (!prog) {
                std::cerr << "error: parse failed" << std::endl;
                return 1;
            }
            mergedDecls.insert(mergedDecls.end(),
                               prog->declarations.begin(), prog->declarations.end());
        }
        auto program = std::make_shared<Program>(mergedDecls);

        TypeChecker typeChecker;
        typeChecker.sourceFile = std::string(InputFilename);
        typeChecker.warnAll = Wall;
        typeChecker.warnExtra = Wextra;
        if (!typeChecker.check(program.get())) {
            return 1;
        }

        AsyncTransform().run(program.get());
        CodeGen codegen;
        if (!TargetTriple.empty()) codegen.targetTriple = std::string(TargetTriple);
        codegen.freestanding = Freestanding;
        codegen.asan = Asan;
        codegen.ubsan = Ubsan;
        if (!codegen.generateCode(program)) {
            std::cerr << "error: code generation failed" << std::endl;
            return 1;
        }

        // `eskiuc run`: link into a temporary executable, then run it.
        std::string runExePath;
        if (g_runMode) {
            llvm::SmallString<128> tmpExe;
            if (llvm::sys::fs::createTemporaryFile("eskiu-run", "", tmpExe)) {
                std::cerr << "error: could not create a temporary executable" << std::endl;
                return 1;
            }
            runExePath = std::string(tmpExe.str());
        }

        std::string outFile = !runExePath.empty() ? runExePath
            : OutputFilename.empty() ? std::string(InputFilename) + ".o"
            : std::string(OutputFilename);

        // Link into an executable when the output is not an object file.
        // Object-only when: -c is given, the output ends in .o, no -o was given,
        // or --freestanding (bare-metal needs a custom linker script — link yourself).
        bool linkExe = g_runMode || (!CompileOnly && !Freestanding &&
                       !OutputFilename.empty() && !endsWith(outFile, ".o"));

        if (linkExe) {
            llvm::SmallString<128> tmpObj;
            if (llvm::sys::fs::createTemporaryFile("eskiu", "o", tmpObj)) {
                std::cerr << "error: could not create a temporary object file" << std::endl;
                return 1;
            }
            std::string tmpObjPath(tmpObj.str());
            if (!codegen.emitObjectFile(tmpObjPath)) {
                llvm::sys::fs::remove(tmpObjPath);
                return 1;
            }
            std::vector<std::string> libs(LinkLibs.begin(), LinkLibs.end());
            std::vector<std::string> paths(LinkPaths.begin(), LinkPaths.end());
            std::vector<std::string> extra(LinkArgs.begin(), LinkArgs.end());
            // ASan needs its runtime linked; --ubsan traps directly (no runtime).
            if (Asan) extra.push_back("-fsanitize=address");
            bool ok = linkExecutable(tmpObjPath, outFile, libs, paths, extra, /*sanitized=*/Asan);
            llvm::sys::fs::remove(tmpObjPath);
            if (!ok) { if (g_runMode) llvm::sys::fs::remove(runExePath); return 1; }

            if (g_runMode) {
                int rc = runExecutable(runExePath, g_runArgs);
                llvm::sys::fs::remove(runExePath);
                return rc;
            }
            std::cout << outFile << std::endl;
            return 0;
        }

        if (!codegen.emitObjectFile(outFile)) {
            return 1;
        }

        std::cout << outFile << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}
