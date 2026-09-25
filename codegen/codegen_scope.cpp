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
    return foldViaCodegen(expr, nullptr);
}

llvm::Constant* CodeGen::foldViaCodegen(const ExprPtr& expr, llvm::Type* targetTy) {
    // The IRBuilder constant-folds an operation on constant operands instead of emitting
    // an instruction, so running the normal expression codegen over a constant tree
    // yields an llvm::Constant. It runs in a throwaway function (so a non-constant
    // operand has somewhere to emit into) that is deleted afterwards.
    llvm::IRBuilderBase::InsertPointGuard guard(*builder);
    llvm::Function* savedFn = currentFunction;
    size_t depth = exprValueStack.size();
    auto* fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), false);
    auto* scratch = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                           "__eskiu.consteval", module.get());
    builder->SetInsertPoint(llvm::BasicBlock::Create(*context, "entry", scratch));
    currentFunction = scratch;
    ++constEvalDepth;
    llvm::Value* v = nullptr;
    try {
        v = evaluateExpr(expr);
        if (v && targetTy) v = coerceValue(v, targetTy, eskiuUnsigned(getExprEskiuType(expr)));
    } catch (const std::exception&) {
        v = nullptr;
    }
    --constEvalDepth;
    while (exprValueStack.size() > depth) exprValueStack.pop();
    currentFunction = savedFn;
    auto* c = llvm::dyn_cast_or_null<llvm::Constant>(v);
    scratch->eraseFromParent();
    if (c && targetTy && c->getType() != targetTy) return nullptr;
    return c;
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
    if (dynamic_cast<LambdaExpr*>(expr.get())) {
        llvm::Constant* c = evaluateConstantExpr(expr);
        return (c && c->getType() == declType) ? c : nullptr;
    }
    return foldViaCodegen(expr, declType);
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
