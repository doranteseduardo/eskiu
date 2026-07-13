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
// Visitor Methods
// ============================================================================

void CodeGen::visit(Program* node) {
    // Three-phase lowering so that any declaration may reference any other
    // regardless of source order (forward references, mutual recursion):
    //   1. type shells (structs/unions) + extern declarations
    //   2. function prototypes (free functions and struct methods)
    //   3. bodies / globals
    // Pre-pass: fold top-level `const` ints so they can be used as array sizes
    // in struct fields / globals declared anywhere (resolved during phase 1).
    for (auto& decl : node->declarations) {
        if (auto* v = dynamic_cast<VarDecl*>(decl.get())) {
            if (v->isConst && v->initializer) {
                if (auto* c = evaluateConstantExpr(v->initializer))
                    if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c))
                        constInts[v->name] = ci->getSExtValue();
            }
        }
    }
    for (auto& decl : node->declarations) {
        if (auto* s = dynamic_cast<StructDecl*>(decl.get())) {
            declareStructType(s); // registers template structs and creates concrete types
        } else if (dynamic_cast<UnionDecl*>(decl.get()) ||
                   dynamic_cast<InterfaceDecl*>(decl.get()) ||
                   dynamic_cast<EnumDecl*>(decl.get()) ||
                   dynamic_cast<TypeAliasDecl*>(decl.get()) ||
                   dynamic_cast<ExternDecl*>(decl.get()) ||
                   dynamic_cast<IntrinsicDecl*>(decl.get())) {
            decl->accept(this);
        }
    }
    for (auto& decl : node->declarations) {
        if (auto* f = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (f->typeParams.empty())
                declareFunction(f->name, f->returnType, f->params);
        } else if (auto* s = dynamic_cast<StructDecl*>(decl.get())) {
            if (!s->typeParams.empty()) continue;
            for (auto& method : s->methods) {
                if (auto* mf = dynamic_cast<FunctionDecl*>(method.get())) {
                    std::vector<std::pair<std::string, std::string>> params;
                    params.push_back({"*" + s->name, "self"});
                    for (auto& p : mf->params) params.push_back(p);
                    declareFunction(s->name + "_" + mf->name, mf->returnType, params);
                }
            }
        }
    }
    for (auto& decl : node->declarations) {
        // Externs, unions, interfaces, enums, and aliases were handled in phase 1.
        if (dynamic_cast<ExternDecl*>(decl.get()) ||
            dynamic_cast<IntrinsicDecl*>(decl.get()) ||
            dynamic_cast<UnionDecl*>(decl.get()) ||
            dynamic_cast<InterfaceDecl*>(decl.get()) ||
            dynamic_cast<EnumDecl*>(decl.get()) ||
            dynamic_cast<TypeAliasDecl*>(decl.get()))
            continue;
        decl->accept(this);
    }
}

llvm::Function* CodeGen::declareFunction(
        const std::string& name, const std::string& returnTypeStr,
        const std::vector<std::pair<std::string, std::string>>& params) {
    // Get parameter types
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : params) {
        if (param.first == "...") continue; // variadic — handled by extern decls
        paramTypes.push_back(getTypeFromString(param.first));
    }

    // Use sret for large struct returns
    llvm::Type* returnType = getTypeFromString(returnTypeStr);
    bool sret = needsSret(returnType);
    if (sret) {
        funcSretTypes[name] = llvm::cast<llvm::StructType>(returnType);
        // Prepend hidden sret pointer as first parameter
        paramTypes.insert(paramTypes.begin(), llvm::PointerType::get(*context, 0));
    }

    // Eskiu param types — for interface boxing at call sites
    {
        std::vector<std::string> pts;
        for (auto& p : params)
            if (p.first != "...") pts.push_back(p.first);
        funcEskiuParamTypes[name] = pts;
    }
    funcEskiuReturnType[name] = returnTypeStr;

    // Idempotent: reuse a prototype declared by the pre-pass.
    if (llvm::Function* existing = module->getFunction(name)) return existing;

    bool isVarArg = false;
    for (auto& p : params) if (p.first == "...") isVarArg = true;
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        sret ? llvm::Type::getVoidTy(*context) : returnType, paramTypes, isVarArg);
    llvm::Function* func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, name, module.get());

    // Set parameter names (skip index 0 for sret functions — that's the hidden ret ptr)
    size_t paramIdx = 0;
    size_t argIdx   = 0;
    for (auto& arg : func->args()) {
        if (sret && argIdx == 0) {
            arg.setName("sret.ptr");
            argIdx++;
            continue;
        }
        if (paramIdx < params.size() && params[paramIdx].first != "...") {
            arg.setName(params[paramIdx].second);
            paramIdx++;
        }
        argIdx++;
    }
    return func;
}

