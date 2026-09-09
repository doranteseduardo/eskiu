#include "codegen.h"
#include "../ast/type_qual.h"

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// ============================================================================
// Symbol Table Management
// ============================================================================

void CodeGen::pushScope() {
    scopeStack.push_back(symbolTable);
    varTypeStack.push_back({});
}

void CodeGen::popScope() {
    if (!scopeStack.empty()) {
        symbolTable = scopeStack.back();
        scopeStack.pop_back();
    }
    if (!varTypeStack.empty()) {
        varTypeStack.pop_back();
    }
}

void CodeGen::defineVarType(const std::string& name, const std::string& type) {
    if (!varTypeStack.empty())
        varTypeStack.back()[name] = type;
    else
        globalVarTypes[name] = type;   // top-level / global scope
}

std::string CodeGen::lookupVarType(const std::string& name) const {
    for (auto it = varTypeStack.rbegin(); it != varTypeStack.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    auto g = globalVarTypes.find(name);
    return g != globalVarTypes.end() ? g->second : "";
}

static llvm::Constant* coerceConst(llvm::Constant* c, llvm::Type* ty);   // defined below

llvm::Constant* CodeGen::evaluateConstantExpr(const ExprPtr& expr) {
    // A non-capturing lambda is a compile-time-constant closure {fn_ptr, null}:
    // emit its function and fold to the constant fat pointer, so a global/static
    // initializer holds a real callable instead of a null closure. (A capturing
    // lambda needs a live frame, so it can't be a global constant.)
    if (auto* lam = dynamic_cast<LambdaExpr*>(expr.get())) {
        if (!lam->captures.empty()) return nullptr;
        std::string lambdaName = "__lambda" + std::to_string(lambdaSeq++);
        llvm::Function* func = emitLambdaFunction(lam, lambdaName, nullptr);
        auto* fatTy = llvm::cast<llvm::StructType>(getTypeFromString("fn()->void"));
        return llvm::ConstantStruct::get(fatTy,
            {func, llvm::ConstantPointerNull::get(
                       llvm::PointerType::get(*context, 0))});
    }

    // Fold unary minus on a numeric literal: -(N) → negative constant
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (unary->op == "-") {
            llvm::Constant* inner = evaluateConstantExpr(unary->operand);
            if (!inner) return nullptr;
            if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(inner))
                return llvm::ConstantInt::get(ci->getType(),
                    static_cast<uint64_t>(-(int64_t)ci->getZExtValue()), true);
            if (auto* cf = llvm::dyn_cast<llvm::ConstantFP>(inner))
                return llvm::ConstantFP::get(cf->getType(),
                    -cf->getValueAPF().convertToDouble());
        }
        return nullptr;
    }

    // Fold a numeric cast `(T)expr` on a constant operand: fold the operand, then
    // convert it to the target type (so `(uint8)10` / `(float)0.5` in a global array
    // initializer are real constants, not zero).
    if (auto* cast = dynamic_cast<CastExpr*>(expr.get())) {
        llvm::Constant* inner = evaluateConstantExpr(cast->expr);
        if (!inner) return nullptr;
        llvm::Type* ty = getTypeFromString(cast->targetType);
        if (!ty) return nullptr;
        return coerceConst(inner, ty);
    }

    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    if (!lit) return nullptr;

    switch (lit->kind) {
        case LiteralExpr::Kind::INT: {
            long long v = std::stoll(lit->value, nullptr, 0);
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), v);
        }
        case LiteralExpr::Kind::FLOAT: {
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                          std::stod(lit->value));
        }
        case LiteralExpr::Kind::BOOL: {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context),
                                           lit->value == "true" ? 1 : 0);
        }
        case LiteralExpr::Kind::CHAR: {
            char c = lit->value.empty() ? 0 : lit->value[0];
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), c);
        }
        case LiteralExpr::Kind::STRING: {
            // Build a private string constant and return a pointer to it
            auto* arrType = llvm::ArrayType::get(llvm::Type::getInt8Ty(*context),
                                                   lit->value.size() + 1);
            std::vector<llvm::Constant*> chars;
            for (unsigned char c : lit->value)
                chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), c));
            chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0));
            auto* strData = new llvm::GlobalVariable(
                *module, arrType, true,
                llvm::GlobalValue::PrivateLinkage,
                llvm::ConstantArray::get(arrType, chars), ".gstr");
            // Return pointer to first element (ptr in opaque-pointer IR)
            return strData;
        }
        case LiteralExpr::Kind::NULL_VAL:
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
        default:
            return nullptr;
    }
}

// Coerce a folded scalar constant to `ty` (int<->int width, fp<->fp width, int->fp,
// fp->int). Also what a numeric `(T)expr` cast folds to inside a constant initializer.
static llvm::Constant* coerceConst(llvm::Constant* c, llvm::Type* ty) {
    if (!c || c->getType() == ty) return c;
    if (c->getType()->isIntegerTy() && ty->isIntegerTy())
        return llvm::ConstantInt::get(ty, llvm::cast<llvm::ConstantInt>(c)->getZExtValue());
    if (c->getType()->isFloatingPointTy() && ty->isFloatingPointTy())
        return llvm::ConstantFP::get(ty,
            llvm::cast<llvm::ConstantFP>(c)->getValueAPF().convertToDouble());
    if (c->getType()->isIntegerTy() && ty->isFloatingPointTy())
        return llvm::ConstantFP::get(ty,
            (double)llvm::cast<llvm::ConstantInt>(c)->getSExtValue());
    if (c->getType()->isFloatingPointTy() && ty->isIntegerTy())
        return llvm::ConstantInt::get(ty,
            (uint64_t)(int64_t)llvm::cast<llvm::ConstantFP>(c)->getValueAPF().convertToDouble(),
            /*isSigned=*/true);
    return nullptr;   // no constant coercion available (e.g. pointer/aggregate mismatch)
}

llvm::Constant* CodeGen::constInitializer(const ExprPtr& expr, llvm::Type* declType) {
    // Array literal `{...}` against a fixed-size array type: fold each element to the
    // array's element type and zero-fill any tail (C semantics: `int[3] = {1}` → {1,0,0}).
    if (auto* arr = dynamic_cast<ArrayLitExpr*>(expr.get())) {
        auto* arrTy = llvm::dyn_cast<llvm::ArrayType>(declType);
        if (!arrTy) return nullptr;
        llvm::Type* elemTy = arrTy->getElementType();
        uint64_t n = arrTy->getNumElements();
        std::vector<llvm::Constant*> elems;
        for (auto& e : arr->elements) {
            if (elems.size() >= n) break;        // extra elements ignored
            llvm::Constant* c = constInitializer(e, elemTy);   // recurse (nested arrays)
            if (!c) return nullptr;              // a non-constant element: not foldable
            elems.push_back(c);
        }
        while (elems.size() < n) elems.push_back(llvm::Constant::getNullValue(elemTy));
        return llvm::ConstantArray::get(arrTy, elems);
    }
    return coerceConst(evaluateConstantExpr(expr), declType);
}

llvm::Value* CodeGen::lookupSymbol(const std::string& name) {
    auto it = symbolTable.find(name);
    if (it != symbolTable.end()) {
        return it->second;
    }
    return nullptr;
}

void CodeGen::defineSymbol(const std::string& name, llvm::Value* value) {
    symbolTable[name] = value;
}
