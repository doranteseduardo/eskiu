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

bool CodeGen::blockTerminated() {
    return builder->GetInsertBlock() && builder->GetInsertBlock()->getTerminator();
}

void CodeGen::runCleanupsToDepth(size_t depth) {
    // Emit each pending cleanup body, innermost frame first and LIFO within a frame.
    for (size_t i = cleanupScopes.size(); i-- > depth; ) {
        auto& frame = cleanupScopes[i];
        for (size_t j = frame.size(); j-- > 0; ) {
            if (blockTerminated()) return;
            frame[j]->accept(this);
        }
    }
}

void CodeGen::visit(BlockStmt* node) {
    cleanupScopes.emplace_back();                    // this block's cleanup frame
    for (auto& item : node->items) {
        // Once this block has a terminator (a return/break/continue/throw was
        // emitted), the rest is unreachable. Emitting into a terminated block
        // yields invalid IR ("terminator in the middle of a basic block"), so
        // stop here — the dead code is simply dropped. (An early exit already ran
        // this frame's cleanups via runCleanupsToDepth before terminating.)
        if (blockTerminated())
            break;
        if (std::holds_alternative<DeclPtr>(item)) {
            auto decl = std::get<DeclPtr>(item);
            decl->accept(this);
        } else if (std::holds_alternative<StmtPtr>(item)) {
            auto stmt = std::get<StmtPtr>(item);
            stmt->accept(this);
        }
    }
    // Normal fall-through: run this block's deferred cleanups (LIFO).
    if (!blockTerminated())
        runCleanupsToDepth(cleanupScopes.size() - 1);
    cleanupScopes.pop_back();
}

void CodeGen::visit(DeferStmt* node) {
    // Register the body to run at scope exit; emitted by runCleanupsToDepth.
    if (node->body && !cleanupScopes.empty())
        cleanupScopes.back().push_back(node->body.get());
}

void CodeGen::visit(IfStmt* node) {
    // Evaluate condition
    llvm::Value* cond = evaluateExpr(node->condition);

    if (!cond) {
        throw std::runtime_error("If condition evaluation failed");
    }

    // Convert to i1
    if (!cond->getType()->isIntegerTy(1)) {
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }

    // Create blocks
    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(*context, "then", currentFunction);
    llvm::BasicBlock* elseBlock = nullptr;
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "merge", currentFunction);

    if (node->elseBranch) {
        elseBlock = llvm::BasicBlock::Create(*context, "else", currentFunction);
        builder->CreateCondBr(cond, thenBlock, elseBlock);
    } else {
        builder->CreateCondBr(cond, thenBlock, mergeBlock);
    }

    // Then block
    builder->SetInsertPoint(thenBlock);
    node->thenBranch->accept(this);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBlock);
    }

    // Else block
    if (node->elseBranch) {
        builder->SetInsertPoint(elseBlock);
        node->elseBranch->accept(this);
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBlock);
        }
    }

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void CodeGen::visit(WhileStmt* node) {
    llvm::BasicBlock* loopBlock = llvm::BasicBlock::Create(*context, "while", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "while_body", currentFunction);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(*context, "while_exit", currentFunction);

    builder->CreateBr(loopBlock);

    builder->SetInsertPoint(loopBlock);
    llvm::Value* cond = evaluateExpr(node->condition);
    if (!cond->getType()->isIntegerTy(1)) {
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }
    builder->CreateCondBr(cond, bodyBlock, exitBlock);

    builder->SetInsertPoint(bodyBlock);
    llvm::BasicBlock* prevBreak    = breakTarget;
    llvm::BasicBlock* prevContinue = continueTarget;
    size_t prevBreakCD = breakCleanupDepth, prevContinueCD = continueCleanupDepth;
    breakTarget    = exitBlock;
    continueTarget = loopBlock;
    breakCleanupDepth = continueCleanupDepth = cleanupScopes.size();
    node->body->accept(this);
    breakTarget    = prevBreak;
    continueTarget = prevContinue;
    breakCleanupDepth = prevBreakCD; continueCleanupDepth = prevContinueCD;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(loopBlock);

    builder->SetInsertPoint(exitBlock);
}

