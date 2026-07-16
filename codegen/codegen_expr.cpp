#include "codegen.h"
#include "../ast/type_qual.h"

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

void CodeGen::visit(BinaryExpr* node) {
    // Assignment: evaluate left as lvalue (pointer), not rvalue
    if (node->op == "=") {
        // Bitfield assignment is a read-modify-write, not a plain store.
        if (auto* mem = dynamic_cast<MemberExpr*>(node->left.get())) {
            auto lit = structLayout.find(structBaseTypeOf(mem->base));
            if (lit != structLayout.end()) {
                auto sit = lit->second.find(mem->member);
                if (sit != lit->second.end() && sit->second.isBitfield) {
                    llvm::Value* rhs = evaluateExpr(node->right);
                    storeBitfield(mem, rhs);
                    exprValueStack.push(rhs);
                    return;
                }
            }
        }
        llvm::Value* lhs = evaluateLValue(node->left);
        llvm::Value* rhs = evaluateExpr(node->right);
        // Coerce RHS to match the lvalue's expected element type.
        // Prefer the LHS's declared (static) scalar type: a union member lvalue
        // collapses to the union's base pointer (all fields at offset 0), so the
        // alloca/GEP type encodes the union storage, not the selected field — and
        // a double would be stored whole into a float field without truncation.
        llvm::Type* elemType = nullptr;
        std::string lhsEskiu = getExprEskiuType(node->left);
        if (!lhsEskiu.empty()) {
            llvm::Type* st = getTypeFromString(lhsEskiu);
            if (st && (st->isFloatingPointTy() || st->isIntegerTy()))
                elemType = st;
        }
        if (!elemType) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(lhs))
                elemType = alloca->getAllocatedType();
            else if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(lhs))
                elemType = gep->getResultElementType();
        }
        if (elemType && rhs->getType() != elemType) {
            if (rhs->getType()->isIntegerTy() && elemType->isIntegerTy()) {
                rhs = coerceInt(rhs, elemType, eskiuUnsigned(getExprEskiuType(node->right)));
            } else if (rhs->getType()->isIntegerTy() && elemType->isFloatingPointTy()) {
                rhs = intToFloat(rhs, elemType, eskiuUnsigned(getExprEskiuType(node->right)));
            } else if (rhs->getType()->isFloatingPointTy() && elemType->isIntegerTy()) {
                rhs = builder->CreateFPToSI(rhs, elemType);
            } else if (rhs->getType()->isFloatingPointTy() && elemType->isFloatingPointTy()) {
                rhs = builder->CreateFPCast(rhs, elemType);  // e.g. double→float
            }
        }
        bool storeVol = false;
        if (auto* ident = llvm::dyn_cast<llvm::AllocaInst>(lhs)) {
            storeVol = volatileVars.count(ident->getName().str()) > 0;
        }
        auto* si = builder->CreateStore(rhs, lhs);
        si->setVolatile(storeVol);
        exprValueStack.push(rhs);
        return;
    }

    // Short-circuit logical operators: the RHS must be evaluated ONLY when the LHS
    // doesn't already decide the result, so a guarded expression like
    // `p != null && p.field` (or any RHS unsafe when the LHS is false/true) is not
    // executed. Evaluating both operands eagerly — as the plain path below does —
    // was a correctness bug.
    if (node->op == "&&" || node->op == "||") {
        llvm::Value* l = evaluateExpr(node->left);
        if (!l->getType()->isIntegerTy(1))
            l = builder->CreateICmpNE(l, llvm::ConstantInt::get(l->getType(), 0));
        llvm::BasicBlock* startBB = builder->GetInsertBlock();
        llvm::BasicBlock* rhsBB  = llvm::BasicBlock::Create(*context, "sc.rhs", currentFunction);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "sc.cont", currentFunction);
        if (node->op == "&&")
            builder->CreateCondBr(l, rhsBB, contBB);   // l true → eval RHS; false → result false
        else
            builder->CreateCondBr(l, contBB, rhsBB);   // l true → result true; false → eval RHS
        builder->SetInsertPoint(rhsBB);
        llvm::Value* r = evaluateExpr(node->right);
        if (!r->getType()->isIntegerTy(1))
            r = builder->CreateICmpNE(r, llvm::ConstantInt::get(r->getType(), 0));
        llvm::BasicBlock* rhsEndBB = builder->GetInsertBlock();   // RHS may have added blocks
        builder->CreateBr(contBB);
        builder->SetInsertPoint(contBB);
        llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2);
        phi->addIncoming(builder->getInt1(node->op == "||"), startBB);  // short-circuit value
        phi->addIncoming(r, rhsEndBB);
        exprValueStack.push(phi);
        return;
    }

    llvm::Value* left = evaluateExpr(node->left);
    llvm::Value* right = evaluateExpr(node->right);

    if (!left || !right) {
        throw std::runtime_error("Binary expression operand evaluation failed");
    }

    llvm::Value* result = nullptr;

    // Integer signedness of each operand, from its Eskiu type. Drives sign- vs
    // zero-extension when widening, and signed vs unsigned div/rem/shr/compare.
    auto isUnsignedEsk = [&](const ExprPtr& e) -> bool {
        return eskiuUnsigned(getExprEskiuType(e));
    };
    bool lUns = isUnsignedEsk(node->left);
    bool rUns = isUnsignedEsk(node->right);
    bool opUnsigned = lUns || rUns;   // C-style: unsigned wins in a mixed op
    auto extTo = [&](llvm::Value* v, llvm::Type* ty, bool uns) {
        return uns ? builder->CreateZExt(v, ty) : builder->CreateSExt(v, ty);
    };

    // Promote to common type: int→float, float→double
    auto promoteToFloat = [&]() {
        if (left->getType()->isFloatingPointTy() && right->getType()->isIntegerTy())
            right = intToFloat(right, left->getType(), rUns);
        else if (right->getType()->isFloatingPointTy() && left->getType()->isIntegerTy())
            left = intToFloat(left, right->getType(), lUns);
        // float × double: widen float → double
        else if (left->getType()->isFloatingPointTy() && right->getType()->isFloatingPointTy()
                 && left->getType() != right->getType()) {
            if (left->getType()->getPrimitiveSizeInBits() <
                right->getType()->getPrimitiveSizeInBits())
                left  = builder->CreateFPCast(left,  right->getType());
            else
                right = builder->CreateFPCast(right, left->getType());
        }
    };

    // Widen the narrower integer to match the wider one, extending each operand
    // according to ITS OWN signedness (sign-extend signed, zero-extend unsigned).
    auto widenInts = [&]() {
        if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                && left->getType() != right->getType()) {
            unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
            unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
            if (lw < rw) left  = extTo(left,  right->getType(), lUns);
            else          right = extTo(right, left->getType(),  rUns);
        }
    };
    auto widenForBitwise = widenInts;
    auto widenForArith   = widenInts;

    // Resolve the element type for typed pointer arithmetic.
    // *int → i32, *uint8 → i8, *void/*char/unknown → i8 (byte arithmetic)
    auto ptrElemType = [&]() -> llvm::Type* {
        std::string eskTy = getExprEskiuType(node->left);
        if (eskTy.empty()) return llvm::Type::getInt8Ty(*context);
        // Strip leading *
        if (!eskTy.empty() && eskTy.front() == '*') eskTy = eskTy.substr(1);
        // Strip trailing *
        if (!eskTy.empty() && eskTy.back()  == '*') eskTy.pop_back();
        if (eskTy == "void" || eskTy == "char" || eskTy.empty())
            return llvm::Type::getInt8Ty(*context);
        return getTypeFromString(eskTy);
    };

    if (node->op == "+") {
        if (left->getType()->isPointerTy()) {
            llvm::Value* idx = builder->CreateSExtOrTrunc(
                right, llvm::Type::getInt64Ty(*context), "ptr.idx");
            result = builder->CreateGEP(ptrElemType(), left, idx, "ptr.add");
        } else {
            promoteToFloat();
            widenForArith();
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFAdd(left, right)
                : builder->CreateAdd(left, right);
        }
    } else if (node->op == "-") {
        if (left->getType()->isPointerTy() && right->getType()->isPointerTy()) {
            // ptr - ptr: byte-level difference → i64
            llvm::Value* l64 = builder->CreatePtrToInt(left,  llvm::Type::getInt64Ty(*context));
            llvm::Value* r64 = builder->CreatePtrToInt(right, llvm::Type::getInt64Ty(*context));
            result = builder->CreateSub(l64, r64, "ptrdiff");
        } else if (left->getType()->isPointerTy()) {
            llvm::Value* neg = builder->CreateNeg(
                builder->CreateSExtOrTrunc(right, llvm::Type::getInt64Ty(*context)), "neg");
            result = builder->CreateGEP(ptrElemType(), left, neg, "ptr.sub");
        } else {
            promoteToFloat(); widenForArith();
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFSub(left, right)
                : builder->CreateSub(left, right);
        }
    } else if (node->op == "*") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFMul(left, right)
            : builder->CreateMul(left, right);
    } else if (node->op == "/") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFDiv(left, right)
            : (opUnsigned ? builder->CreateUDiv(left, right)
                          : builder->CreateSDiv(left, right));
    } else if (node->op == "%") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFRem(left, right)
            : (opUnsigned ? builder->CreateURem(left, right)
                          : builder->CreateSRem(left, right));
    } else if (node->op == "==") {
        promoteToFloat();   // mixed float/int or float/double: bring both to a common float type
        if (left->getType()->isFloatingPointTy())
            result = builder->CreateFCmpOEQ(left, right);
        else {
            widenInts();   // equality is bit-equal; widening just needs the right extend
            result = builder->CreateICmpEQ(left, right);
        }
    } else if (node->op == "!=" || node->op == "<" || node->op == ">" ||
               node->op == "<=" || node->op == ">=") {
        promoteToFloat();   // mixed float/int or float/double: bring both to a common float type
        bool isFloat = left->getType()->isFloatingPointTy();
        if (!isFloat) widenInts();
        if (node->op == "!=") {
            result = isFloat ? builder->CreateFCmpONE(left, right)
                             : builder->CreateICmpNE(left, right);
        } else if (node->op == "<") {
            result = isFloat ? builder->CreateFCmpOLT(left, right)
                   : (opUnsigned ? builder->CreateICmpULT(left, right)
                                 : builder->CreateICmpSLT(left, right));
        } else if (node->op == ">") {
            result = isFloat ? builder->CreateFCmpOGT(left, right)
                   : (opUnsigned ? builder->CreateICmpUGT(left, right)
                                 : builder->CreateICmpSGT(left, right));
        } else if (node->op == "<=") {
            result = isFloat ? builder->CreateFCmpOLE(left, right)
                   : (opUnsigned ? builder->CreateICmpULE(left, right)
                                 : builder->CreateICmpSLE(left, right));
        } else {
            result = isFloat ? builder->CreateFCmpOGE(left, right)
                   : (opUnsigned ? builder->CreateICmpUGE(left, right)
                                 : builder->CreateICmpSGE(left, right));
        }
    } else if (node->op == "&&") {
        result = builder->CreateLogicalAnd(left, right);
    } else if (node->op == "||") {
        result = builder->CreateLogicalOr(left, right);
    // Bitwise operators (widen narrower integer before operating)
    } else if (node->op == "&") {
        widenForBitwise(); result = builder->CreateAnd(left, right);
    } else if (node->op == "|") {
        widenForBitwise(); result = builder->CreateOr(left, right);
    } else if (node->op == "^") {
        widenForBitwise(); result = builder->CreateXor(left, right);
    } else if (node->op == "<<") {
        widenForBitwise(); result = builder->CreateShl(left, right);
    } else if (node->op == ">>") {
        widenForBitwise();
        result = opUnsigned ? builder->CreateLShr(left, right)
                            : builder->CreateAShr(left, right);
    } else {
        throw std::runtime_error("Unknown binary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(QuestionExpr* node) {
    // `expr?` — if expr is an Err Result, return it from the enclosing function;
    // otherwise evaluate to the unwrapped success value.
    llvm::Value* resVal = evaluateExpr(node->operand);
    llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(resVal->getType());
    if (!st || !st->hasName())
        throw std::runtime_error("`?` operator requires a named Result struct value");
    std::string opType = st->getName().str();

    auto fIt = structFields.find(opType);
    if (fIt == structFields.end())
        throw std::runtime_error("`?` operator on non-Result type: " + opType);

    unsigned okIdx = 0, valueIdx = 0;
    std::string valueFieldType;
    for (unsigned i = 0; i < fIt->second.size(); ++i) {
        if (fIt->second[i].name == "ok")    okIdx = i;
        if (fIt->second[i].name == "value") { valueIdx = i; valueFieldType = fIt->second[i].type; }
    }

    // Materialize the Result into a temp so we can read fields and return it whole.
    llvm::Value* tmp = entryAlloca(st, nullptr, "try.tmp");
    builder->CreateStore(resVal, tmp);

    llvm::Value* okPtr = builder->CreateStructGEP(st, tmp, okIdx);
    llvm::Type*  okTy  = st->getElementType(okIdx);
    llvm::Value* okVal = builder->CreateLoad(okTy, okPtr, "try.ok");
    llvm::Value* isErr = builder->CreateICmpEQ(okVal, llvm::ConstantInt::get(okTy, 0), "try.iserr");

    llvm::BasicBlock* errBB  = llvm::BasicBlock::Create(*context, "try.err",  currentFunction);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "try.cont", currentFunction);
    builder->CreateCondBr(isErr, errBB, contBB);

    // Error path: propagate the Result unchanged out of the enclosing function.
    // This is an early function exit, so run pending defers/finally first.
    builder->SetInsertPoint(errBB);
    llvm::Value* whole = builder->CreateLoad(st, tmp, "try.whole");
    if (currentSretParam != nullptr) {
        builder->CreateStore(whole, currentSretParam);
        runCleanupsToDepth(0, /*errorPath=*/true);      // ? error exit: defers + errdefers
        builder->CreateRetVoid();
    } else {
        runCleanupsToDepth(0, /*errorPath=*/true);
        builder->CreateRet(whole);
    }

    // Success path: unwrap and yield the value field.
    builder->SetInsertPoint(contBB);
    llvm::Value* valPtr = builder->CreateStructGEP(st, tmp, valueIdx);
    llvm::Type*  valTy  = getTypeFromString(valueFieldType);
    exprValueStack.push(builder->CreateLoad(valTy, valPtr, "try.value"));
}

