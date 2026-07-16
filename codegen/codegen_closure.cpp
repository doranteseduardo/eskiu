#include "codegen.h"
#include "../ast/type_qual.h"
#include "llvm/IR/InlineAsm.h"

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// CodeGen — closures/lambdas, exception handling (throw/try + invoke),
// threads, await, and inline asm.
// Part of the codegen_expr.cpp split; see codegen.h.

// Emit the lambda's underlying LLVM function (params: env* first, then the
// declared params) and compile its body. `envTy` is the capture-env struct type
// (null for a non-capturing lambda). Saves/restores the caller's insert point, so
// this is safe to call from a global (builder-less) context. Returns the function.
llvm::Function* CodeGen::emitLambdaFunction(LambdaExpr* node,
                                            const std::string& lambdaName,
                                            llvm::StructType* envTy) {
    bool hasCaptures = !node->captures.empty();
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);

    // ── Build lambda function: env* always first param ───────────────────
    std::vector<llvm::Type*> paramTypes = {ptrTy}; // env* (null if no captures)
    for (const auto& p : node->params)
        paramTypes.push_back(getTypeFromString(p.first));

    llvm::Type* retTy = getTypeFromString(node->returnType);
    llvm::FunctionType* fty = llvm::FunctionType::get(retTy, paramTypes, false);
    llvm::Function* func = llvm::Function::Create(
        fty, llvm::Function::InternalLinkage, lambdaName, module.get());

    auto argIt = func->arg_begin();
    argIt->setName("env");
    llvm::Argument* envArg = &*argIt++;
    size_t i = 0;
    for (; argIt != func->arg_end(); ++argIt, ++i)
        argIt->setName(node->params[i].second);

    // ── Compile lambda body ───────────────────────────────────────────────
    llvm::Function* prevFunc     = currentFunction;
    llvm::Value*    prevSret     = currentSretParam;
    llvm::BasicBlock* prevInsert = builder->GetInsertBlock();

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entry);
    currentFunction  = func;
    currentSretParam = nullptr;
    pushScope();

    // Expose captured variables by loading from env
    if (hasCaptures) {
        for (size_t ci = 0; ci < node->captures.size(); ++ci) {
            const auto& [capName, capType] = node->captures[ci];
            llvm::Type* capLLVMTy = getTypeFromString(capType);
            auto* capAlloca = entryAlloca(capLLVMTy, nullptr, capName);
            auto* gep = builder->CreateStructGEP(envTy, envArg, ci, capName + ".gep");
            auto* val = builder->CreateLoad(capLLVMTy, gep, capName + ".val");
            builder->CreateStore(val, capAlloca);
            defineSymbol(capName, capAlloca);
            defineVarType(capName, capType);
        }
    }

    // Define parameters
    i = 0;
    argIt = func->arg_begin();
    ++argIt; // skip env
    for (; argIt != func->arg_end(); ++argIt, ++i) {
        llvm::Value* slot = &*argIt;
        if (argIt->getType()->isStructTy()) {
            auto* a = entryAlloca(argIt->getType(), nullptr,
                                            node->params[i].second + ".byval");
            builder->CreateStore(&*argIt, a);
            slot = a;
        }
        defineSymbol(node->params[i].second, slot);
        defineVarType(node->params[i].second, node->params[i].first);
    }

    if (node->body) node->body->accept(this);
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (retTy->isVoidTy()) builder->CreateRetVoid();
        else builder->CreateRet(llvm::Constant::getNullValue(retTy));
    }

    popScope();
    currentFunction  = prevFunc;
    currentSretParam = prevSret;
    if (prevInsert) builder->SetInsertPoint(prevInsert);
    return func;
}