void CodeGen::visit(DoWhileStmt* node) {
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "do_body", currentFunction);
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "do_cond", currentFunction);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(*context, "do_exit", currentFunction);

    builder->CreateBr(bodyBlock);
    builder->SetInsertPoint(bodyBlock);

    llvm::BasicBlock* prevBreak    = breakTarget;
    llvm::BasicBlock* prevContinue = continueTarget;
    size_t prevBreakCD = breakCleanupDepth, prevContinueCD = continueCleanupDepth;
    breakTarget    = exitBlock;
    continueTarget = condBlock;   // `continue` re-tests the condition
    breakCleanupDepth = continueCleanupDepth = cleanupScopes.size();
    node->body->accept(this);
    breakTarget    = prevBreak;
    continueTarget = prevContinue;
    breakCleanupDepth = prevBreakCD; continueCleanupDepth = prevContinueCD;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(condBlock);

    builder->SetInsertPoint(condBlock);
    llvm::Value* cond = evaluateExpr(node->condition);
    if (!cond->getType()->isIntegerTy(1))
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    builder->CreateCondBr(cond, bodyBlock, exitBlock);

    builder->SetInsertPoint(exitBlock);
}

void CodeGen::visit(ForStmt* node) {
    llvm::BasicBlock* loopBlock = llvm::BasicBlock::Create(*context, "for", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "for_body", currentFunction);
    llvm::BasicBlock* stepBlock = llvm::BasicBlock::Create(*context, "for_step", currentFunction);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(*context, "for_exit", currentFunction);

    // Init
    if (node->init) {
        node->init->accept(this);
    }
    builder->CreateBr(loopBlock);

    // Condition
    builder->SetInsertPoint(loopBlock);
    if (node->condition) {
        llvm::Value* cond = evaluateExpr(node->condition);
        if (!cond->getType()->isIntegerTy(1)) {
            cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
        }
        builder->CreateCondBr(cond, bodyBlock, exitBlock);
    } else {
        builder->CreateBr(bodyBlock);
    }

    // Body
    builder->SetInsertPoint(bodyBlock);
    llvm::BasicBlock* prevBreak    = breakTarget;
    llvm::BasicBlock* prevContinue = continueTarget;
    size_t prevBreakCD = breakCleanupDepth, prevContinueCD = continueCleanupDepth;
    breakTarget    = exitBlock;
    continueTarget = stepBlock;   // continue jumps to the step
    breakCleanupDepth = continueCleanupDepth = cleanupScopes.size();
    node->body->accept(this);
    breakTarget    = prevBreak;
    continueTarget = prevContinue;
    breakCleanupDepth = prevBreakCD; continueCleanupDepth = prevContinueCD;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(stepBlock);

    // Step
    builder->SetInsertPoint(stepBlock);
    if (node->step) {
        evaluateExpr(node->step);
    }
    builder->CreateBr(loopBlock);

    // Exit
    builder->SetInsertPoint(exitBlock);
}

