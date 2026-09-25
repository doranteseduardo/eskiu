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

    // `&global` is a link-time constant address.
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get()); unary && unary->op == "&") {
        if (auto* id = dynamic_cast<IdentExpr*>(unary->operand.get()))
            if (auto* gv = llvm::dyn_cast_or_null<llvm::GlobalVariable>(lookupSymbol(id->name)))
                return gv;
        return nullptr;
    }

    // Fold a built-in binary operator over constant operands (`3 + 1`, `A * 2`, `1.0/4.0`).
    // A left-leaning chain (`A + B + C ...`) is folded along its spine with a loop.
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr.get())) {
        std::vector<BinaryExpr*> spine{bin};
        while (auto* l = dynamic_cast<BinaryExpr*>(spine.back()->left.get())) spine.push_back(l);
        for (auto* b : spine) if (!b->opFunc.empty()) return nullptr;
        llvm::Constant* value = evaluateConstantExpr(spine.back()->left);
        for (size_t i = spine.size(); i-- > 0 && value;) {
            BinaryExpr* b = spine[i];
            llvm::Constant* r = evaluateConstantExpr(b->right);
            if (!r) return nullptr;
            bool uns = eskiuUnsigned(getExprEskiuType(b->left)) || eskiuUnsigned(getExprEskiuType(b->right));
            value = foldConstBinary(b->op, value, r, uns);
        }
        return value;
    }

    // Fold `c ? a : b` with a constant condition to the chosen arm.
    if (auto* ter = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto* c = llvm::dyn_cast_or_null<llvm::ConstantInt>(evaluateConstantExpr(ter->condition));
        if (!c) return nullptr;
        return evaluateConstantExpr(c->isZero() ? ter->elseExpr : ter->thenExpr);
    }

    // Fold unary `-`, `~`, `!`, `+` on a constant operand.
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        llvm::Constant* inner = evaluateConstantExpr(unary->operand);
        if (unary->op == "+") return inner;
        if (!inner) return nullptr;
        auto* ci = llvm::dyn_cast<llvm::ConstantInt>(inner);
        auto* cf = llvm::dyn_cast<llvm::ConstantFP>(inner);
        if (unary->op == "-") {
            if (ci) return constIntBits(ci->getType(),
                static_cast<uint64_t>(-ci->getSExtValue()));
            if (cf) return llvm::ConstantFP::get(cf->getType(), -cf->getValueAPF().convertToDouble());
        } else if (unary->op == "~") {
            if (ci) return constIntBits(ci->getType(), ~ci->getZExtValue());
        } else if (unary->op == "!") {
            if (ci) return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context),
                ci->getZExtValue() == 0 ? 1 : 0);
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

    // A bare identifier that names an enum constant or a folded top-level `const int`.
    if (auto* id = dynamic_cast<IdentExpr*>(expr.get())) {
        auto ec = enumConstants.find(id->name);
        if (ec != enumConstants.end())
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), ec->second, true);
        auto ci = constInts.find(id->name);
        if (ci != constInts.end())
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), (uint64_t)ci->second, true);
        auto cg = constGlobalValues.find(id->name);
        if (cg != constGlobalValues.end()) return cg->second;
        return nullptr;
    }
    // sizeof(T) -> i64 byte size.
    if (auto* so = dynamic_cast<SizeofExpr*>(expr.get())) {
        llvm::Type* ty = getTypeFromString(so->typeName);
        if (!ty) return nullptr;
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
            module->getDataLayout().getTypeAllocSize(ty));
    }

    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    if (!lit) return nullptr;

    switch (lit->kind) {
        case LiteralExpr::Kind::INT: {
            // Fold as i64 so a value that needs more than 32 bits survives; the caller's
            // coerceConst narrows to the declared slot width (C-style truncation).
            uint64_t v;
            try { v = (uint64_t)std::stoll(lit->value, nullptr, 0); }
            catch (...) { v = std::stoull(lit->value, nullptr, 0); }
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), v);
        }
        case LiteralExpr::Kind::FLOAT: {
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                          std::strtod(lit->value.c_str(), nullptr));
        }
        case LiteralExpr::Kind::BOOL: {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context),
                                           lit->value == "true" ? 1 : 0);
        }
        case LiteralExpr::Kind::CHAR: {
            char c = lit->value.empty() ? 0 : lit->value[0];
            return constIntBits(llvm::Type::getInt8Ty(*context), (uint8_t)c);
        }
        case LiteralExpr::Kind::STRING: {
            // Build a private string constant and return a pointer to it
            auto* arrType = llvm::ArrayType::get(llvm::Type::getInt8Ty(*context),
                                                   lit->value.size() + 1);
            std::vector<llvm::Constant*> chars;
            for (unsigned char c : lit->value)
                chars.push_back(constIntBits(llvm::Type::getInt8Ty(*context), (uint8_t)c));
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

llvm::Constant* CodeGen::foldConstBinary(const std::string& op, llvm::Constant* a,
                                         llvm::Constant* b, bool isUnsigned) {
    auto* i1 = llvm::Type::getInt1Ty(*context);
    auto* i64 = llvm::Type::getInt64Ty(*context);
    auto boolC = [&](bool v) { return llvm::ConstantInt::get(i1, v ? 1 : 0); };
    auto* fa = llvm::dyn_cast<llvm::ConstantFP>(a);
    auto* fb = llvm::dyn_cast<llvm::ConstantFP>(b);
    auto* ia = llvm::dyn_cast<llvm::ConstantInt>(a);
    auto* ib = llvm::dyn_cast<llvm::ConstantInt>(b);
    if ((!fa && !ia) || (!fb && !ib)) return nullptr;
    // An i1 (bool) operand is 0/1; every other integer widens by its signedness.
    auto intVal = [&](llvm::ConstantInt* c) -> int64_t {
        if (c->getType()->isIntegerTy(1) || isUnsigned) return (int64_t)c->getZExtValue();
        return c->getSExtValue();
    };
    if (op == "&&" || op == "||") {
        bool x = fa ? !fa->isZero() : !ia->isZero();
        bool y = fb ? !fb->isZero() : !ib->isZero();
        return boolC(op == "&&" ? (x && y) : (x || y));
    }
    if (fa || fb) {
        auto dv = [&](llvm::ConstantFP* f, llvm::ConstantInt* c) -> double {
            if (f) return f->getValueAPF().convertToDouble();
            return isUnsigned ? (double)c->getZExtValue() : (double)intVal(c);
        };
        double x = dv(fa, ia), y = dv(fb, ib);
        // `float op float` stays float; anything with a double (or a literal) is double.
        llvm::Type* ft = llvm::Type::getDoubleTy(*context);
        if ((!fa || fa->getType()->isFloatTy()) && (!fb || fb->getType()->isFloatTy()))
            ft = llvm::Type::getFloatTy(*context);
        if (op == "+") return llvm::ConstantFP::get(ft, x + y);
        if (op == "-") return llvm::ConstantFP::get(ft, x - y);
        if (op == "*") return llvm::ConstantFP::get(ft, x * y);
        if (op == "/") return llvm::ConstantFP::get(ft, x / y);
        if (op == "==") return boolC(x == y);
        if (op == "!=") return boolC(x != y);
        if (op == "<")  return boolC(x < y);
        if (op == ">")  return boolC(x > y);
        if (op == "<=") return boolC(x <= y);
        if (op == ">=") return boolC(x >= y);
        return nullptr;
    }
    int64_t x = intVal(ia), y = intVal(ib);
    uint64_t ux = (uint64_t)x, uy = (uint64_t)y;
    auto iC = [&](uint64_t v) { return llvm::ConstantInt::get(i64, v); };
    if (op == "+") return iC(ux + uy);
    if (op == "-") return iC(ux - uy);
    if (op == "*") return iC(ux * uy);
    if (op == "/" || op == "%") {
        if (y == 0) return nullptr;
        if (isUnsigned) return iC(op == "/" ? ux / uy : ux % uy);
        if (x == INT64_MIN && y == -1) return iC(op == "/" ? ux : 0);
        return iC((uint64_t)(op == "/" ? x / y : x % y));
    }
    if (op == "&") return iC(ux & uy);
    if (op == "|") return iC(ux | uy);
    if (op == "^") return iC(ux ^ uy);
    if (op == "<<") return iC(uy >= 64 ? 0 : ux << uy);
    if (op == ">>") {
        if (uy >= 64) return iC(isUnsigned || x >= 0 ? 0 : ~0ULL);
        return iC(isUnsigned ? ux >> uy : (uint64_t)(x >> y));
    }
    if (op == "==") return boolC(x == y);
    if (op == "!=") return boolC(x != y);
    if (op == "<")  return boolC(isUnsigned ? ux < uy : x < y);
    if (op == ">")  return boolC(isUnsigned ? ux > uy : x > y);
    if (op == "<=") return boolC(isUnsigned ? ux <= uy : x <= y);
    if (op == ">=") return boolC(isUnsigned ? ux >= uy : x >= y);
    return nullptr;
}

// Coerce a folded scalar constant to `ty` (int<->int width, fp<->fp width, int->fp,
// fp->int). Also what a numeric `(T)expr` cast folds to inside a constant initializer.
static llvm::Constant* coerceConst(llvm::Constant* c, llvm::Type* ty) {
    if (!c || c->getType() == ty) return c;
    if (c->getType()->isIntegerTy() && ty->isIntegerTy())
        return constIntBits(ty, llvm::cast<llvm::ConstantInt>(c)->getZExtValue());
    if (c->getType()->isFloatingPointTy() && ty->isFloatingPointTy())
        return llvm::ConstantFP::get(ty,
            llvm::cast<llvm::ConstantFP>(c)->getValueAPF().convertToDouble());
    if (c->getType()->isIntegerTy() && ty->isFloatingPointTy())
        return llvm::ConstantFP::get(ty,
            (double)llvm::cast<llvm::ConstantInt>(c)->getSExtValue());
    if (c->getType()->isFloatingPointTy() && ty->isIntegerTy())
        return constIntBits(ty,
            (uint64_t)(int64_t)llvm::cast<llvm::ConstantFP>(c)->getValueAPF().convertToDouble());
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
    // Struct literal `S{ ... }` against a struct type: fold each named/positional field
    // to a constant and zero-fill the rest. Bitfield-packed structs need physical-slot
    // packing, so those fall through to a zero global (a documented limitation).
    if (auto* si = dynamic_cast<StructInitExpr*>(expr.get())) {
        std::string sname = resolveStructInitName(si->structName);
        auto fit = structFields.find(sname);
        auto stIt = structTypes.find(sname);
        if (fit == structFields.end() || stIt == structTypes.end()) return nullptr;
        if (structLayout.count(sname)) return nullptr;   // bitfield struct: not folded
        if (unionFields.count(sname)) return nullptr;    // union: members overlap, not folded
        const auto& fields = fit->second;
        llvm::StructType* st = stIt->second;
        std::vector<llvm::Constant*> vals(fields.size(), nullptr);
        bool named = !si->fieldInits.empty() && !si->fieldInits[0].first.empty();
        auto foldField = [&](size_t i, const ExprPtr& e) -> bool {
            if (i >= fields.size()) return true;
            llvm::Constant* c = constInitializer(e, st->getElementType((unsigned)i));
            if (!c) return false;               // a provided field isn't constant → bail
            vals[i] = c;
            return true;
        };
        if (named) {
            for (const auto& [fname, e] : si->fieldInits)
                for (size_t i = 0; i < fields.size(); ++i)
                    if (fields[i].name == fname) { if (!foldField(i, e)) return nullptr; break; }
        } else {
            for (size_t i = 0; i < si->fieldInits.size() && i < fields.size(); ++i)
                if (!foldField(i, si->fieldInits[i].second)) return nullptr;
        }
        for (size_t i = 0; i < vals.size(); ++i)
            if (!vals[i]) vals[i] = llvm::Constant::getNullValue(st->getElementType((unsigned)i));
        return llvm::ConstantStruct::get(st, vals);
    }
    return coerceConst(evaluateConstantExpr(expr), declType);
}

bool CodeGen::isUnfoldableBitfieldInit(const ExprPtr& expr) {
    auto* si = dynamic_cast<StructInitExpr*>(expr.get());
    return si && structLayout.count(resolveStructInitName(si->structName));
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
