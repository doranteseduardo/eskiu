#include "codegen.h"
#include "../ast/type_qual.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/raw_os_ostream.h"
#include <iostream>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// ============================================================================

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("eskiu", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

CodeGen::~CodeGen() = default;

llvm::Module* CodeGen::generateCode(std::shared_ptr<Program> program) {
    // Set target triple + data layout early so sizeof queries work in alloc()
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
#ifdef ESKIU_HAS_X86
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
#endif
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);
    std::string terr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, terr);
    if (target) {
        bool isCross = !targetTriple.empty() &&
            targetTriple != llvm::sys::getDefaultTargetTriple();
        auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> tm(
            target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_));
        module->setDataLayout(tm->createDataLayout());
    }

    program->accept(this);

    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    if (llvm::verifyModule(*module, &errorStream)) {
        std::cerr << "LLVM verification failed:\n" << errorStr << std::endl;
        return nullptr;
    }

    return module.get();
}

void CodeGen::printIR() const {
    if (module) {
        std::cerr << "Module has " << module->getFunctionList().size() << " functions\n";
        llvm::raw_os_ostream out(std::cout);
        module->print(out, nullptr);
        out.flush();
    } else {
        std::cerr << "Module is null!\n";
    }
}

void CodeGen::optimizeModule() {
    if (optLevel == 0) return;

    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();

    // Target-aware pipeline: give the PassBuilder a TargetMachine + DataLayout so
    // target heuristics (and the data layout the optimizer needs for, e.g., SROA)
    // are correct. Mirrors emitObjectFile's target setup.
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, err);
    std::unique_ptr<llvm::TargetMachine> tm;
    if (target) {
        bool isCross = !targetTriple.empty() &&
            targetTriple != llvm::sys::getDefaultTargetTriple();
        auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
        llvm::TargetOptions opt;
        tm.reset(target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_));
        if (tm) module->setDataLayout(tm->createDataLayout());
    }

    llvm::OptimizationLevel lvl;
    switch (optLevel) {
        case 1:  lvl = llvm::OptimizationLevel::O1; break;
        case 2:  lvl = llvm::OptimizationLevel::O2; break;
        default: lvl = llvm::OptimizationLevel::O3; break;   // 3 and above
    }

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    llvm::PassBuilder PB(tm.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(lvl);
    MPM.run(*module, MAM);
}

bool CodeGen::emitObjectFile(const std::string& filename) {
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();

    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, err);
    if (!target) {
        std::cerr << "error: " << err << std::endl;
        return false;
    }

    // Use native CPU for native compilation; generic CPU when cross-compiling
    bool isCross = !targetTriple.empty() &&
        targetTriple != llvm::sys::getDefaultTargetTriple();
    auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
    llvm::TargetOptions opt;
    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_));
    module->setDataLayout(tm->createDataLayout());

    // Sanitizer instrumentation (new pass manager), run over the whole module
    // before code generation. --asan instruments memory accesses (the asan
    // runtime is linked separately); --ubsan inserts trapping bounds checks.
    if (asan || ubsan) {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;
        llvm::PassBuilder PB(tm.get());
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        // AddressSanitizer only instruments functions marked sanitize_address
        // (Clang stamps this per function); add it to every defined function.
        if (asan) {
            for (llvm::Function& F : *module)
                if (!F.isDeclaration()) F.addFnAttr(llvm::Attribute::SanitizeAddress);
        }

        llvm::ModulePassManager MPM;
        if (ubsan) {
            llvm::BoundsCheckingPass::Options opts;   // empty Runtime => trap on OOB
            llvm::FunctionPassManager FPM;
            FPM.addPass(llvm::BoundsCheckingPass(opts));
            MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        }
        if (asan) {
            MPM.addPass(llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions{}));
        }
        MPM.run(*module, MAM);
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "error: cannot open '" << filename << "': " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        std::cerr << "error: target cannot emit object file" << std::endl;
        return false;
    }

    pm.run(*module);
    dest.flush();
    return true;
}