void CodeGen::visit(TernaryExpr* node) {
    // `cond ? a : b` — branch on the condition and evaluate exactly one arm, then phi
    // the results. Both arms are coerced to their common type (see the type checker).
    llvm::Value* cond = evaluateExpr(node->condition);
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isPointerTy())
            cond = builder->CreateICmpNE(
                cond, llvm::ConstantPointerNull::get(
                          llvm::cast<llvm::PointerType>(cond->getType())));
        else
            cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }

    std::string thenTy = getExprEskiuType(node->thenExpr);
    std::string elseTy = getExprEskiuType(node->elseExpr);
    llvm::Type* tLL = getTypeFromString(thenTy);
    llvm::Type* eLL = getTypeFromString(elseTy);
    llvm::Type* resTy;
    if (tLL == eLL)
        resTy = tLL;
    else if (tLL->isFloatingPointTy() || eLL->isFloatingPointTy())
        resTy = (tLL->isDoubleTy() || eLL->isDoubleTy())
                    ? llvm::Type::getDoubleTy(*context)
                    : llvm::Type::getFloatTy(*context);
    else if (tLL->isIntegerTy() && eLL->isIntegerTy())
        resTy = tLL->getIntegerBitWidth() >= eLL->getIntegerBitWidth() ? tLL : eLL;
    else
        resTy = tLL;

    auto coerce = [&](llvm::Value* v, const std::string& srcEskiu) -> llvm::Value* {
        if (v->getType() == resTy) return v;
        bool uns = eskiuUnsigned(srcEskiu);
        if (v->getType()->isIntegerTy() && resTy->isIntegerTy())             return coerceInt(v, resTy, uns);
        if (v->getType()->isIntegerTy() && resTy->isFloatingPointTy())       return intToFloat(v, resTy, uns);
        if (v->getType()->isFloatingPointTy() && resTy->isFloatingPointTy()) return builder->CreateFPCast(v, resTy);
        return v;
    };

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "tern.then", currentFunction);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "tern.else", currentFunction);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "tern.cont", currentFunction);
    builder->CreateCondBr(cond, thenBB, elseBB);

    builder->SetInsertPoint(thenBB);
    llvm::Value* tv = coerce(evaluateExpr(node->thenExpr), thenTy);
    llvm::BasicBlock* thenEnd = builder->GetInsertBlock();   // arm may have added blocks
    builder->CreateBr(contBB);

    builder->SetInsertPoint(elseBB);
    llvm::Value* ev = coerce(evaluateExpr(node->elseExpr), elseTy);
    llvm::BasicBlock* elseEnd = builder->GetInsertBlock();
    builder->CreateBr(contBB);

    builder->SetInsertPoint(contBB);
    llvm::PHINode* phi = builder->CreatePHI(resTy, 2);
    phi->addIncoming(tv, thenEnd);
    phi->addIncoming(ev, elseEnd);
    exprValueStack.push(phi);
}

