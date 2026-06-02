#include <iostream>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "lexer/lexer.h"

// Command line options
static llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional,
                                                 llvm::cl::desc("<input .esk file>"));

static llvm::cl::opt<std::string> OutputFilename("o",
                                                  llvm::cl::desc("Output filename"),
                                                  llvm::cl::value_desc("filename"));

static llvm::cl::opt<bool> ShowVersion("version",
                                        llvm::cl::desc("Show version and exit"));

const char* VERSION = "0.0.1";

int main(int argc, char** argv) {
    llvm::InitLLVM X(argc, argv);
    llvm::cl::ParseCommandLineOptions(argc, argv, "Eskiu Language Compiler\n");

    // Handle --version
    if (ShowVersion) {
        std::cout << "Eskiu " << VERSION << " (LLVM " << LLVM_VERSION_MAJOR << "."
                  << LLVM_VERSION_MINOR << ")" << std::endl;
        return 0;
    }

    // Check input file provided
    if (InputFilename.empty()) {
        std::cerr << "error: no input file specified" << std::endl;
        return 1;
    }

    // TODO: implement actual compilation pipeline
    std::cerr << "error: compilation not yet implemented" << std::endl;
    return 1;
}