void CodeGen::visit(ForInStmt* node) {
    // Desugar `for (x in it)` into a counted for-loop. We lower to a real
    // ForStmt (not a while) so `continue` lands on the index increment.
    static int counter = 0;
    std::string idxName = "__forin_i_" + std::to_string(counter++);
    auto idx = [&]() -> ExprPtr { return std::make_shared<IdentExpr>(idxName); };
    auto intLit = [&](const std::string& v) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, v);
    };

    std::string itType = getExprEskiuType(node->iterable);
    std::string elemType;
    ExprPtr lengthExpr, elemExpr;

    ty::Type itT = ty::Type::parse(itType);
    if (itT.kind == ty::Type::Kind::Array) {
        // Fixed-size array T[N] — resolve the outer dimension N (literal, enum, const int).
        elemType   = itT.elem->str();
        uint64_t len = 0;
        resolveArrayDim(itT.dim, len);
        lengthExpr = intLit(std::to_string(len));
        elemExpr   = std::make_shared<IndexExpr>(node->iterable, idx());
    } else {
        // List-like struct: needs `data` (pointer) and `size` (int) fields.
        // Strip the struct:/pointer decoration to the bare registry key (the
        // resolved type arrives normalized as e.g. "struct:List_int").
        std::string s = itType;
        if (s.rfind("struct:", 0) == 0) s = s.substr(7);
        while (!s.empty() && s.front() == '*') s = s.substr(1);
        while (!s.empty() && s.back()  == '*') s.pop_back();
        auto it = structFields.find(s);
        std::string dataType;
        bool hasSize = false;
        if (it != structFields.end())
            for (const auto& f : it->second) {
                if (f.name == "data") dataType = f.type;
                if (f.name == "size") hasSize = true;
            }
        if (dataType.empty() || !hasSize)
            throw std::runtime_error("for-in over unsupported type: " + itType);
        while (!dataType.empty() && dataType.front() == '*') dataType = dataType.substr(1);
        while (!dataType.empty() && dataType.back()  == '*') dataType.pop_back();
        elemType   = dataType;
        lengthExpr = std::make_shared<MemberExpr>(node->iterable, "size");
        elemExpr   = std::make_shared<IndexExpr>(
            std::make_shared<MemberExpr>(node->iterable, "data"), idx());
    }

    auto idxDecl = std::make_shared<VarDecl>(idxName, "int", intLit("0"));
    StmtPtr init = std::make_shared<BlockStmt>(std::vector<BlockItem>{DeclPtr(idxDecl)});
    ExprPtr cond = std::make_shared<BinaryExpr>(idx(), "<", lengthExpr);
    ExprPtr step = std::make_shared<BinaryExpr>(idx(), "=",
                       std::make_shared<BinaryExpr>(idx(), "+", intLit("1")));

    auto elemDecl = std::make_shared<VarDecl>(node->varName, elemType, elemExpr);
    StmtPtr body = std::make_shared<BlockStmt>(std::vector<BlockItem>{
        DeclPtr(elemDecl), StmtPtr(node->body)});

    ForStmt loop(init, cond, step, body);
    visit(&loop);
}

void CodeGen::visit(ReturnStmt* node) {
    // Coerce return value to the declared function return type
    auto coerceRetVal = [&](llvm::Value* v) -> llvm::Value* {
        if (!currentFunction) return v;
        llvm::Type* ft = currentFunction->getReturnType();
        if (v->getType() == ft) return v;
        if (v->getType()->isIntegerTy() && ft->isIntegerTy()) {
            unsigned vw = llvm::cast<llvm::IntegerType>(v->getType())->getBitWidth();
            unsigned fw = llvm::cast<llvm::IntegerType>(ft)->getBitWidth();
            if (vw >= fw) return builder->CreateTrunc(v, ft);
            // Widen by the source's signedness: a bool/comparison result (i1) or
            // an unsigned source zero-extends — e.g. `return a < b;` from an int
            // function is 1, not -1.
            std::string st = node->value ? expandAlias(getExprEskiuType(node->value)) : "";
            bool uns = vw == 1 || st == "uint" || st == "uint8" || st == "uint16" ||
                       st == "uint32" || st == "uint64" || st == "char" || st == "bool";
            return uns ? builder->CreateZExt(v, ft) : builder->CreateSExt(v, ft);
        }
        if (v->getType()->isIntegerTy() && ft->isFloatingPointTy())
            return intToFloat(v, ft, node->value && eskiuUnsigned(getExprEskiuType(node->value)));
        if (v->getType()->isFloatingPointTy() && ft->isIntegerTy())
            return builder->CreateFPToSI(v, ft);
        if (v->getType()->isFloatingPointTy() && ft->isFloatingPointTy())
            return builder->CreateFPCast(v, ft);  // double→float or float→double
        return v;
    };

    if (currentSretParam != nullptr) {
        // sret function: store result to hidden pointer, return void
        if (node->value) {
            llvm::Value* retValue = evaluateExpr(node->value);
            builder->CreateStore(retValue, currentSretParam);
        }
        runCleanupsToDepth(0);          // run pending defers/finally before leaving
        builder->CreateRetVoid();
    } else if (node->value) {
        // Evaluate the return value first, THEN run cleanups (C defer order), then ret.
        llvm::Value* retValue = coerceRetVal(evaluateExpr(node->value));
        runCleanupsToDepth(0);
        builder->CreateRet(retValue);
    } else {
        runCleanupsToDepth(0);
        builder->CreateRetVoid();
    }
}

void CodeGen::visit(BreakStmt* node) {
    if (!breakTarget)
        throw std::runtime_error("break used outside of a loop");
    runCleanupsToDepth(breakCleanupDepth);   // defers inside the loop body run
    builder->CreateBr(breakTarget);
}