void CodeGen::visit(UnaryExpr* node) {
    llvm::Value* val = evaluateExpr(node->operand);

    if (!val) {
        throw std::runtime_error("Unary operand evaluation failed");
    }

    llvm::Value* result = nullptr;

    if (node->op == "-") {
        result = val->getType()->isFloatingPointTy()
            ? builder->CreateFNeg(val)
            : builder->CreateNeg(val);
    } else if (node->op == "~") {
        result = builder->CreateNot(val); // bitwise NOT
    } else if (node->op == "!") {
        // Logical NOT: convert to bool
        if (val->getType()->isIntegerTy(1))
            result = builder->CreateNot(val);
        else
            result = builder->CreateICmpEQ(val, llvm::ConstantInt::get(val->getType(), 0));
    } else if (node->op == "&") {
        // Address-of: return the lvalue (alloca/GEP pointer), not the loaded value
        result = evaluateLValue(node->operand);
    } else if (node->op == "*") {
        // Dereference: use Eskiu type info to load the correct element type
        std::string ptrEskiuType = getExprEskiuType(node->operand);
        llvm::Type* elemType = llvm::Type::getInt8Ty(*context); // fallback
        if (!ptrEskiuType.empty()) {
            std::string elemStr;
            if (ptrEskiuType.front() == '*')
                elemStr = ptrEskiuType.substr(1);
            else if (ptrEskiuType.back() == '*')
                elemStr = ptrEskiuType.substr(0, ptrEskiuType.size() - 1);
            if (!elemStr.empty() && elemStr != "void")
                elemType = getTypeFromString(elemStr);
        }
        result = builder->CreateLoad(elemType, val);
    } else {
        throw std::runtime_error("Unknown unary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(IncDecExpr* node) {
    llvm::Value* ptr = evaluateLValue(node->operand);
    std::string ety = getExprEskiuType(node->operand);
    bool isPtr = !ety.empty() && (ety.front() == '*' || ety.back() == '*');
    llvm::Type* ty = isPtr ? (llvm::Type*)llvm::PointerType::get(*context, 0)
                           : getTypeFromString(ety.empty() ? "int" : ety);
    llvm::Value* old = builder->CreateLoad(ty, ptr);
    llvm::Value* nw;
    if (isPtr) {
        // pointer step by one element
        std::string elemStr = ety.front() == '*' ? ety.substr(1) : ety.substr(0, ety.size() - 1);
        llvm::Type* elemTy = (elemStr.empty() || elemStr == "void")
            ? (llvm::Type*)llvm::Type::getInt8Ty(*context) : getTypeFromString(elemStr);
        llvm::Value* step = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                                   node->decrement ? -1 : 1, true);
        nw = builder->CreateGEP(elemTy, old, step);
    } else {
        llvm::Value* one = llvm::ConstantInt::get(ty, 1);
        nw = node->decrement ? builder->CreateSub(old, one) : builder->CreateAdd(old, one);
    }
    builder->CreateStore(nw, ptr);
    exprValueStack.push(node->prefix ? nw : old);
}

void CodeGen::emitBoundsCheck(llvm::Value* idx, llvm::Value* len) {
    llvm::Type* i64 = llvm::Type::getInt64Ty(*context);
    llvm::Value* idx64 = idx->getType()->isIntegerTy(64) ? idx : builder->CreateSExt(idx, i64);
    llvm::Value* lo  = builder->CreateICmpSLT(idx64, llvm::ConstantInt::get(i64, 0));
    llvm::Value* hi  = builder->CreateICmpSGE(idx64, len);
    llvm::Value* oob = builder->CreateOr(lo, hi);
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    auto* trapBB = llvm::BasicBlock::Create(*context, "bounds.fail", fn);
    auto* contBB = llvm::BasicBlock::Create(*context, "bounds.ok",   fn);
    builder->CreateCondBr(oob, trapBB, contBB);
    builder->SetInsertPoint(trapBB);
    builder->CreateCall(llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap));
    builder->CreateUnreachable();
    builder->SetInsertPoint(contBB);
}