void CodeGen::visit(LambdaExpr* node) {
    std::string lambdaName = "__lambda" + std::to_string(lambdaSeq++);
    bool hasCaptures = !node->captures.empty();

    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);

    // ── Build env struct type (one field per captured variable) ──────────
    llvm::StructType* envTy = nullptr;
    llvm::Value*      envAlloca = nullptr;
    if (hasCaptures) {
        std::vector<llvm::Type*> envFields;
        for (const auto& [name, type] : node->captures)
            envFields.push_back(getTypeFromString(type));
        envTy = llvm::StructType::create(*context, envFields,
                                         lambdaName + ".env");
        if (node->escapes) {
            // Escaping closure (returned, stored, or passed to an `escaping`
            // parameter): heap-allocate the env so it outlives this function.
            // Freed via free_closure (the async transform emits it at the owner
            // boundary; otherwise the owner frees it explicitly).
            uint64_t envSize = module->getDataLayout().getTypeAllocSize(envTy);
            llvm::Function* mallocFn = getOrDeclareFunc(
                "malloc", ptrTy, {llvm::Type::getInt64Ty(*context)}, false);
            envAlloca = builder->CreateCall(mallocFn,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), envSize)},
                lambdaName + ".env.heap");
        } else {
            // Non-escaping closure: the env dies with this frame — stack-allocate
            // it. Zero cost, no leak. (The common map/filter/apply case.)
            envAlloca = entryAlloca(envTy, nullptr, lambdaName + ".env");
        }
        for (size_t ci = 0; ci < node->captures.size(); ++ci) {
            llvm::Value* capturedVal = nullptr;
            llvm::Value* sym = lookupSymbol(node->captures[ci].first);
            if (sym) {
                // Load the current value from the outer alloca/variable
                if (llvm::isa<llvm::AllocaInst>(sym)) {
                    auto* alloca = llvm::cast<llvm::AllocaInst>(sym);
                    capturedVal = builder->CreateLoad(
                        alloca->getAllocatedType(), sym, node->captures[ci].first);
                } else {
                    capturedVal = sym;
                }
            }
            if (capturedVal) {
                auto* gep = builder->CreateStructGEP(envTy, envAlloca, ci);
                builder->CreateStore(capturedVal, gep);
            }
        }
    }

    llvm::Function* func = emitLambdaFunction(node, lambdaName, envTy);

    // ── Build fat pointer {fn_ptr, env_ptr} ──────────────────────────────
    llvm::StructType* fatTy = llvm::cast<llvm::StructType>(
        getTypeFromString("fn()->void")); // any fn type gives {ptr,ptr}
    llvm::Value* fatAlloca = entryAlloca(fatTy, nullptr, lambdaName + ".fat");
    auto* fnSlot  = builder->CreateStructGEP(fatTy, fatAlloca, 0);
    auto* envSlot = builder->CreateStructGEP(fatTy, fatAlloca, 1);
    builder->CreateStore(func, fnSlot);
    builder->CreateStore(
        hasCaptures ? (llvm::Value*)envAlloca
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
        envSlot);
    exprValueStack.push(builder->CreateLoad(fatTy, fatAlloca, lambdaName + ".fat.val"));
}

// ── Exception helpers (invoke/landingpad) ─────────────────────────────────

// Ensure personality function and _ZTIPv type_info are declared in the module.
static void ensureEHDecls(llvm::Module* mod, llvm::LLVMContext& ctx) {
    if (mod->getFunction("__gxx_personality_v0")) return;
    llvm::Type* i32  = llvm::Type::getInt32Ty(ctx);
    llvm::Type* ptrTy = llvm::PointerType::get(ctx, 0);
    // Personality function
    llvm::FunctionType* persType = llvm::FunctionType::get(i32, true);
    llvm::Function::Create(persType, llvm::Function::ExternalLinkage,
        "__gxx_personality_v0", mod);
    // _ZTIPv — void* type_info (from libc++)
    if (!mod->getNamedGlobal("_ZTIPv"))
        new llvm::GlobalVariable(*mod, ptrTy, true,
            llvm::GlobalValue::ExternalLinkage, nullptr, "_ZTIPv");
}

