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