llvm::Value* CodeGen::indexElemAddr(const ExprPtr& base, llvm::Value* idx) {
    std::string baseType = getExprEskiuType(base);
    ty::Type bt = ty::Type::parse(baseType);
    if (baseType == "string")
        return builder->CreateGEP(llvm::Type::getInt8Ty(*context), evaluateExpr(base), idx);
    if (bt.kind == ty::Type::Kind::Array) {
        llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        uint64_t n = 0;
        if (safe && resolveArrayDim(bt.dim, n))   // bounds-check against the static length
            emitBoundsCheck(idx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), n));
        return builder->CreateGEP(getTypeFromString(baseType), evaluateLValue(base), {zero, idx});
    }
    if (bt.kind == ty::Type::Kind::Slice) {
        llvm::Value* fat  = evaluateExpr(base);                     // evaluate the slice once
        llvm::Value* data = builder->CreateExtractValue(fat, {0});  // fat.ptr
        if (safe) emitBoundsCheck(idx, builder->CreateExtractValue(fat, {1}));   // vs fat.len
        return builder->CreateGEP(getTypeFromString(bt.elem->str()), data, idx);
    }
    if (isPointerType(baseType)) {
        std::string elemStr = (!baseType.empty() && baseType.front() == '*')
            ? baseType.substr(1) : baseType.substr(0, baseType.size() - 1);
        return builder->CreateGEP(getTypeFromString(elemStr), evaluateExpr(base), idx);
    }
    throw std::runtime_error("Cannot index into type: " + baseType);
}