void CodeGen::visit(ExprStmt* node) {
    evaluateExpr(node->expr);
}

void CodeGen::visit(ContinueStmt* node) {
    if (!continueTarget)
        throw std::runtime_error("continue used outside of a loop");
    runCleanupsToDepth(continueCleanupDepth);
    builder->CreateBr(continueTarget);
}

void CodeGen::visit(MatchStmt* node) {
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    // The type checker stamps node->enumName, except inside template-function
    // bodies (which it skips) — there we derive it from the subject's type, with
    // the active typeParamOverride applied, and ensure the instance is built.
    std::string enumName = node->enumName;
    if (enumName.empty() || !structTypes.count(enumName)) {
        std::string st = getExprEskiuType(node->subject);
        // getExprEskiuType's deref only strips a leading '*'; for a trailing-star
        // pointer (`Option<int>*`) it returns "" — fall back to the operand's type.
        if (st.empty())
            if (auto* u = dynamic_cast<UnaryExpr*>(node->subject.get()); u && u->op == "*")
                st = getExprEskiuType(u->operand);
        if (!typeParamOverride.empty()) st = substType(st, typeParamOverride);  // T -> int
        while (!st.empty() && st.front() == '*') st = st.substr(1);
        while (!st.empty() && st.back()  == '*') st.pop_back();
        if (st.rfind("struct:", 0) == 0) st = st.substr(7);
        if (st.find('<') != std::string::npos) {
            auto [b, a] = splitTemplateType(st);
            enumName = genericEnumDecls.count(b) ? ensureEnumInst(b, a) : mangleTemplate(st);
        } else if (!st.empty()) {
            enumName = st;
        }
    }
    // Resolve the enum decl + (for a generic instance) the type-arg substitutions,
    // so each variant's payload field types come out concrete.
    EnumDecl* ed = nullptr;
    std::map<std::string, std::string> subs;
    if (adtEnumDecls.count(enumName)) {
        ed = adtEnumDecls[enumName];                         // concrete ADT enum
    } else if (enumInstanceArgs.count(enumName)) {           // generic instance
        auto& inst = enumInstanceArgs[enumName];
        ed = genericEnumDecls[inst.first];
        for (size_t i = 0; i < ed->typeParams.size() && i < inst.second.size(); ++i)
            subs[ed->typeParams[i]] = inst.second[i];
    }
    if (!ed)
        throw std::runtime_error("match: could not resolve the algebraic enum for subject "
                                 "type '" + enumName + "' (the subject must be an enum value)");
    auto variantIndex = [&](const std::string& v) -> int {
        for (size_t i = 0; i < ed->members.size(); ++i)
            if (ed->members[i].first == v) return (int)i;
        return -1;
    };
    // Concrete payload LLVM field types of a variant (after substitution).
    auto payloadTypes = [&](int idx) {
        std::vector<llvm::Type*> v;
        for (const auto& ft : ed->payloads[idx]) v.push_back(getTypeFromString(substType(ft, subs)));
        return v;
    };

    llvm::StructType* et = structTypes[enumName];

    // Materialize the subject so tag + payload can be read by pointer.
    llvm::Value* sv = evaluateExpr(node->subject);
    llvm::Value* sptr = entryAlloca(et, nullptr, "match.subj");
    builder->CreateStore(sv, sptr);
    llvm::Value* tag = builder->CreateLoad(i32, builder->CreateStructGEP(et, sptr, 0), "match.tag");

    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "match.end", currentFunction);
    std::vector<llvm::BasicBlock*> armBlocks(node->arms.size());
    llvm::BasicBlock* defaultBlock = endBlock;
    for (size_t i = 0; i < node->arms.size(); ++i) {
        armBlocks[i] = llvm::BasicBlock::Create(*context, "match.arm", currentFunction);
        if (node->arms[i].variant.empty()) defaultBlock = armBlocks[i];
    }
    llvm::SwitchInst* sw = builder->CreateSwitch(tag, defaultBlock);
    for (size_t i = 0; i < node->arms.size(); ++i)
        if (!node->arms[i].variant.empty())
            sw->addCase(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(i32, variantIndex(node->arms[i].variant))), armBlocks[i]);

    for (size_t i = 0; i < node->arms.size(); ++i) {
        auto& arm = node->arms[i];
        builder->SetInsertPoint(armBlocks[i]);
        pushScope();
        if (!arm.variant.empty() && !arm.bindings.empty()) {
            int vi = variantIndex(arm.variant);
            std::vector<llvm::Type*> vfields = payloadTypes(vi);
            llvm::StructType* vt = llvm::StructType::get(*context, vfields);
            llvm::Value* pay = builder->CreateStructGEP(et, sptr, 1);
            for (size_t b = 0; b < arm.bindings.size() && b < vfields.size(); ++b) {
                llvm::Value* fp = builder->CreateStructGEP(vt, pay, b);
                llvm::Value* val = builder->CreateLoad(vfields[b], fp, arm.bindings[b]);
                llvm::Value* slot = entryAlloca(vfields[b], nullptr, arm.bindings[b]);
                builder->CreateStore(val, slot);
                defineSymbol(arm.bindings[b], slot);
                defineVarType(arm.bindings[b], substType(ed->payloads[vi][b], subs));
            }
        }
        if (arm.body) arm.body->accept(this);
        popScope();
        if (!builder->GetInsertBlock()->getTerminator())
            builder->CreateBr(endBlock);
    }
    builder->SetInsertPoint(endBlock);
}

