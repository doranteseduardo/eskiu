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
#ifdef _WIN32
  #include "llvm/Support/thread.h"
#else
  #include <pthread.h>
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

// Command line options. All of them live in one category so `--help` lists only
// Eskiu's options (not the ~200 LLVM internals linked in with the backend).
static llvm::cl::OptionCategory EskiuCat("Eskiu options");

static const char* OVERVIEW =
    "Eskiu Language Compiler\n\n"
    "  eskiuc file.esk [more.esk ...] -o prog   compile and link an executable\n"
    "  eskiuc file.esk -c -o file.o             compile to an object file\n"
    "  eskiuc run [flags] file.esk [--] [args]  compile to a temp executable and run it\n"
    "  eskiuc fmt [--check] file.esk ...        reindent files in place\n";
static llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional,
                                                 llvm::cl::desc("<input .esk file>"),
    llvm::cl::cat(EskiuCat));

// Additional .esk files: `eskiuc a.esk b.esk -o prog` compiles them together.
static llvm::cl::list<std::string> ExtraInputs(llvm::cl::Positional,
                                               llvm::cl::desc("[additional .esk files]"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> OutputFilename("o",
                                                  llvm::cl::desc("Output filename"),
                                                  llvm::cl::value_desc("filename"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> TestLexer("test-lexer",
                                     llvm::cl::desc("Tokenize input and print token stream"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> TestParser("test-parser",
                                      llvm::cl::desc("Parse input and print AST"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> TestCodegen("test-codegen",
                                       llvm::cl::desc("Generate LLVM IR and print it"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> TestTypeChecker("test-typechecker",
                                           llvm::cl::desc("Type check input and report errors"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> TargetTriple("target",
    llvm::cl::desc("Override target triple (e.g. x86_64-pc-none, aarch64-unknown-none)"),
    llvm::cl::value_desc("triple"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> TargetCPU("mcpu",
    llvm::cl::desc("Override target CPU (e.g. mpcore for the 3DS ARM11)"),
    llvm::cl::value_desc("cpu"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> TargetFeatures("mattr",
    llvm::cl::desc("Target feature string, LLVM -mattr syntax (e.g. +vfp2)"),
    llvm::cl::value_desc("features"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> RelocModel("reloc",
    llvm::cl::desc("Relocation model: pic (default), static, dynamic-no-pic. "
                   "3DS .3dsx targets need 'static'."),
    llvm::cl::value_desc("model"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> Freestanding("freestanding",
    llvm::cl::desc("Compile without libc — alloc/free use esk_alloc/esk_free"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> Safe("safe",
    llvm::cl::desc("Insert runtime safety checks (slice bounds); traps on violation"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> Wall("Wall",
    llvm::cl::desc("Enable lint-style warnings: unused variables, parameters, "
                   "and functions, and assignment used as a condition"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> Wextra("Wextra",
    llvm::cl::desc("Extra warnings: signed/unsigned comparison mismatches"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> HoverAt("hover-at",
    llvm::cl::desc("Print the Eskiu type at LINE:COL (e.g. --hover-at 8:12)"),
    llvm::cl::value_desc("LINE:COL"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<std::string> DefinitionAt("definition-at",
    llvm::cl::desc("Print the definition location of the symbol at LINE:COL"),
    llvm::cl::value_desc("LINE:COL"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> CompileOnly("c",
    llvm::cl::desc("Compile to an object file only; do not link"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::list<std::string> LinkLibs("l", llvm::cl::Prefix,
    llvm::cl::desc("Link against a library, e.g. -lpthread (passed to the linker)"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::opt<bool> NoDefaultLibs("no-default-libs",
    llvm::cl::desc("Do not add the libraries the program implies (#pragma link, the C++ "
                   "exception runtime, pthread); link only what -l names"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::list<std::string> LinkPaths("L", llvm::cl::Prefix,
    llvm::cl::desc("Add a library search path (passed to the linker)"),
    llvm::cl::cat(EskiuCat));

static llvm::cl::list<std::string> LinkArgs("link-arg",
    llvm::cl::desc("Pass an extra argument to the linker (repeatable)"),
    llvm::cl::value_desc("arg"),
    llvm::cl::cat(EskiuCat));

// Sanitizers: instrument the module (real LLVM passes) and link the runtime.
static llvm::cl::opt<bool> Asan("asan",
    llvm::cl::desc("Instrument with AddressSanitizer (detects memory errors)"),
    llvm::cl::cat(EskiuCat));
static llvm::cl::opt<bool> Ubsan("ubsan",
    llvm::cl::desc("Instrument with bounds checking (traps on out-of-bounds access)"),
    llvm::cl::cat(EskiuCat));

// Optimization level: -O0 (default, naive IR straight to the backend), -O1/-O2/-O3
// run the LLVM middle-end (mem2reg/SROA/instcombine/inlining/GVN/...) before codegen.
static llvm::cl::opt<unsigned> OptLevel("O", llvm::cl::Prefix,
    llvm::cl::desc("Optimization level: -O0 (default), -O1, -O2, -O3"),
    llvm::cl::init(0),
    llvm::cl::cat(EskiuCat));

const char* VERSION = "0.9.2";

// `eskiuc run`: set when argv[1] == "run". The program is compiled to a
// temporary executable, run with g_runArgs, then deleted (see main()).
static bool g_runMode = false;
static std::vector<std::string> g_runArgs;
static bool sawSeparator = false;   // a `--` after the script separates program args

// Every input file: the first positional plus the extra ones (`eskiuc a.esk b.esk`).
// The build and the --test-parser/typechecker/codegen modes all merge them.
static std::vector<std::string> allInputs() {
    std::vector<std::string> ins = { std::string(InputFilename) };
    for (const auto& f : ExtraInputs) ins.push_back(f);
    return ins;
}

// Test lexer: tokenize and print all tokens
static int testLexer(const std::string& filename) {
    std::string source = readFile(filename);
    std::map<std::string, Macro> macros;
    seedPredefinedMacros(macros, std::string(TargetTriple), Freestanding);
    Lexer lexer(source, &macros, filename);

    std::cout << "Tokenizing: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    Token tok = lexer.next_token();
    int tokenCount = 0;

    while (tok.type != TokenType::EOF_TOKEN) {
        std::string typeStr = tokenTypeToString(tok.type);
        // Right-align like printf("%3d") / "%15s": pad short fields, never truncate
        // (a column past 999 must not underflow the pad count).
        auto pad = [](const std::string& v, size_t w) { return std::string(v.size() < w ? w - v.size() : 0, ' ') + v; };
        std::cout << "  Line " << pad(std::to_string(tok.line), 3)
                  << ", Col " << pad(std::to_string(tok.column), 3)
                  << "  " << pad(typeStr, 15)
                  << "  '" << tok.value << "'" << std::endl;
        tok = lexer.next_token();
        tokenCount++;
    }

    std::cout << "========================================================" << std::endl;
    std::cout << "Total tokens: " << tokenCount << std::endl;
    return lexer.hadError ? 1 : 0;
}

// Test type checker: tokenize, parse, type check, and report errors
static int testTypeChecker(const std::string& filename) {
    auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return 1;
    }

    std::cout << "Type checking: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Type check
        TypeChecker typeChecker; typeChecker.targetTriple = std::string(TargetTriple);
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
static int testCodegen(const std::string& filename) {
    auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return 1;
    }

    std::cout << "Generating LLVM IR: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Type-check first: the async transform relies on resolved await types,
        // and codegen on the type checker's struct/enum registration.
        TypeChecker tc; tc.targetTriple = std::string(TargetTriple);
        tc.sourceFile = filename;
        if (!tc.check(program.get())) {
            std::cerr << "Type checking failed!" << std::endl;
            return 1;
        }
        AsyncTransform(&tc.expressionTypeMap(), &tc.instanceArgsMap()).run(program.get());
        // Single resolver: re-resolve the post-transform AST; codegen consumes it.
        TypeChecker postTc; postTc.targetTriple = std::string(TargetTriple); postTc.sourceFile = filename;
        if (!postTc.check(program.get())) {
            std::cerr << "error: internal: the async lowering produced a program that does not type-check" << std::endl;
            return 1;
        }
        // Codegen
        CodeGen codegen;
        codegen.resolvedExprTypes = &postTc.expressionTypeMap();
        codegen.semaInstanceArgs = &postTc.instanceArgsMap();
        if (!TargetTriple.empty()) codegen.targetTriple = std::string(TargetTriple);
        if (!TargetCPU.empty()) codegen.targetCPU = std::string(TargetCPU);
        if (!TargetFeatures.empty()) codegen.targetFeatures = std::string(TargetFeatures);
        if (!RelocModel.empty()) codegen.relocModel = std::string(RelocModel);
        codegen.freestanding = Freestanding;
        codegen.safe = Safe;
        codegen.optLevel = OptLevel;
        llvm::Module* module = codegen.generateCode(program);

        if (!module) {
            std::cerr << "Code generation failed!" << std::endl;
            return 1;
        }

        if (OptLevel) codegen.optimizeModule();

        llvm::raw_os_ostream out(std::cout);
        module->print(out, nullptr);
        out.flush();

        std::cout << "========================================================" << std::endl;
        std::cout << "Code generation succeeded!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

// Test parser: tokenize, parse, and print AST
static int testParser(const std::string& filename) {
    auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding);
    if (!program) {
        std::cerr << "Parse failed!" << std::endl;
        return 1;
    }

    std::cout << "Parsing: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    ASTPrinter printer;
    printer.print(program);

    std::cout << "========================================================" << std::endl;
    std::cout << "Parse succeeded!" << std::endl;
    return 0;
}

static int compilerMain(int argc, char** argv) {
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
        // Options that take their value as the next argument (`-o out`, `--target T`):
        // that argument is the option's value, not the script.
        static const std::set<std::string> valueOpts = {
            "-o", "-target", "--target", "-mcpu", "--mcpu", "-mattr", "--mattr",
            "-reloc", "--reloc", "-link-arg", "--link-arg", "-hover-at", "--hover-at",
            "-definition-at", "--definition-at", "-l", "-L",
        };
        std::vector<char*> clArgv = { argv[0] };
        bool gotScript = false;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (!gotScript) {
                if (a == "--") {                              // end of compiler flags
                    if (i + 1 < argc) { clArgv.push_back(argv[++i]); gotScript = true; }
                    continue;
                }
                clArgv.push_back(argv[i]);
                if (valueOpts.count(a) && i + 1 < argc) { clArgv.push_back(argv[++i]); continue; }
                if (a[0] != '-') gotScript = true;           // first non-flag = the script
            } else if (g_runArgs.empty() && a == "--" && !sawSeparator) {
                sawSeparator = true;                          // `run f.esk -- args`: drop the `--`
            } else {
                g_runArgs.push_back(argv[i]);
            }
        }
        int newArgc = (int)clArgv.size();
        llvm::cl::HideUnrelatedOptions(EskiuCat);
        llvm::cl::ParseCommandLineOptions(newArgc, clArgv.data(), OVERVIEW);
    } else {
        llvm::cl::HideUnrelatedOptions(EskiuCat);
        llvm::cl::ParseCommandLineOptions(argc, argv, OVERVIEW);
    }

    // Resolve stdlib root once — used by all parsers for import <name>
    stdlibRoot = resolveStdlibPath();

    // Check input file provided
    if (InputFilename.empty()) {
        std::cerr << "error: no input file specified" << std::endl;
        return 1;
    }
    if (OptLevel > 3) {
        std::cerr << "error: invalid optimization level '-O" << OptLevel
                  << "' (use -O0, -O1, -O2 or -O3)" << std::endl;
        return 1;
    }
    // Refuse an output path that names one of the inputs: `-o prog.esk` would
    // silently replace the source with an object file or executable.
    if (!OutputFilename.empty()) {
        std::string outCanon = Parser::canonicalPath(std::string(OutputFilename));
        std::vector<std::string> ins = { std::string(InputFilename) };
        for (const auto& f : ExtraInputs) ins.push_back(f);
        for (const auto& f : ins) {
            if (Parser::canonicalPath(f) == outCanon) {
                std::cerr << "error: output file '" << std::string(OutputFilename)
                          << "' would overwrite the input '" << f << "'" << std::endl;
                return 1;
            }
        }
    }

    // Handle --test-lexer
    if (TestLexer) {
        return testLexer(InputFilename);
    }

    // Handle --test-parser
    if (TestParser) {
        return testParser(InputFilename);
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
        auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding);
        if (!program) { std::cout << "(parse error)\n"; return 0; }
        try {
            TypeChecker tc; tc.targetTriple = std::string(TargetTriple);
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
        auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding);
        if (!program) { std::cout << "(parse error)\n"; return 0; }
        try {
            TypeChecker tc; tc.targetTriple = std::string(TargetTriple);
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
        return testCodegen(InputFilename);
    }

    // Full compilation pipeline — parse every input file and merge their
    // top-level declarations into a single program (`eskiuc a.esk b.esk ...`).
    try {
        std::map<std::string, Macro> macros;
        bool lexFailed = false;
        auto program = loadProgram(allInputs(), std::string(TargetTriple), Freestanding,
                                   &macros, &lexFailed);
        if (!program) {
            if (!lexFailed) std::cerr << "error: parse failed" << std::endl;
            return 1;
        }
        const std::vector<std::string>& pragmaLibs = program->linkLibs;

        TypeChecker typeChecker; typeChecker.targetTriple = std::string(TargetTriple);
        typeChecker.sourceFile = std::string(InputFilename);
        typeChecker.warnAll = Wall;
        typeChecker.warnExtra = Wextra;
        if (!typeChecker.check(program.get())) {
            return 1;
        }

        AsyncTransform(&typeChecker.expressionTypeMap(), &typeChecker.instanceArgsMap()).run(program.get());
        // Single resolver: re-resolve the post-transform AST; codegen consumes it.
        TypeChecker postTc; postTc.targetTriple = std::string(TargetTriple); postTc.sourceFile = std::string(InputFilename);
        if (!postTc.check(program.get())) {
            std::cerr << "error: internal: the async lowering produced a program that does not type-check" << std::endl;
            return 1;
        }
        CodeGen codegen;
        codegen.resolvedExprTypes = &postTc.expressionTypeMap();
        codegen.semaInstanceArgs = &postTc.instanceArgsMap();
        if (!TargetTriple.empty()) codegen.targetTriple = std::string(TargetTriple);
        if (!TargetCPU.empty()) codegen.targetCPU = std::string(TargetCPU);
        if (!TargetFeatures.empty()) codegen.targetFeatures = std::string(TargetFeatures);
        if (!RelocModel.empty()) codegen.relocModel = std::string(RelocModel);
        codegen.freestanding = Freestanding;
        codegen.safe = Safe;
        codegen.asan = Asan;
        codegen.ubsan = Ubsan;
        codegen.optLevel = OptLevel;
        if (!codegen.generateCode(program)) {
            std::cerr << "error: code generation failed" << std::endl;
            return 1;
        }
        if (OptLevel) codegen.optimizeModule();

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
            // The libraries the program implies go after every object (a --link-arg
            // object may need them too), skipping any -l already given.
            if (!NoDefaultLibs) {
                llvm::Module* mod = codegen.getModule();
                bool usesEH = mod->getFunction("__cxa_throw") || mod->getFunction("__cxa_rethrow") ||
                              mod->getFunction("__gxx_personality_v0") ||
                              mod->getFunction("__gxx_personality_seh0");
                bool usesThreads = mod->getFunction("pthread_create") != nullptr;
                std::vector<std::string> implied = pragmaLibs;
                for (const auto& l : implicitLinkLibs(macros, usesEH, usesThreads)) implied.push_back(l);
                std::set<std::string> seen(libs.begin(), libs.end());
                for (const auto& l : implied)
                    if (seen.insert(l).second) extra.push_back("-l" + l);
            }
            bool ok = linkExecutable(tmpObjPath, outFile, libs, paths, extra, /*sanitized=*/Asan);
            llvm::sys::fs::remove(tmpObjPath);
            if (!ok) { if (g_runMode) llvm::sys::fs::remove(runExePath); return 1; }

            if (g_runMode) {
                int rc = runExecutable(runExePath, g_runArgs);
                llvm::sys::fs::remove(runExePath);
                return rc;
            }
            return 0;
        }

        if (!codegen.emitObjectFile(outFile)) {
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}

// The parser, the type checker, the async transform and codegen recurse once per nesting
// level of the source (parentheses, blocks, nested ifs and lambdas), so the pipeline runs
// on a thread with a large stack: deep input then reaches the parser's nesting limit
// (Parser::kMaxNesting) instead of overflowing a default 8 MB main-thread stack. The
// stack is reserved address space; pages are only touched as deep input needs them. If
// the thread cannot be created, the compiler runs on the current thread.
static constexpr unsigned kPipelineStackBytes = 1u << 30;   // 1 GB

#ifndef _WIN32
namespace {
struct MainArgs { int argc; char** argv; int rc; };
void* runCompilerMain(void* p) {
    auto* a = static_cast<MainArgs*>(p);
    a->rc = compilerMain(a->argc, a->argv);
    return nullptr;
}
}  // namespace
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    int rc = 1;
    llvm::thread worker(std::optional<unsigned>(kPipelineStackBytes),
                        [&] { rc = compilerMain(argc, argv); });
    worker.join();
    return rc;
#else
    MainArgs args{argc, argv, 1};
    pthread_attr_t attr;
    pthread_t tid;
    if (pthread_attr_init(&attr) == 0) {
        bool started = pthread_attr_setstacksize(&attr, kPipelineStackBytes) == 0 &&
                       pthread_create(&tid, &attr, runCompilerMain, &args) == 0;
        pthread_attr_destroy(&attr);
        if (started) {
            pthread_join(tid, nullptr);
            return args.rc;
        }
    }
    return compilerMain(argc, argv);
#endif
}
