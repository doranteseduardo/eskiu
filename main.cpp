#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <climits>
#ifdef __APPLE__
  #include <mach-o/dyld.h>
#elif defined(__linux__)
  #include <unistd.h>
#endif
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_os_ostream.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"
#include "sema/type_checker.h"
#include "codegen/codegen.h"

// Command line options
static llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional,
                                                 llvm::cl::desc("<input .esk file>"));

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

static llvm::cl::opt<std::string> HoverAt("hover-at",
    llvm::cl::desc("Print the Eskiu type at LINE:COL (e.g. --hover-at 8:12)"),
    llvm::cl::value_desc("LINE:COL"));

static llvm::cl::opt<std::string> DefinitionAt("definition-at",
    llvm::cl::desc("Print the definition location of the symbol at LINE:COL"),
    llvm::cl::value_desc("LINE:COL"));

const char* VERSION = "0.1.1";
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
static void testTypeChecker(const std::string& filename) {
    std::string source = readFile(filename);
    Lexer lexer(source);

    // Tokenize
    std::vector<Token> tokens;
    Token tok = lexer.next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
        tok = lexer.next_token();
    }
    tokens.push_back(tok);

    std::cout << "Type checking: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Parse
        Parser parser(tokens);
        parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";
        auto program = parser.parse();

        if (!program) {
            std::cerr << "Parse failed!" << std::endl;
            return;
        }

        // Type check
        TypeChecker typeChecker;
        typeChecker.sourceFile = filename;
        bool success = typeChecker.check(program.get());

        std::cout << "========================================================" << std::endl;
        if (success) {
            std::cout << "Type checking succeeded!" << std::endl;
        } else {
            std::cout << "Type checking failed!" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}

// Test codegen: tokenize, parse, generate LLVM IR, and print it
static void testCodegen(const std::string& filename) {
    std::string source = readFile(filename);
    Lexer lexer(source);

    // Tokenize
    std::vector<Token> tokens;
    Token tok = lexer.next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
        tok = lexer.next_token();
    }
    tokens.push_back(tok);

    std::cout << "Generating LLVM IR: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Parse
        Parser parser(tokens);
        parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";
        auto program = parser.parse();

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
    std::string source = readFile(filename);
    std::cout << "Source loaded: " << source.length() << " bytes" << std::endl;

    Lexer lexer(source);

    // Tokenize
    std::vector<Token> tokens;
    std::cout << "Starting tokenization..." << std::endl;
    Token tok = lexer.next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
        tok = lexer.next_token();
    }
    tokens.push_back(tok); // Add EOF token

    std::cout << "Tokenization complete: " << tokens.size() << " tokens" << std::endl;

    std::cout << "Parsing: " << filename << std::endl;
    std::cout << "========================================================" << std::endl;

    try {
        // Parse
        std::cout << "Creating parser..." << std::endl;
        Parser parser(tokens);
        parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";

        std::cout << "Calling parser.parse()..." << std::endl;
        auto program = parser.parse();

        std::cout << "Parse completed, creating AST printer..." << std::endl;

        // Print AST
        ASTPrinter printer;
        std::cout << "Printing AST..." << std::endl;
        printer.print(program);

        std::cout << "========================================================" << std::endl;
        std::cout << "Parse succeeded!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Parse error: " << e.what() << std::endl;
        return;
    }
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
        testTypeChecker(InputFilename);
        return 0;
    }

    // Handle --hover-at LINE:COL
    if (!HoverAt.empty()) {
        int line = 0, col = 0;
        if (sscanf(HoverAt.c_str(), "%d:%d", &line, &col) != 2) {
            std::cerr << "error: --hover-at expects LINE:COL format\n"; return 1;
        }
        std::string src = readFile(InputFilename);
        Lexer lexer(src);
        std::vector<Token> tokens;
        Token t = lexer.next_token();
        while (t.type != TokenType::EOF_TOKEN) { tokens.push_back(t); t = lexer.next_token(); }
        tokens.push_back(t);
        try {
            Parser parser(tokens);
            parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos
                ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";
            auto program = parser.parse();
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
        std::string src = readFile(InputFilename);
        Lexer lexer(src);
        std::vector<Token> tokens;
        Token t = lexer.next_token();
        while (t.type != TokenType::EOF_TOKEN) { tokens.push_back(t); t = lexer.next_token(); }
        tokens.push_back(t);
        try {
            Parser parser(tokens);
            parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos
                ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";
            auto program = parser.parse();
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

    // Full compilation pipeline
    std::string source = readFile(InputFilename);
    Lexer lexer(source);

    std::vector<Token> tokens;
    Token tok = lexer.next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
        tok = lexer.next_token();
    }
    tokens.push_back(tok);

    try {
        Parser parser(tokens);
        parser.stdlibPath = stdlibRoot; parser.basedir = std::string(InputFilename).rfind("/") != std::string::npos ? std::string(InputFilename).substr(0, std::string(InputFilename).rfind("/")) : ".";
        auto program = parser.parse();
        if (!program) {
            std::cerr << "error: parse failed" << std::endl;
            return 1;
        }

        TypeChecker typeChecker;
        typeChecker.sourceFile = std::string(InputFilename);
        if (!typeChecker.check(program.get())) {
            return 1;
        }

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