void CodeGen::visit(IndexExpr* node) {
    std::string baseType = getExprEskiuType(node->base);
    ty::Type bt = ty::Type::parse(baseType);
    llvm::Type* i64 = llvm::Type::getInt64Ty(*context);
    auto toI64 = [&](llvm::Value* v) -> llvm::Value* {
        if (v->getType()->isIntegerTy(64)) return v;
        return builder->CreateSExt(v, i64);
    };

    // Slice construction: base[lo..hi] → a fat pointer { &base[lo], hi - lo }.
    if (node->highIndex) {
        llvm::Value* lo = toI64(evaluateExpr(node->index));
        llvm::Value* hi = toI64(evaluateExpr(node->highIndex));
        llvm::Value* data = indexElemAddr(node->base, lo);
        llvm::Value* len  = builder->CreateSub(hi, lo);
        // Element type of the base: string→char, array/slice→elem, pointer→pointee.
        // Slicing a raw pointer (`*T`) yields `T[]`, so heap buffers can become slices.
        std::string elemStr;
        if (baseType == "string") {
            elemStr = "char";
        } else if ((bt.kind == ty::Type::Kind::Array || bt.kind == ty::Type::Kind::Slice) && bt.elem) {
            elemStr = bt.elem->str();
        } else if (isPointerType(baseType)) {
            elemStr = (!baseType.empty() && baseType.front() == '*')
                ? baseType.substr(1)
                : baseType.substr(0, baseType.size() - 1);
        } else {
            elemStr = "uint8";  // unreachable: sema rejects non-indexable slice bases
        }
        llvm::Type* sliceTy = getTypeFromString(elemStr + "[]");
        llvm::Value* s = llvm::UndefValue::get(sliceTy);
        s = builder->CreateInsertValue(s, data, {0});
        s = builder->CreateInsertValue(s, len,  {1});
        exprValueStack.push(s);
        return;
    }

    llvm::Value* idx = evaluateExpr(node->index);

    // Slice element: s[i] → load from the fat pointer's data at i.
    if (bt.kind == ty::Type::Kind::Slice) {
        llvm::Type* elemType = getTypeFromString(bt.elem->str());
        exprValueStack.push(builder->CreateLoad(elemType, indexElemAddr(node->base, idx)));
        return;
    }
    // String: string[i] → char.
    if (baseType == "string") {
        exprValueStack.push(builder->CreateLoad(
            llvm::Type::getInt8Ty(*context), indexElemAddr(node->base, idx)));
        return;
    }
    // Fixed-size array: T[N] (for T[N][M], indexing peels the outer dimension → T[M]).
    if (bt.kind == ty::Type::Kind::Array) {
        llvm::Type* elemType = getTypeFromString(bt.elem->str());
        exprValueStack.push(builder->CreateLoad(elemType, indexElemAddr(node->base, idx)));
        return;
    }
    // Pointer: *T or T*.
    if (isPointerType(baseType)) {
        std::string elemStr = (!baseType.empty() && baseType.front() == '*')
            ? baseType.substr(1)
            : baseType.substr(0, baseType.size() - 1);
        exprValueStack.push(builder->CreateLoad(
            getTypeFromString(elemStr), indexElemAddr(node->base, idx)));
        return;
    }

    throw std::runtime_error("Cannot index into type: " + baseType);
}

