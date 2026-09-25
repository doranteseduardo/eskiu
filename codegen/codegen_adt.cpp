#include "codegen.h"
#include "../ast/type_qual.h"

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// CodeGen — algebraic enums (construction + monomorphization), struct
// initialization, and interface boxing.
// Part of the codegen_expr.cpp split; see codegen.h.

// Create the tagged-union LLVM type for an ADT enum given each variant's payload
// field types: { i32 tag; [N x i64] payload }, N sized to the largest variant. The
// i64 array forces 8-byte alignment (enough for scalars/pointers/doubles).
static llvm::StructType* makeAdtStruct(llvm::LLVMContext& ctx, const llvm::DataLayout& DL,
                                       const std::string& name,
                                       const std::vector<std::vector<llvm::Type*>>& variantFields) {
    uint64_t maxBytes = 0;
    for (const auto& fields : variantFields) {
        uint64_t bytes = 0;
        for (llvm::Type* t : fields) {
            uint64_t al = DL.getABITypeAlign(t).value();
            bytes = ((bytes + al - 1) / al) * al + DL.getTypeAllocSize(t);
        }
        if (bytes > maxBytes) maxBytes = bytes;
    }
    uint64_t n = (maxBytes + 7) / 8; if (n == 0) n = 1;
    std::vector<llvm::Type*> f = {
        llvm::Type::getInt32Ty(ctx),
        llvm::ArrayType::get(llvm::Type::getInt64Ty(ctx), n)
    };
    return llvm::StructType::create(ctx, f, name);
}

void CodeGen::visit(EnumDecl* node) {
    enumTypes.insert(node->name);
    if (!node->isADT()) {
        for (const auto& m : node->members) enumConstants[m.first] = m.second;
        plainEnumDecls[node->name] = node;   // classic int enum: `match` lowers to a value switch
        return;
    }
    if (!node->typeParams.empty()) {
        // Generic ADT enum: a template; instances are built on demand (ensureEnumInst).
        genericEnumDecls[node->name] = node;
        for (size_t v = 0; v < node->members.size(); ++v)
            genericVariants[node->members[v].first] = {node->name, (int)v};
        return;
    }
    adtEnumDecls[node->name] = node;
    std::vector<std::vector<llvm::Type*>> vf;
    for (size_t v = 0; v < node->members.size(); ++v) {
        adtVariants[node->members[v].first] = {node->name, (int)v};
        std::vector<llvm::Type*> fields;
        for (const auto& ft : node->payloads[v]) fields.push_back(getTypeFromString(ft));
        vf.push_back(fields);
    }
    structTypes[node->name] = makeAdtStruct(*context, module->getDataLayout(), node->name, vf);
}

// Monomorphize a generic enum for `typeArgs` (build its struct + record the args).
std::string CodeGen::ensureEnumInst(const std::string& genericName,
                                    const std::vector<std::string>& typeArgs) {
    std::string inst = genericName + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) { if (i) inst += ","; inst += typeArgs[i]; }
    inst += ">";
    std::string mangled = mangleTemplate(inst);
    if (structTypes.count(mangled)) return mangled;
    EnumDecl* ge = genericEnumDecls[genericName];
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < ge->typeParams.size() && i < typeArgs.size(); ++i)
        subs[ge->typeParams[i]] = typeArgs[i];
    std::vector<std::vector<llvm::Type*>> vf;
    for (size_t v = 0; v < ge->members.size(); ++v) {
        std::vector<llvm::Type*> fields;
        for (const auto& ft : ge->payloads[v]) fields.push_back(getTypeFromString(substType(ft, subs)));
        vf.push_back(fields);
    }
    structTypes[mangled] = makeAdtStruct(*context, module->getDataLayout(), mangled, vf);
    enumInstanceArgs[mangled] = {genericName, typeArgs};
    return mangled;
}

// Core builder: alloca the enum struct, store the tag, write payload fields (viewed
// as the variant's struct, coerced to the field types), then load the value.
llvm::Value* CodeGen::buildEnumValue(llvm::StructType* et, int tag,
        const std::vector<llvm::Type*>& fieldTypes, const std::vector<ExprPtr>& args) {
    llvm::Value* tmp = entryAlloca(et, nullptr, "variant.tmp");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), tag),
                         builder->CreateStructGEP(et, tmp, 0));
    if (!fieldTypes.empty()) {
        llvm::StructType* vt = llvm::StructType::get(*context, fieldTypes);
        llvm::Value* pay = builder->CreateStructGEP(et, tmp, 1);   // the [N x i64] area
        for (size_t i = 0; i < args.size() && i < fieldTypes.size(); ++i) {
            llvm::Value* fp = builder->CreateStructGEP(vt, pay, i);
            llvm::Value* val = evaluateExpr(args[i]);
            llvm::Type* ft = fieldTypes[i];
            val = coerceValue(val, ft, eskiuUnsigned(getExprEskiuType(args[i])));  // arg to field type
            builder->CreateStore(val, fp);
        }
    }
    return builder->CreateLoad(et, tmp, "variant.val");
}