llvm::Value* CodeGen::createMaybeInvoke(
    llvm::FunctionType* fty, llvm::Value* callee,
    llvm::ArrayRef<llvm::Value*> args, const llvm::Twine& name) {

    if (!unwindTarget)
        return builder->CreateCall(fty, callee, args, name);

    // Create a "normal" continuation block
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "invoke.cont", fn);
    auto* inv = builder->CreateInvoke(fty, callee, contBB, unwindTarget, args, name);
    builder->SetInsertPoint(contBB);
    return inv;
}

void CodeGen::visit(ThrowStmt* node) {
    ensureEHDecls(module.get(), *context);
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Type* i64   = llvm::Type::getInt64Ty(*context);

    // EskiuEx: { i64 value, ptr type_name } — 16 bytes
    llvm::Function* allocEx = getOrDeclareFunc("__cxa_allocate_exception",
        ptrTy, {i64});
    llvm::Value* exPtr = builder->CreateCall(allocEx,
        {llvm::ConstantInt::get(i64, 16)}, "ex.alloc");

    // Store value as i64
    llvm::Value* val = evaluateExpr(node->value);
    llvm::Value* ival;
    if (val->getType()->isPointerTy())
        ival = builder->CreatePtrToInt(val, i64);
    else
        ival = builder->CreateSExtOrTrunc(val, i64);
    builder->CreateStore(ival, exPtr);

    // Store type name at offset 8
    std::string thrownType = node->valueType.empty() ? "unknown" : node->valueType;
    auto* typeStr = builder->CreateGlobalString(thrownType, ".ex.tname");
    auto* typeSlot = builder->CreateConstGEP1_64(
        llvm::Type::getInt8Ty(*context), exPtr, 8, "ex.type.slot");
    auto* typeSlotPtr = builder->CreateBitCast(typeSlot, ptrTy, "ex.type.ptr");
    builder->CreateStore(typeStr, typeSlotPtr);

    // __cxa_throw(ex, _ZTIPv, null)
    // Must be an invoke when inside a try body so the local landingpad fires.
    llvm::Function* cxaThrow = getOrDeclareFunc("__cxa_throw",
        llvm::Type::getVoidTy(*context), {ptrTy, ptrTy, ptrTy});
    // Do NOT mark noreturn — it prevents invoke from propagating the exception
    llvm::Value* typeInfo = module->getNamedGlobal("_ZTIPv");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrTy));
    std::vector<llvm::Value*> throwArgs = {exPtr, typeInfo, nullPtr};

    if (unwindTarget) {
        // Inside a try — use invoke so the landingpad catches it
        llvm::Function* fn = builder->GetInsertBlock()->getParent();
        auto* unreachBB = llvm::BasicBlock::Create(*context, "throw.unreach", fn);
        builder->CreateInvoke(cxaThrow->getFunctionType(), cxaThrow,
            unreachBB, unwindTarget, throwArgs);
        builder->SetInsertPoint(unreachBB);
    } else {
        builder->CreateCall(cxaThrow, throwArgs);
    }
    builder->CreateUnreachable();
}