std::string CodeGen::structBaseTypeOf(const ExprPtr& base) {
    std::string baseType = getExprEskiuType(base);
    if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
    while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();
    if (baseType.find('<') != std::string::npos) {
        auto [tn, args] = splitTemplateType(baseType);
        ensureTemplateInstantiated(mangleTemplate(baseType), tn, args);
        baseType = mangleTemplate(baseType);
    }
    return baseType;
}

void CodeGen::visit(MemberExpr* node) {
    // Slice `.len`: read the fat pointer's length field (i64).
    if (node->member == "len" && ty::Type::parse(getExprEskiuType(node->base)).kind == ty::Type::Kind::Slice) {
        exprValueStack.push(builder->CreateExtractValue(evaluateExpr(node->base), {1}));
        return;
    }

    std::string baseType = structBaseTypeOf(node->base);

    // A pointer-to-struct base is dereferenced via its value; a value-struct
    // base via its address (see the matching logic in evaluateLValue).
    std::string rawBaseTy = getExprEskiuType(node->base);
    bool baseIsPtr = (!rawBaseTy.empty() && (rawBaseTy.front() == '*' || rawBaseTy.back() == '*'));
    // A struct-valued temporary (call result, template call, struct literal) is
    // an rvalue with no address — materialize it into an alloca so we can GEP.
    Expr* b = node->base.get();
    bool baseIsTemp = !baseIsPtr &&
        (dynamic_cast<CallExpr*>(b) || dynamic_cast<TemplateCallExpr*>(b) ||
         dynamic_cast<StructInitExpr*>(b));
    auto baseAddr = [&]() -> llvm::Value* {
        if (baseIsPtr) return evaluateExpr(node->base);
        if (baseIsTemp) {
            llvm::Value* v = evaluateExpr(node->base);
            llvm::Value* tmp = entryAlloca(v->getType(), nullptr, "mem.tmp");
            builder->CreateStore(v, tmp);
            return tmp;
        }
        return evaluateLValue(node->base);
    };

    // Bitfield-layout struct: physical slot map (handles bitfields and the
    // non-bitfield fields whose physical index differs from the logical one).
    auto lit = structLayout.find(baseType);
    if (lit != structLayout.end()) {
        auto sit = lit->second.find(node->member);
        if (sit == lit->second.end())
            throw std::runtime_error("Struct '" + baseType + "' has no field '" + node->member + "'");
        const BitfieldSlot& slot = sit->second;
        llvm::Value* basePtr = baseAddr();
        llvm::Value* gep = builder->CreateStructGEP(structTypes[baseType], basePtr,
                                                    slot.physIndex, node->member);
        if (!slot.isBitfield) {
            exprValueStack.push(builder->CreateLoad(slot.storageType, gep, node->member));
            return;
        }
        llvm::Type* sty = slot.storageType;
        llvm::Value* word = builder->CreateLoad(sty, gep);
        llvm::Value* shifted = slot.bitOffset
            ? builder->CreateLShr(word, llvm::ConstantInt::get(sty, slot.bitOffset)) : word;
        uint64_t mask = (slot.bitWidth >= 64) ? ~0ULL : ((1ULL << slot.bitWidth) - 1);
        llvm::Value* masked = builder->CreateAnd(shifted, llvm::ConstantInt::get(sty, mask));
        if (slot.isSigned && slot.bitWidth < sty->getIntegerBitWidth()) {
            unsigned sh = sty->getIntegerBitWidth() - slot.bitWidth;
            masked = builder->CreateAShr(builder->CreateShl(masked, sh), sh);
        }
        exprValueStack.push(masked);
        return;
    }

    auto fit = structFields.find(baseType);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct type in member access: '" + baseType + "'");

    const auto& fields = fit->second;
    bool isUnion = unionFields.count(baseType) > 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == node->member) {
            llvm::Value* basePtr = baseAddr();
            llvm::Type*  fieldTy = getTypeFromString(fields[i].type);
            llvm::Value* ptr;
            if (isUnion) {
                // Union: all fields at offset 0 — base ptr is the field ptr
                ptr = basePtr;
            } else {
                ptr = builder->CreateStructGEP(structTypes[baseType], basePtr, i, node->member);
            }
            exprValueStack.push(builder->CreateLoad(fieldTy, ptr, node->member));
            return;
        }
    }
    throw std::runtime_error("Struct/union '" + baseType + "' has no field '" + node->member + "'");
}