void CodeGen::visit(SwitchStmt* node) {
    // Pre-evaluate case values (must be ConstantInt) before creating the switch
    std::vector<llvm::ConstantInt*> caseVals;
    for (auto& c : node->cases) {
        if (!c.value) { caseVals.push_back(nullptr); continue; }
        llvm::Value* v = evaluateExpr(c.value);
        auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v);
        if (!ci) throw std::runtime_error("switch case value must be a constant integer");
        caseVals.push_back(ci);
    }

    llvm::Value* subj = evaluateExpr(node->subject);
    if (!subj->getType()->isIntegerTy())
        throw std::runtime_error("switch subject must be integer");

    // LLVM requires the switch value and every case constant to share one integer
    // type. Widen to the widest of the subject and the cases (C promotes the
    // controlling expression; the cases may be wider, e.g. an int64 switch), so a
    // `switch (aChar)` (i8 subject, i32 case constants) is well-formed.
    unsigned swW = subj->getType()->getIntegerBitWidth();
    for (auto* ci : caseVals) if (ci) swW = std::max(swW, ci->getType()->getIntegerBitWidth());
    llvm::Type* swTy = llvm::Type::getIntNTy(*context, swW);
    if (subj->getType() != swTy)
        subj = coerceInt(subj, swTy, eskiuUnsigned(getExprEskiuType(node->subject)));
    for (auto& ci : caseVals)
        if (ci && ci->getType() != swTy)
            ci = llvm::ConstantInt::get(*context, ci->getValue().sextOrTrunc(swW));

    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "switch.end", currentFunction);

    std::vector<llvm::BasicBlock*> caseBlocks(node->cases.size());
    for (size_t i = 0; i < node->cases.size(); ++i) {
        std::string lbl = node->cases[i].value ? "case" + std::to_string(i) : "default";
        caseBlocks[i] = llvm::BasicBlock::Create(*context, lbl, currentFunction);
    }

    llvm::BasicBlock* defaultBlock = endBlock;
    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (!node->cases[i].value) { defaultBlock = caseBlocks[i]; break; }
    }

    llvm::SwitchInst* sw = builder->CreateSwitch(subj, defaultBlock);
    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (caseVals[i]) sw->addCase(caseVals[i], caseBlocks[i]);
    }

    llvm::BasicBlock* prevBreak = breakTarget;
    breakTarget = endBlock;

    for (size_t i = 0; i < node->cases.size(); ++i) {
        builder->SetInsertPoint(caseBlocks[i]);
        for (auto& stmt : node->cases[i].stmts) {
            stmt->accept(this);
            if (builder->GetInsertBlock()->getTerminator()) break;
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::BasicBlock* next = (i + 1 < caseBlocks.size()) ? caseBlocks[i+1] : endBlock;
            builder->CreateBr(next);
        }
    }

    breakTarget = prevBreak;
    builder->SetInsertPoint(endBlock);
}