void CodeGen::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) {
        funcTemplateDecls[node->name] = node;
        return;
    }

    // Declare (or reuse) the prototype, then emit the body.
    llvm::Function* func = declareFunction(node->name, node->returnType, node->params);

    // A body-less declaration (forward declaration) only needs the prototype.
    if (!node->body) return;
    // Defensive: skip if a body was already emitted (e.g. forward decl + definition).
    if (!func->empty()) return;

    llvm::Type* returnType = getTypeFromString(node->returnType);
    bool sret = needsSret(returnType);

    // Create entry block
    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entryBlock);

    // Save current function + sret context
    llvm::Function* prevFunc       = currentFunction;
    llvm::Value*    prevSretParam  = currentSretParam;
    currentFunction = func;
    currentSretParam = sret ? &*func->arg_begin() : nullptr;

    // A nested function body (e.g. a template instantiated mid-expression) is a fresh
    // scope-exit context: save and reset the cleanup stack + loop targets so this body
    // never runs the enclosing function's defers/finally or branches to its loops.
    std::vector<std::vector<Cleanup>> prevCleanups = std::move(cleanupScopes);
    size_t prevBreakCD = breakCleanupDepth, prevContinueCD = continueCleanupDepth;
    llvm::BasicBlock* prevBreakT = breakTarget, *prevContinueT = continueTarget;
    cleanupScopes.clear();
    breakCleanupDepth = continueCleanupDepth = 0;
    breakTarget = continueTarget = nullptr;

    // Push scope for function parameters
    pushScope();
    // (Eskiu param types for interface boxing were registered by declareFunction.)

    // Define parameters in symbol table + type map (skip sret hidden param at index 0)
    size_t paramIdx = 0;
    size_t argIdx   = 0;
    for (auto& arg : func->args()) {
        if (sret && argIdx == 0) { argIdx++; continue; }  // skip sret ptr
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            // Give every parameter a stack slot: it makes the parameter a mutable
            // lvalue (so the body may reassign it, like a local) and gives
            // struct-by-value params a pointer for MemberExpr GEP. The incoming
            // argument is stored into the slot; reads load from it.
            auto* a = entryAlloca(arg.getType(), nullptr,
                                            node->params[paramIdx].second);
            builder->CreateStore(&arg, a);
            llvm::Value* paramSlot = a;
            defineSymbol(node->params[paramIdx].second, paramSlot);
            std::string ptype = !typeParamOverride.empty()
                ? substType(node->params[paramIdx].first, typeParamOverride)
                : node->params[paramIdx].first;
            if (ptype.find('<') != std::string::npos) {
                std::string sfx;
                while (!ptype.empty() && ptype.back() == '*') { sfx += '*'; ptype.pop_back(); }
                // Instantiate the template instance's struct now, so member
                // access on this param (e.g. List<String>* self -> self.data)
                // finds its fields even if no earlier code referenced the type.
                auto [tn, targs] = splitTemplateType(ptype);
                ensureTemplateInstantiated(mangleTemplate(ptype), tn, targs);
                ptype = mangleTemplate(ptype) + sfx;
            }
            defineVarType(node->params[paramIdx].second, ptype);
            paramIdx++;
        }
        argIdx++;
    }

    // Generate function body
    if (node->body) {
        node->body->accept(this);
    }

    // Default return if no explicit return emitted
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (sret || returnType->isVoidTy()) {
            builder->CreateRetVoid();
        } else if (returnType->isIntegerTy()) {
            builder->CreateRet(llvm::ConstantInt::get(returnType, 0));
        } else {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // Restore context
    popScope();
    currentFunction  = prevFunc;
    currentSretParam = prevSretParam;
    cleanupScopes = std::move(prevCleanups);
    breakCleanupDepth = prevBreakCD; continueCleanupDepth = prevContinueCD;
    breakTarget = prevBreakT; continueTarget = prevContinueT;
}