llvm::Value* CodeGen::buildVariant(const std::string& variant, const std::vector<ExprPtr>& args) {
    auto& info = adtVariants[variant];
    EnumDecl* ed = adtEnumDecls[info.first];
    std::vector<llvm::Type*> fts;
    for (const auto& ft : ed->payloads[info.second]) fts.push_back(getTypeFromString(ft));
    return buildEnumValue(structTypes[info.first], info.second, fts, args);
}

std::string CodeGen::resolveStructInitName(const std::string& name) {
    if (name.find('<') == std::string::npos) return name;
    // Resolve type args through the enclosing template's substitutions, so a
    // `Pair<A,B>{...}` literal inside a template body instantiates Pair<int,int>,
    // not a bogus Pair_A_B.
    std::string resolved = typeParamOverride.empty() ? name : substType(name, typeParamOverride);
    auto [tn, args] = splitTemplateType(resolved);
    std::string mangled = mangleTemplate(resolved);
    ensureTemplateInstantiated(mangled, tn, args);
    return mangled;
}

void CodeGen::emitArrayInitInto(llvm::Value* dest, ArrayLitExpr* lit, const std::string& arrType) {
    // Peel the outer (leftmost) dimension: `int[2][3]` → dim 2, element `int[3]`.
    ty::Type bt = ty::Type::parse(arrType);
    if (bt.kind != ty::Type::Kind::Array) return;
    std::string elemStr = bt.elem->str();
    uint64_t n = 0;
    if (!resolveArrayDim(bt.dim, n)) n = lit->elements.size();
    llvm::Type* elemTy = getTypeFromString(elemStr);
    llvm::Type* arrTy = llvm::ArrayType::get(elemTy, n);
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    bool elemIsArray = (bt.elem->kind == ty::Type::Kind::Array);

    for (uint64_t i = 0; i < n; ++i) {
        llvm::Value* slot = builder->CreateGEP(arrTy, dest,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, i)});
        if (i < lit->elements.size()) {
            // A nested initializer for a sub-array recurses into the row; a scalar
            // element is evaluated and coerced to the element type.
            if (elemIsArray) {
                if (auto* sub = dynamic_cast<ArrayLitExpr*>(lit->elements[i].get()))
                    emitArrayInitInto(slot, sub, elemStr);
                else
                    builder->CreateStore(evaluateExpr(lit->elements[i]), slot);
                continue;
            }
            llvm::Value* val = evaluateExpr(lit->elements[i]);
            if (val->getType() != elemTy) {
                val = coerceValue(val, elemTy, eskiuUnsigned(getExprEskiuType(lit->elements[i])));
            }
            builder->CreateStore(val, slot);
        } else {
            builder->CreateStore(llvm::Constant::getNullValue(elemTy), slot);   // zero-fill (C-style)
        }
    }
}

void CodeGen::emitStructInitInto(llvm::Value* dest, StructInitExpr* init) {
    std::string sname = resolveStructInitName(init->structName);
    auto fit = structFields.find(sname);
    if (fit == structFields.end()) return;
    const auto& fields = fit->second;
    llvm::StructType* st = structTypes[sname];

    bool named = !init->fieldInits.empty() && !init->fieldInits[0].first.empty();

    auto coerce = [&](llvm::Value* val, llvm::Type* fieldType, bool unsignedSrc) -> llvm::Value* {
        return coerceValue(val, fieldType, unsignedSrc);
    };

    auto storeField = [&](size_t idx, ExprPtr expr) {
        llvm::Value* val = evalForType(expr, fields[idx].type);
        bool uns = eskiuUnsigned(getExprEskiuType(expr));
        // Bitfield-layout struct: store via the physical slot.
        auto lit = structLayout.find(sname);
        if (lit != structLayout.end()) {
            const BitfieldSlot& slot = lit->second.at(fields[idx].name);
            llvm::Value* gep = builder->CreateStructGEP(st, dest, slot.physIndex);
            if (slot.isBitfield) { storeBitfieldInto(gep, slot, val); return; }
            if (val) builder->CreateStore(coerce(val, slot.storageType, uns), gep);
            return;
        }
        llvm::Type* fieldType = getTypeFromString(fields[idx].type);
        val = coerce(val, fieldType, uns);
        llvm::Value* gep = builder->CreateStructGEP(st, dest, idx);
        if (val) builder->CreateStore(val, gep);
    };

    if (named) {
        for (const auto& [fname, expr] : init->fieldInits) {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].name == fname) { storeField(i, expr); break; }
            }
        }
    } else {
        for (size_t i = 0; i < init->fieldInits.size() && i < fields.size(); ++i) {
            storeField(i, init->fieldInits[i].second);
        }
    }
}

void CodeGen::visit(ArrayLitExpr* node) {
    (void)node;
    throw std::runtime_error("an array literal '{...}' is only valid as a variable initializer");
}

