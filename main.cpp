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

const char* VERSION = "0.2.0-dev";
static std::string stdlibRoot; // set once at startup via resolveStdlibPath()

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
static std::string findCDriver() {
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
                           const std::vector<std::string>& extra) {
    std::string driver = findCDriver();
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

    llvm::cl::ParseCommandLineOptions(argc, argv, "Eskiu Language Compiler\n");

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
            Lexer lexer(source, &macros);
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
        if (!typeChecker.check(program.get())) {
            return 1;
        }

        AsyncTransform().run(program.get());
        CodeGen codegen;
        if (!TargetTriple.empty()) codegen.targetTriple = std::string(TargetTriple);
        codegen.freestanding = Freestanding;
        if (!codegen.generateCode(program)) {
            std::cerr << "error: code generation failed" << std::endl;
            return 1;
        }

        std::string outFile = OutputFilename.empty()
            ? std::string(InputFilename) + ".o"
            : std::string(OutputFilename);

        // Link into an executable when the output is not an object file.
        // Object-only when: -c is given, the output ends in .o, no -o was given,
        // or --freestanding (bare-metal needs a custom linker script — link yourself).
        bool linkExe = !CompileOnly && !Freestanding &&
                       !OutputFilename.empty() && !endsWith(outFile, ".o");

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
            bool ok = linkExecutable(tmpObjPath, outFile, libs, paths, extra);
            llvm::sys::fs::remove(tmpObjPath);
            if (!ok) return 1;
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