void CodeGen::visit(VarDecl* node) {
    // Register `const` ints so a later (local) array dimension can use them.
    if (node->isConst && node->initializer && !constInts.count(node->name)) {
        if (auto* c = evaluateConstantExpr(node->initializer))
            if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c))
                constInts[node->name] = ci->getSExtValue();
    }

    llvm::Type* declType = getTypeFromString(node->type);

    // Global scope (no active function) → emit as llvm::GlobalVariable
    if (currentFunction == nullptr) {
        llvm::Constant* init = node->initializer
            ? evaluateConstantExpr(node->initializer)
            : nullptr;
        // Coerce initializer to match declared type
        if (init && init->getType() != declType) {
            if (init->getType()->isIntegerTy() && declType->isIntegerTy()) {
                // Use ConstantInt directly
                uint64_t v = llvm::cast<llvm::ConstantInt>(init)->getZExtValue();
                init = llvm::ConstantInt::get(declType, v);
            } else if (init->getType()->isFloatingPointTy() && declType->isFloatingPointTy()) {
                double v = llvm::cast<llvm::ConstantFP>(init)->getValueAPF().convertToDouble();
                init = llvm::ConstantFP::get(declType, v);
            }
        }
        if (!init) init = llvm::Constant::getNullValue(declType);

        auto* gv = new llvm::GlobalVariable(
            *module, declType, /*isConstant=*/false,
            llvm::GlobalValue::PrivateLinkage, init, node->name);

        defineSymbol(node->name, gv);
        defineVarType(node->name, node->type);
        return;
    }
    // Static local: one instance in module scope, persists across calls.
    if (node->isStatic) {
        llvm::Constant* init = node->initializer
            ? evaluateConstantExpr(node->initializer)
            : nullptr;
        if (init && init->getType() != declType) {
            if (init->getType()->isIntegerTy() && declType->isIntegerTy())
                init = llvm::ConstantInt::get(
                    declType, llvm::cast<llvm::ConstantInt>(init)->getZExtValue());
            else if (init->getType()->isFloatingPointTy() && declType->isFloatingPointTy())
                init = llvm::ConstantFP::get(
                    declType,
                    llvm::cast<llvm::ConstantFP>(init)->getValueAPF().convertToDouble());
        }
        if (!init) init = llvm::Constant::getNullValue(declType);
        std::string gname = currentFunction->getName().str() + "." + node->name;
        auto* gv = new llvm::GlobalVariable(
            *module, declType, /*isConstant=*/false,
            llvm::GlobalValue::PrivateLinkage, init, gname);
        defineSymbol(node->name, gv);
        defineVarType(node->name, node->type);
        return;
    }

    llvm::AllocaInst* alloca = entryAlloca(declType, nullptr, node->name);
    if (node->isVolatile) volatileVars.insert(node->name);
    defineSymbol(node->name, alloca);
    // Resolve type params and mangle template names for varTypeStack,
    // preserving pointer suffixes (e.g. "List<int>*" → "List_int*")
    std::string varType = !typeParamOverride.empty()
                          ? substType(node->type, typeParamOverride)
                          : node->type;
    if (varType.find('<') != std::string::npos) {
        // Strip trailing pointer stars, mangle the base, then re-append stars
        std::string suffix;
        while (!varType.empty() && varType.back() == '*') {
            suffix += '*'; varType.pop_back();
        }
        varType = mangleTemplate(varType) + suffix;
    }
    defineVarType(node->name, varType);

    if (node->initializer) {
        if (auto structInit = dynamic_cast<StructInitExpr*>(node->initializer.get())) {
            // Fill the alloca directly — no temporary needed
            emitStructInitInto(alloca, structInit);
        } else if (auto arrLit = dynamic_cast<ArrayLitExpr*>(node->initializer.get())) {
            emitArrayInitInto(alloca, arrLit, varType);
        } else {
            llvm::Value* val = evaluateExpr(node->initializer);
            if (val && val->getType() != declType) {
                if (val->getType()->isIntegerTy() && declType->isIntegerTy()) {
                    val = coerceInt(val, declType, eskiuUnsigned(getExprEskiuType(node->initializer)));
                } else if (val->getType()->isIntegerTy() && declType->isFloatingPointTy()) {
                    val = intToFloat(val, declType, eskiuUnsigned(getExprEskiuType(node->initializer)));
                } else if (val->getType()->isFloatingPointTy() && declType->isIntegerTy()) {
                    val = builder->CreateFPToSI(val, declType);
                } else if (val->getType()->isFloatingPointTy() && declType->isFloatingPointTy()) {
                    val = builder->CreateFPCast(val, declType);
                }
            }
            if (val) builder->CreateStore(val, alloca);
        }
    }
}