void CodeGen::visit(StructInitExpr* node) {
    std::string sname = resolveStructInitName(node->structName);
    auto fit = structFields.find(sname);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct: " + node->structName);
    llvm::StructType* st = structTypes[sname];
    // Temporary alloca — filled then loaded so caller can store it anywhere
    llvm::Value* tmp = entryAlloca(st, nullptr, sname + ".init");
    emitStructInitInto(tmp, node);
    exprValueStack.push(builder->CreateLoad(st, tmp));
}

void CodeGen::visit(InterfaceDecl* node) {
    // Build vtable struct type: %I_vtable = type { ptr, ptr, ... }
    std::vector<llvm::Type*> fnPtrs(node->methods.size(),
                                     llvm::PointerType::get(*context, 0));
    std::string vtName = node->name + "_vtable";
    llvm::StructType* vtType = llvm::StructType::create(*context, fnPtrs, vtName);
    ifaceVtableTypes[node->name] = vtType;

    // Method order + return/param type info for typed dispatch
    std::vector<std::string> order;
    std::vector<std::string> retTypes;
    std::vector<std::vector<std::string>> paramTypesList;
    for (const auto& m : node->methods) {
        order.push_back(m.name);
        retTypes.push_back(m.returnType);
        std::vector<std::string> pts;
        for (const auto& p : m.params) pts.push_back(p.first);
        paramTypesList.push_back(pts);
    }
    ifaceMethodOrder[node->name]           = order;
    ifaceMethodReturnTypes[node->name]      = retTypes;
    ifaceMethodParamEskiuTypes[node->name]  = paramTypesList;

    // Fat pointer type: %I_fat = type { ptr, ptr }. An interface value IS this struct,
    // by value (like a closure), so it can live in a local, a field, or a return value
    // without pointing into some caller's stack frame.
    llvm::StructType* fatPtr = llvm::StructType::create(*context,
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0)},
        node->name + "_fat");
    ifaceFatPtrTypes[node->name] = fatPtr;
}

// Create a fat pointer {data_ptr, vtable_ptr} for struct S implementing interface I
llvm::Value* CodeGen::boxAsInterface(const std::string& ifaceName,
                                      const std::string& structName,
                                      llvm::Value* structPtr) {
    auto vtIt = ifaceVtableTypes.find(ifaceName);
    if (vtIt == ifaceVtableTypes.end())
        throw std::runtime_error("Unknown interface: " + ifaceName);

    const auto& methods = ifaceMethodOrder[ifaceName];
    llvm::StructType* vtType = vtIt->second;
    llvm::StructType* fatType = ifaceFatPtrTypes[ifaceName];

    // Build vtable constant: { &S_method1, &S_method2, ... }
    std::string vtGlobName = ifaceName + "_vtable_" + structName;
    llvm::GlobalVariable* vtGlob = module->getGlobalVariable(vtGlobName);
    if (!vtGlob) {
        std::vector<llvm::Constant*> entries;
        for (const auto& mname : methods) {
            std::string mangled = structName + "_" + mname;
            llvm::Function* fn = module->getFunction(mangled);
            if (!fn) throw std::runtime_error("Method not found: " + mangled);
            entries.push_back(fn);
        }
        llvm::Constant* vtInit = llvm::ConstantStruct::get(vtType, entries);
        vtGlob = new llvm::GlobalVariable(*module, vtType, true,
            llvm::GlobalValue::PrivateLinkage, vtInit, vtGlobName);
    }

    // The fat pointer value { data ptr, vtable ptr }.
    llvm::Value* fat = llvm::UndefValue::get(fatType);
    fat = builder->CreateInsertValue(fat, structPtr, {0});
    fat = builder->CreateInsertValue(fat, vtGlob, {1});
    return fat;
}

std::string CodeGen::interfaceName(const std::string& type) const {
    std::string t = tyq::strip(type);
    if (t.rfind("interface:", 0) == 0) t = t.substr(10);
    return ifaceFatPtrTypes.count(t) ? t : "";
}

llvm::Value* CodeGen::evalForType(const ExprPtr& e, const std::string& targetType) {
    std::string iface = interfaceName(targetType);
    if (iface.empty()) return evaluateExpr(e);
    // Converting to an interface: an interface value passes through; `null` is the
    // empty {null, null}; a pointer to a struct is boxed with that struct's vtable.
    std::string st = getExprEskiuType(e);
    if (interfaceName(st) == iface) return evaluateExpr(e);
    if (auto* lit = dynamic_cast<LiteralExpr*>(e.get()); lit && lit->kind == LiteralExpr::Kind::NULL_VAL)
        return llvm::ConstantAggregateZero::get(ifaceFatPtrTypes[iface]);
    bool isPtr = !st.empty() && (st.front() == '*' || st.back() == '*');
    if (!isPtr)
        throw std::runtime_error("cannot convert a '" + st + "' value to interface '" + iface +
                                 "'; pass a pointer (&x)");
    std::string sname = st;
    while (!sname.empty() && sname.front() == '*') sname = sname.substr(1);
    while (!sname.empty() && sname.back() == '*') sname.pop_back();
    return boxAsInterface(iface, stripToStructKey(sname), evaluateExpr(e));
}
