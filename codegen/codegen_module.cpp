#include "codegen.h"
#include "../ast/type_qual.h"
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

// Map the --reloc string to an LLVM relocation model. 3DS .3dsx targets need
// "static": the loader applies static relocations and has no dynamic loader to
// populate a GOT, so PIC (GOT/PLT) code reads garbage and faults.
static std::optional<llvm::Reloc::Model> parseRelocModel(const std::string& s) {
    if (s == "static")         return llvm::Reloc::Static;
    if (s == "dynamic-no-pic") return llvm::Reloc::DynamicNoPIC;
    return llvm::Reloc::PIC_;   // default (empty or "pic")
}

// Register the code-emission backends eskiuc supports: AArch64 and 32-bit ARM
// always, X86 only when the build links it (dynamic LLVM, or a non-Apple static
// build; the static Apple release ships without X86 libs). `withAsm` also pulls in
// the assembly printer/parser, which the object-emitting paths need.
static void initCodegenTargets(bool withAsm) {
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTargetMC();
#ifdef ESKIU_HAS_X86
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
#endif
    if (withAsm) {
        LLVMInitializeAArch64AsmPrinter();
        LLVMInitializeAArch64AsmParser();
        LLVMInitializeARMAsmPrinter();
        LLVMInitializeARMAsmParser();
#ifdef ESKIU_HAS_X86
        LLVMInitializeX86AsmPrinter();
        LLVMInitializeX86AsmParser();
#endif
    }
}

// Build a TargetMachine for `triple`, applying the CPU, feature, relocation, and
// float-ABI selection shared by every code-emitting path (returns nullptr when the
// triple has no registered target). Centralizing this keeps the three call sites
// from drifting: an earlier version set the hard-float ABI in module-gen and the
// optimizer but not the object emitter, so the emitted object dropped the
// Tag_ABI_VFP_args attribute that hard-float libraries like libctru require.
static std::unique_ptr<llvm::TargetMachine> makeTargetMachine(
        const CodeGen& cg, const llvm::Triple& triple, const std::string& tripleStr) {
    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, err);
    if (!target) return nullptr;
    bool isCross = !cg.targetTriple.empty() &&
        cg.targetTriple != llvm::sys::getDefaultTargetTriple();
    llvm::StringRef cpu = !cg.targetCPU.empty() ? llvm::StringRef(cg.targetCPU)
        : (isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName());
    llvm::TargetOptions opt;
    // Hard-float ABI for hard-float ARM triples (those ending in "hf", e.g. the
    // 3DS's armv6k-none-eabihf). LLVM parses "eabihf" into the OS field, not the
    // environment, so match the triple string directly. This makes LLVM emit the
    // Tag_ABI_VFP_args "VFP registers" build attribute, required to link against
    // hard-float libraries like libctru.
    if (tripleStr.size() >= 2 && tripleStr.compare(tripleStr.size() - 2, 2, "hf") == 0)
        opt.FloatABIType = llvm::FloatABI::Hard;
    return std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple, cpu, cg.targetFeatures, opt,
                                    parseRelocModel(cg.relocModel)));
}

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("eskiu", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

CodeGen::~CodeGen() = default;

llvm::Module* CodeGen::generateCode(std::shared_ptr<Program> program) {
    // Set target triple + data layout early so sizeof queries work in alloc()
    initCodegenTargets(/*withAsm=*/true);
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);
    if (auto tm = makeTargetMachine(*this, triple, tripleStr))
        module->setDataLayout(tm->createDataLayout());

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

    initCodegenTargets(/*withAsm=*/false);

    // Target-aware pipeline: give the PassBuilder a TargetMachine + DataLayout so
    // target heuristics (and the data layout the optimizer needs for, e.g., SROA)
    // are correct. Mirrors emitObjectFile's target setup.
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::unique_ptr<llvm::TargetMachine> tm = makeTargetMachine(*this, triple, tripleStr);
    if (tm) module->setDataLayout(tm->createDataLayout());

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
    initCodegenTargets(/*withAsm=*/true);

    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::unique_ptr<llvm::TargetMachine> tm = makeTargetMachine(*this, triple, tripleStr);
    if (!tm) {
        std::cerr << "error: no registered target for triple '" << tripleStr << "'" << std::endl;
        return false;
    }
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