bool CodeGen::buildPackedLayout(const std::vector<StructDecl::Field>& fields, unsigned packN,
                                std::vector<llvm::Type*>& phys,
                                std::map<std::string, BitfieldSlot>& slots) {
    const llvm::DataLayout& DL = module->getDataLayout();
    llvm::Type* i8 = llvm::Type::getInt8Ty(*context);
    uint64_t offset = 0, structAlign = 1;
    for (const auto& f : fields) {
        if (f.bitWidth > 0) return false;  // pack + bitfields: fall back to the bitfield path
        llvm::Type* ft = getTypeFromString(f.type);
        uint64_t align = std::min<uint64_t>(DL.getABITypeAlign(ft).value(), packN);
        if (align > structAlign) structAlign = align;
        uint64_t aligned = (offset + align - 1) / align * align;
        if (aligned > offset) { phys.push_back(llvm::ArrayType::get(i8, aligned - offset)); offset = aligned; }
        BitfieldSlot s;
        s.isBitfield = false;
        s.physIndex = (unsigned)phys.size();
        s.storageType = ft;
        slots[f.name] = s;
        phys.push_back(ft);
        offset += DL.getTypeAllocSize(ft).getFixedValue();
    }
    // Round the total size up to the struct's alignment (min(maxFieldAlign, N)),
    // so an array element stride matches the C `#pragma pack(N)` ABI.
    uint64_t total = (offset + structAlign - 1) / structAlign * structAlign;
    if (total > offset) phys.push_back(llvm::ArrayType::get(i8, total - offset));
    return true;
}

void CodeGen::declareStructType(StructDecl* node) {
    if (!node->typeParams.empty()) {
        templateDecls[node->name] = node;
        return;
    }
    if (structTypes.count(node->name)) return; // already created by the pre-pass

    bool hasBitfields = false;
    for (const auto& f : node->fields) if (f.bitWidth > 0) hasBitfields = true;

    if (!hasBitfields) {
        // #pragma pack(N>=2): manual layout (padding + physical-index remap).
        if (node->packAlign >= 2) {
            std::vector<llvm::Type*> phys;
            std::map<std::string, BitfieldSlot> slots;
            buildPackedLayout(node->fields, (unsigned)node->packAlign, phys, slots);
            structTypes[node->name]  = llvm::StructType::create(*context, phys, node->name, /*isPacked=*/true);
            structFields[node->name] = node->fields;
            structLayout[node->name] = slots;
            return;
        }
        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : node->fields)
            fieldTypes.push_back(getTypeFromString(field.type));
        structTypes[node->name] = llvm::StructType::create(*context, fieldTypes, node->name, node->isPacked);
        structFields[node->name] = node->fields;
        return;
    }

    // Packed layout: pack consecutive bitfields into storage words of their
    // declared type; non-bitfield fields close the current word and get their
    // own slot. Each field records its physical slot + bit position.
    std::vector<llvm::Type*> phys;
    std::map<std::string, BitfieldSlot> slots;
    int curPhys = -1; unsigned curUnitBits = 0, curOffset = 0;
    for (const auto& f : node->fields) {
        if (f.bitWidth > 0) {
            llvm::Type* sty = getTypeFromString(f.type);
            unsigned stBits = sty->getIntegerBitWidth();
            if (curPhys < 0 || curUnitBits != stBits ||
                curOffset + (unsigned)f.bitWidth > stBits) {
                phys.push_back(sty);
                curPhys = (int)phys.size() - 1;
                curUnitBits = stBits; curOffset = 0;
            }
            BitfieldSlot s;
            s.isBitfield = true; s.physIndex = (unsigned)curPhys;
            s.bitOffset = curOffset; s.bitWidth = (unsigned)f.bitWidth;
            s.storageType = sty; s.isSigned = (f.type.rfind("uint", 0) != 0);
            slots[f.name] = s;
            curOffset += (unsigned)f.bitWidth;
        } else {
            curPhys = -1; curUnitBits = 0; curOffset = 0;
            llvm::Type* ft = getTypeFromString(f.type);
            phys.push_back(ft);
            BitfieldSlot s;
            s.isBitfield = false; s.physIndex = (unsigned)phys.size() - 1;
            s.storageType = ft;
            slots[f.name] = s;
        }
    }
    structTypes[node->name]  = llvm::StructType::create(*context, phys, node->name, node->isPacked);
    structFields[node->name] = node->fields;
    structLayout[node->name] = slots;
}