void CodeGen::storeBitfieldInto(llvm::Value* wordPtr, const BitfieldSlot& slot,
                                llvm::Value* val) {
    llvm::Type* sty = slot.storageType;  // integer storage word
    if (val->getType() != sty) {
        if (val->getType()->isIntegerTy())
            val = val->getType()->getIntegerBitWidth() > sty->getIntegerBitWidth()
                ? builder->CreateTrunc(val, sty) : builder->CreateZExt(val, sty);
        else if (val->getType()->isFloatingPointTy())
            val = builder->CreateFPToSI(val, sty);
    }
    llvm::Value* word = builder->CreateLoad(sty, wordPtr);
    uint64_t mask = (slot.bitWidth >= 64) ? ~0ULL : ((1ULL << slot.bitWidth) - 1);
    llvm::Value* fieldMask = llvm::ConstantInt::get(sty, mask << slot.bitOffset);
    llvm::Value* cleared  = builder->CreateAnd(word, builder->CreateNot(fieldMask));
    llvm::Value* vMasked  = builder->CreateAnd(val, llvm::ConstantInt::get(sty, mask));
    llvm::Value* vShifted = slot.bitOffset
        ? builder->CreateShl(vMasked, llvm::ConstantInt::get(sty, slot.bitOffset)) : vMasked;
    builder->CreateStore(builder->CreateOr(cleared, vShifted), wordPtr);
}

void CodeGen::storeBitfield(MemberExpr* m, llvm::Value* val) {
    std::string baseType = structBaseTypeOf(m->base);
    const BitfieldSlot& slot = structLayout[baseType][m->member];
    llvm::Value* basePtr = evaluateLValue(m->base);
    llvm::Value* gep = builder->CreateStructGEP(structTypes[baseType], basePtr, slot.physIndex);
    storeBitfieldInto(gep, slot, val);
}

void CodeGen::visit(CastExpr* node) {
    llvm::Type* targetType = getTypeFromString(node->targetType);

    // Casting a top-level function name to a pointer type yields its RAW C
    // function pointer — the bare symbol address, not the {fn, env} closure fat
    // pointer the name would otherwise decay to. This is how an Eskiu function is
    // handed to a C API as a callback (e.g. OpenSSL's ALPN select callback).
    if (targetType->isPointerTy()) {
        if (auto* id = dynamic_cast<IdentExpr*>(node->expr.get())) {
            if (!lookupSymbol(id->name)) {               // not shadowed by a variable
                if (llvm::Function* fn = module->getFunction(id->name)) {
                    exprValueStack.push(fn);             // a Function* is already a ptr
                    return;
                }
            }
        }
    }

    llvm::Value* val = evaluateExpr(node->expr);

    llvm::Value* result = nullptr;

    if (val->getType() == targetType) {
        result = val;
    } else if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
        // Integer to integer
        unsigned srcWidth = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
        unsigned dstWidth = llvm::cast<llvm::IntegerType>(targetType)->getBitWidth();
        if (srcWidth < dstWidth) {
            // Widen per the SOURCE's signedness: an unsigned source (uint*/char/bool)
            // zero-extends — e.g. (int)(uint8)255 is 255, not -1.
            bool uns = eskiuUnsigned(getExprEskiuType(node->expr));
            result = uns ? builder->CreateZExt(val, targetType)
                         : builder->CreateSExt(val, targetType);
        } else {
            result = builder->CreateTrunc(val, targetType);
        }
    } else if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy()) {
        result = intToFloat(val, targetType, eskiuUnsigned(getExprEskiuType(node->expr)));
    } else if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy()) {
        result = builder->CreateFPToSI(val, targetType);
    } else if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        result = builder->CreateFPCast(val, targetType);
    } else if (val->getType()->isPointerTy() && targetType->isIntegerTy()) {
        result = builder->CreatePtrToInt(val, targetType);
    } else if (val->getType()->isIntegerTy() && targetType->isPointerTy()) {
        result = builder->CreateIntToPtr(val, targetType);
    } else if (val->getType()->isPointerTy() && targetType->isPointerTy()) {
        result = val; // opaque pointers: ptr == ptr, no bitcast needed
    } else {
        throw std::runtime_error("Cannot cast between these types");
    }

    exprValueStack.push(result);
}

void CodeGen::visit(LiteralExpr* node) {
    llvm::Value* result = nullptr;

    switch (node->kind) {
        case LiteralExpr::Kind::INT: {
            // base 0 = auto (dec/hex/oct). Materialize as i64 when the value
            // does not fit in a *signed* 32-bit int, so large literals are not
            // truncated. A literal in [2^31, 2^32) fits u32 but not i32; keeping
            // it i32 would set the high bit and then sign-extend to a negative
            // i64 on assignment, so it must be i64 (matches the self-host).
            unsigned long long uval;
            bool wide;
            try {
                long long sval = std::stoll(node->value, nullptr, 0);
                uval = (unsigned long long)sval;
                wide = (sval < -2147483648LL || sval > 2147483647LL);
            } catch (const std::out_of_range&) {
                uval = std::stoull(node->value, nullptr, 0); // e.g. large uint64 literal
                wide = true;
            }
            llvm::Type* ity = wide ? llvm::Type::getInt64Ty(*context)
                                   : llvm::Type::getInt32Ty(*context);
            result = llvm::ConstantInt::get(ity, uval, false);
            break;
        }
        case LiteralExpr::Kind::FLOAT: {
            double val = std::stod(node->value);
            result = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), val);
            break;
        }
        case LiteralExpr::Kind::STRING: {
            result = builder->CreateGlobalString(node->value);
            break;
        }
        case LiteralExpr::Kind::BOOL: {
            bool val = node->value == "true";
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), val);
            break;
        }
        case LiteralExpr::Kind::NULL_VAL: {
            auto ptrType = llvm::PointerType::get(*context, 0);
            result = llvm::ConstantPointerNull::get(ptrType);
            break;
        }
        case LiteralExpr::Kind::CHAR: {
            char val = node->value.empty() ? 0 : node->value[0];
            result = llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), val);
            break;
        }
    }

    exprValueStack.push(result);
}

