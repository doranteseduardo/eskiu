#include <iostream>
#include <fstream>
#include <sstream>
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

const char* VERSION = "0.0.1";

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
        auto program = parser.parse();

        if (!program) {
            std::cerr << "Parse failed!" << std::endl;
            return;
        }

        // Type check
        TypeChecker typeChecker;
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
        auto program = parser.parse();

        // Codegen
        CodeGen codegen;
        auto module = codegen.generateCode(program);

        if (!module) {
            std::cerr << "Code generation failed!" << std::endl;
            return;
        }

        // Print LLVM IR before module is destroyed
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

    // Handle --test-codegen
    if (TestCodegen) {
        testCodegen(InputFilename);
        return 0;
    }

    // TODO: implement actual compilation pipeline
    std::cerr << "error: compilation not yet implemented" << std::endl;
    return 1;
}