void CodeGen::visit(StructDecl* node) {
    if (!node->typeParams.empty()) {
        templateDecls[node->name] = node;
        return;
    }

    declareStructType(node);

    // Emit methods as mangled functions: StructName_methodName(self: *Struct, ...)
    for (const auto& method : node->methods) {
        if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
            std::vector<std::pair<std::string, std::string>> params;
            params.push_back({"*" + node->name, "self"});
            for (const auto& p : func->params) params.push_back(p);

            auto mangled = std::make_shared<FunctionDecl>(
                node->name + "_" + func->name,
                func->returnType, params, func->body);
            mangled->accept(this);
        }
    }
}

void CodeGen::visit(ExternDecl* node) {
    // Get parameter types
    std::vector<llvm::Type*> paramTypes;
    bool hasVarargs = false;

    for (auto& param : node->params) {
        if (param.first == "...") {
            hasVarargs = true;
            break;
        }
        paramTypes.push_back(getTypeFromString(param.first));
    }

    // Create function type
    llvm::Type* returnType = getTypeFromString(node->returnType);
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, hasVarargs);

    // Create external function declaration
    llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, node->name, module.get());
}

void CodeGen::visit(IntrinsicDecl* node) {
    // No declaration is emitted: a call to an intrinsic lowers to inline IR
    // (see the intrinsic dispatch at the top of visit(CallExpr)). We only record
    // the name so that callsites can be recognised regardless of source order.
    intrinsicNames.insert(node->name);
}

void CodeGen::visit(TypeAliasDecl* node) {
    typeAliases[node->name] = node->aliased;
}

void CodeGen::visit(UnionDecl* node) {
    // Compute size = max(sizeof(field)) across all fields
    uint64_t maxSize = 0;
    for (const auto& f : node->fields) {
        llvm::Type* ft = getTypeFromString(f.type);
        uint64_t sz = module->getDataLayout().getTypeAllocSize(ft);
        if (sz > maxSize) maxSize = sz;
    }
    if (maxSize == 0) maxSize = 1;

    // Store as opaque byte array — same as struct in LLVM type registry
    llvm::Type* unionTy = llvm::ArrayType::get(
        llvm::Type::getInt8Ty(*context), maxSize);
    std::string mangledName = node->name;
    structTypes[mangledName] = llvm::cast<llvm::StructType>(
        llvm::StructType::get(*context, {unionTy}, /*isPacked=*/false));
    // Actually use a named struct wrapping the byte array for cleaner IR
    auto* namedTy = llvm::StructType::create(*context, {unionTy}, mangledName + ".union");
    structTypes[mangledName] = namedTy;

    // Register fields so MemberExpr can resolve them (all at offset 0, typed via cast)
    unionFields[mangledName] = node->fields;
    // Also register in structFields for MemberExpr type lookup
    std::vector<StructDecl::Field> sf;
    for (const auto& f : node->fields) sf.push_back({f.type, f.name});
    structFields[mangledName] = sf;
}