void CodeGen::visit(TryStmt* node) {
    ensureEHDecls(module.get(), *context);
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Type* i64   = llvm::Type::getInt64Ty(*context);
    llvm::Type* i32   = llvm::Type::getInt32Ty(*context);

    llvm::Function* fn = builder->GetInsertBlock()->getParent();

    // Set personality on the enclosing function if not already set
    if (!fn->hasPersonalityFn()) {
        auto* pers = module->getFunction("__gxx_personality_v0");
        fn->setPersonalityFn(pers);
    }

    llvm::BasicBlock* lpadBB    = llvm::BasicBlock::Create(*context, "try.lpad",    fn);
    llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "try.finally", fn);
    llvm::BasicBlock* doneBB    = llvm::BasicBlock::Create(*context, "try.done",    fn);

    // ── try body — all calls become invokes ───────────────────────────────
    llvm::BasicBlock* savedUnwind = unwindTarget;
    unwindTarget = lpadBB;
    // Register `finally` as a cleanup for the body's duration, so an early exit
    // (return/break/continue/`?`) from inside the body runs it — the normal
    // fall-through and exception paths still emit it via finallyBB / the landingpad
    // below, so we pop this frame WITHOUT running it here.
    cleanupScopes.emplace_back();
    if (node->finally) cleanupScopes.back().push_back({node->finally.get(), /*isErr=*/false});
    if (node->body) node->body->accept(this);
    cleanupScopes.pop_back();
    unwindTarget = savedUnwind;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(finallyBB);

    // ── landingpad ────────────────────────────────────────────────────────
    builder->SetInsertPoint(lpadBB);
    auto* lp = builder->CreateLandingPad(
        llvm::StructType::get(*context, {ptrTy, i32}), 1, "lpad");
    // catch i8* null = catch-all
    lp->addClause(llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrTy)));

    llvm::Value* exObjPtr = builder->CreateExtractValue(lp, {0}, "ex.ptr");

    // __cxa_begin_catch(ex) → pointer to our EskiuEx
    llvm::Function* beginCatch = getOrDeclareFunc("__cxa_begin_catch",
        ptrTy, {ptrTy});
    llvm::Value* exData = builder->CreateCall(beginCatch, {exObjPtr}, "ex.data");

    // Read the type name (offset 8)
    auto* typeSlot = builder->CreateConstGEP1_64(
        llvm::Type::getInt8Ty(*context), exData, 8, "ex.tslot");
    llvm::Value* exType = builder->CreateLoad(ptrTy,
        builder->CreateBitCast(typeSlot, ptrTy), "ex.type");

    // ── catch clauses ─────────────────────────────────────────────────────
    llvm::Function* endCatch  = getOrDeclareFunc("__cxa_end_catch",
        llvm::Type::getVoidTy(*context), {});
    llvm::Function* strcmpFn  = getOrDeclareFunc("strcmp", i32, {ptrTy, ptrTy});

    for (auto& c : node->catches) {
        auto* cTypeStr  = builder->CreateGlobalString(c.type, ".catch.t");
        llvm::Value* cmp   = builder->CreateCall(strcmpFn, {exType, cTypeStr}, "tcmp");
        llvm::Value* match = builder->CreateICmpEQ(cmp,
            llvm::ConstantInt::get(i32, 0), "tmatch");

        auto* handlerBB = llvm::BasicBlock::Create(*context, "catch." + c.type, fn);
        auto* nextBB    = llvm::BasicBlock::Create(*context, "catch.next",       fn);
        builder->CreateCondBr(match, handlerBB, nextBB);

        builder->SetInsertPoint(handlerBB);
        pushScope();

        // Load value (offset 0)
        llvm::Value* ival = builder->CreateLoad(i64, exData, "ex.ival");
        llvm::Type*  catchTy = getTypeFromString(c.type);
        llvm::Value* catchVal = catchTy->isPointerTy()
            ? builder->CreateIntToPtr(ival, catchTy)
            : builder->CreateTrunc(ival, catchTy);
        auto* catchAlloca = entryAlloca(catchTy, nullptr, c.name);
        builder->CreateStore(catchVal, catchAlloca);
        defineSymbol(c.name, catchAlloca);
        defineVarType(c.name, c.type);

        if (c.body) c.body->accept(this);
        popScope();

        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateCall(endCatch, {});
            builder->CreateBr(finallyBB);
        }

        builder->SetInsertPoint(nextBB);
    }

    // No catch clause matched (or there were none, e.g. a catch-less try/finally):
    // run the finally body on this exceptional path too, then re-raise the in-flight
    // exception with __cxa_rethrow. (end_catch + resume here double-freed the
    // exception and aborted; and the finally was skipped entirely.) The rethrow is an
    // invoke when an enclosing try can catch it, so its landingpad fires.
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (node->finally) node->finally->accept(this);
        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::Function* rethrow = getOrDeclareFunc("__cxa_rethrow",
                llvm::Type::getVoidTy(*context), {});
            if (savedUnwind) {
                auto* rethrowUnreach = llvm::BasicBlock::Create(*context, "rethrow.unreach", fn);
                builder->CreateInvoke(rethrow->getFunctionType(), rethrow,
                                      rethrowUnreach, savedUnwind, {});
                builder->SetInsertPoint(rethrowUnreach);
            } else {
                builder->CreateCall(rethrow, {});
            }
            builder->CreateUnreachable();
        }
    }

    // ── finally (normal, non-exceptional path) ─────────────────────────────
    builder->SetInsertPoint(finallyBB);
    if (node->finally) node->finally->accept(this);
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(doneBB);

    builder->SetInsertPoint(doneBB);
}

