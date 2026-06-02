#include <iostream>
#include <fstream>
#include <sstream>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "lexer/lexer.h"

// Command line options
static llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional,
                                                 llvm::cl::desc("<input .esk file>"));

static llvm::cl::opt<std::string> OutputFilename("o",
                                                  llvm::cl::desc("Output filename"),
                                                  llvm::cl::value_desc("filename"));

static llvm::cl::opt<bool> TestLexer("test-lexer",
                                     llvm::cl::desc("Tokenize input and print token stream"));

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
    std::cout << "============================================" << std::endl;

    Token tok = lexer.next_token();
    int tokenCount = 0;

    while (tok.type != TokenType::EOF_TOKEN) {
        std::cout << "Line " << tok.line << ", Col " << tok.column << ": "
                  << "Token(" << static_cast<int>(tok.type) << ") = '" << tok.value << "'"
                  << std::endl;
        tok = lexer.next_token();
        tokenCount++;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "Total tokens: " << tokenCount << std::endl;
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

    // TODO: implement actual compilation pipeline
    std::cerr << "error: compilation not yet implemented" << std::endl;
    return 1;
}