void CodeGen::visit(IdentExpr* node) {
    // Bare enum member, e.g. `Red` — an i32 constant.
    if (!lookupSymbol(node->name)) {
        auto ec = enumConstants.find(node->name);
        if (ec != enumConstants.end()) {
            exprValueStack.push(llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(*context), ec->second, /*isSigned=*/true));
            return;
        }
        // Bare algebraic variant with no payload, e.g. `None`.
        if (adtVariants.count(node->name)) {
            exprValueStack.push(buildVariant(node->name, {}));
            return;
        }
    }

    // Look up variable
    llvm::Value* val = lookupSymbol(node->name);

    if (!val) {
        // A bare function name used as a value decays to a closure fat pointer.
        if (auto* fn = module->getFunction(node->name)) {
            exprValueStack.push(makeFunctionPointer(fn));
            return;
        }
    }

    if (!val) {
        throw std::runtime_error("Undefined variable or function: " + node->name);
    }

    llvm::Value* result = nullptr;

    bool vol = volatileVars.count(node->name) > 0;
    if (llvm::isa<llvm::AllocaInst>(val)) {
        auto* inst = builder->CreateLoad(
            llvm::cast<llvm::AllocaInst>(val)->getAllocatedType(), val);
        inst->setVolatile(vol);
        result = inst;
    } else if (llvm::isa<llvm::GlobalVariable>(val)) {
        auto* gv = llvm::cast<llvm::GlobalVariable>(val);
        auto* inst = builder->CreateLoad(gv->getValueType(), gv);
        inst->setVolatile(vol);
        result = inst;
    } else {
        // Function argument or function pointer
        result = val;
    }

    exprValueStack.push(result);
}

llvm::Value* CodeGen::evaluateExpr(const ExprPtr& expr) {
    expr->accept(this);
    llvm::Value* result = exprValueStack.top();
    exprValueStack.pop();
    return result;
}

void CodeGen::visit(SizeofExpr* node) {
    llvm::Type* ty   = getTypeFromString(node->typeName);
    uint64_t    size = module->getDataLayout().getTypeAllocSize(ty);
    exprValueStack.push(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size));
}

llvm::Value* CodeGen::evaluateLValue(const ExprPtr& expr) {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        llvm::Value* val = lookupSymbol(ident->name);
        if (!val) throw std::runtime_error("Undefined variable: " + ident->name);
        return val;
    }

    // Dereference as lvalue: *ptr = val — load the pointer value, use as the store target
    if (auto unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (unary->op == "*") {
            // evaluateExpr gives us the pointer value; that IS the lvalue address
            return evaluateExpr(unary->operand);
        }
    }

    if (auto member = dynamic_cast<MemberExpr*>(expr.get())) {
        std::string baseType = getExprEskiuType(member->base);
        // A pointer-to-struct base must be dereferenced: the struct pointer is the
        // base's *value* (evaluateExpr loads a local pointer var or yields a param),
        // whereas a value-struct base uses its *address* (evaluateLValue).
        bool baseIsPtr = (!baseType.empty() && (baseType.front() == '*' || baseType.back() == '*'));
        auto baseAddr = [&]() -> llvm::Value* {
            return baseIsPtr ? evaluateExpr(member->base) : evaluateLValue(member->base);
        };
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
    while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();
    if (baseType.find('<') != std::string::npos) {
        auto [tn, args] = splitTemplateType(baseType);
        ensureTemplateInstantiated(mangleTemplate(baseType), tn, args);
        baseType = mangleTemplate(baseType);
    }
        auto fit = structFields.find(baseType);
        if (fit == structFields.end())
            throw std::runtime_error("Unknown struct type: " + baseType);
        const auto& fields = fit->second;
        // Union field access: all fields are at offset 0 — just return the base ptr
        // (the load/store will use the field's type via the caller)
        bool isUnion = unionFields.count(baseType) > 0;
        auto lit = structLayout.find(baseType);
        if (lit != structLayout.end()) {
            auto sit = lit->second.find(member->member);
            if (sit != lit->second.end()) {
                if (sit->second.isBitfield)
                    throw std::runtime_error("cannot take the address of bitfield '"
                                             + member->member + "'");
                llvm::Value* basePtr = baseAddr();
                return builder->CreateStructGEP(structTypes[baseType], basePtr,
                                                sit->second.physIndex);
            }
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == member->member) {
                llvm::Value* basePtr = baseAddr();
                if (isUnion) return basePtr; // offset 0 for all union fields
                return builder->CreateStructGEP(structTypes[baseType], basePtr, i);
            }
        }
        throw std::runtime_error("Struct/union '" + baseType + "' has no field '" + member->member + "'");
    }

    if (auto index = dynamic_cast<IndexExpr*>(expr.get())) {
        // `a[i] = x` — element address for array / slice / pointer / string bases.
        llvm::Value* idx = evaluateExpr(index->index);
        return indexElemAddr(index->base, idx);
    }

    throw std::runtime_error("Left-hand side of assignment is not an lvalue");
}