void CodeGen::visit(AwaitExpr* node) {
    // The async transform rewrites `async fn`/`await` into a state machine before
    // codegen; reaching here means the transform has not run on this code.
    (void)node;
    throw std::runtime_error("internal error: an `await` survived to codegen — "
                             "the async state-machine transform did not run");
}

void CodeGen::visit(FreeClosureExpr* node) {
    // A closure is a fat pointer {fn_ptr, env_ptr}. Free its heap env (slot 1).
    // A non-capturing closure has a null env; free(null) is a safe no-op.
    llvm::Value* fat = evaluateExpr(node->closure);
    llvm::Value* env = builder->CreateExtractValue(fat, 1, "clos.env");
    llvm::Function* freeFn = getOrDeclareFunc(
        "free", llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0)}, false);
    builder->CreateCall(freeFn, {env});
    exprValueStack.push(llvm::UndefValue::get(llvm::Type::getVoidTy(*context)));
}

void CodeGen::visit(ThreadCreateExpr* node) {
    // Evaluate the closure — a fat pointer {fn_ptr, env_ptr}
    llvm::Value* fatPtr = evaluateExpr(node->worker);

    // Extract fn_ptr and env_ptr
    llvm::Value* fnPtr  = builder->CreateExtractValue(fatPtr, {0}, "thr.fn");
    llvm::Value* envPtr = builder->CreateExtractValue(fatPtr, {1}, "thr.env");

    // pthread_t is typically *void; alloca space for the tid
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Value* tidAlloca = entryAlloca(ptrTy, nullptr, "thr.tid");

    // pthread_create(pthread_t* tid, null, fn_ptr, env_ptr)
    llvm::Function* pthreadCreate = getOrDeclareFunc("pthread_create",
        llvm::Type::getInt32Ty(*context),
        {ptrTy, ptrTy, ptrTy, ptrTy});

    builder->CreateCall(pthreadCreate, {
        tidAlloca,
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
        fnPtr,
        envPtr
    });

    // Return the thread handle (tid value)
    exprValueStack.push(builder->CreateLoad(ptrTy, tidAlloca, "thr.handle"));
}

void CodeGen::visit(ThreadJoinStmt* node) {
    llvm::Value* tid = evaluateExpr(node->tid);
    llvm::Type*  ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Function* pthreadJoin = getOrDeclareFunc("pthread_join",
        llvm::Type::getInt32Ty(*context), {ptrTy, ptrTy});
    builder->CreateCall(pthreadJoin, {
        tid,
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy))
    });
}

void CodeGen::visit(AsmStmt* node) {
    // Build LLVM inline asm from GCC-style extended asm syntax
    std::vector<llvm::Value*> argVals;
    std::string constraints;

    for (auto& [constraint, expr] : node->inputs) {
        argVals.push_back(evaluateExpr(expr));
        if (!constraints.empty()) constraints += ",";
        constraints += constraint;
    }
    for (const auto& clob : node->clobbers) {
        if (!constraints.empty()) constraints += ",";
        constraints += "~{" + clob + "}";
    }
    // sideeffect + alignstack are standard for kernel inline asm
    if (!constraints.empty()) constraints += ",~{dirflag},~{fpsr},~{flags}";

    std::vector<llvm::Type*> argTypes;
    for (auto* v : argVals) argTypes.push_back(v->getType());

    auto* fty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context), argTypes, false);
    auto* iasm = llvm::InlineAsm::get(
        fty, node->asmString, constraints,
        /*hasSideEffects=*/true, /*isAlignStack=*/false,
        llvm::InlineAsm::AD_ATT);

    builder->CreateCall(iasm, argVals);
}
