#include "codegen.h"
#include "../ast/type_qual.h"

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// CodeGen — calls, template instantiation, intrinsics, function pointers,
// and explicit-allocator construction.
// Part of the codegen_expr.cpp split; all methods are CodeGen members (codegen.h).

void CodeGen::unifyTypeParam(std::string pattern, std::string concrete,
                             const std::set<std::string>& tps,
                             std::map<std::string, std::string>& subs) {
    auto stripStruct = [](std::string s) {
        return s.rfind("struct:", 0) == 0 ? s.substr(7) : s;
    };
    auto canon = [](std::string t) {           // move trailing '*' to leading
        int stars = 0;
        while (!t.empty() && t.back()  == '*') { t.pop_back();    stars++; }
        while (!t.empty() && t.front() == '*') { t = t.substr(1); stars++; }
        return std::string(stars, '*') + t;
    };
    pattern  = canon(stripStruct(pattern));
    concrete = canon(stripStruct(concrete));
    // strip matching leading '*' from both
    size_t pi = 0, ci = 0;
    while (pi < pattern.size() && pattern[pi] == '*' &&
           ci < concrete.size() && concrete[ci] == '*') { pi++; ci++; }
    pattern = pattern.substr(pi);
    concrete = stripStruct(concrete.substr(ci));
    if (pattern.empty() || concrete.empty()) return;

    if (tps.count(pattern)) {                   // bare type parameter
        if (!subs.count(pattern)) subs[pattern] = concrete;
        return;
    }
    size_t lt = pattern.find('<');              // Name<args> vs an instance
    if (lt == std::string::npos) return;
    auto [pbase, pargs] = splitTemplateType(pattern);
    std::string cbase; std::vector<std::string> cargs;
    if (concrete.find('<') != std::string::npos) {
        auto pr = splitTemplateType(concrete); cbase = pr.first; cargs = pr.second;
    } else {
        auto it = templateInstanceArgs.find(concrete);
        if (it != templateInstanceArgs.end()) { cbase = it->second.first; cargs = it->second.second; }
    }
    if (cbase != pbase) return;
    for (size_t i = 0; i < pargs.size() && i < cargs.size(); ++i)
        unifyTypeParam(pargs[i], cargs[i], tps, subs);
}

void CodeGen::ensureTemplateInstantiated(const std::string& mangled,
                                          const std::string& tname,
                                          const std::vector<std::string>& args) {
    templateInstanceArgs[mangled] = {tname, args};  // record for type-arg inference
    if (structTypes.count(mangled)) return;
    auto it = templateDecls.find(tname);
    if (it == templateDecls.end()) return;
    StructDecl* tmpl = it->second;

    auto& tp = tmpl->typeParams;
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < args.size(); ++i) subs[tp[i]] = args[i];

    std::vector<llvm::Type*> fieldTypes;
    std::vector<StructDecl::Field> fields;
    for (const auto& f : tmpl->fields) {
        std::string concrete = substType(f.type, subs);
        fieldTypes.push_back(getTypeFromString(concrete));
        fields.push_back({concrete, f.name});
    }
    // #pragma pack(N>=2): same manual layout as concrete structs.
    if (tmpl->packAlign >= 2) {
        std::vector<llvm::Type*> phys;
        std::map<std::string, BitfieldSlot> slots;
        buildPackedLayout(fields, (unsigned)tmpl->packAlign, phys, slots);
        structTypes[mangled]  = llvm::StructType::create(*context, phys, mangled, /*isPacked=*/true);
        structFields[mangled] = fields;
        structLayout[mangled] = slots;
        return;
    }
    llvm::StructType* st = llvm::StructType::create(*context, fieldTypes, mangled, tmpl->isPacked);
    structTypes[mangled] = st;
    structFields[mangled] = fields;
}

// Lower a call to an `intrinsic`-declared function to inline IR. The registry of
// supported intrinsics lives here; their signatures are declared in stdlib (e.g.
// stdlib/atomic.esk) and the orderings/semantics are fixed (docs/dev/async-design.md §3).
llvm::Value* CodeGen::lowerIntrinsicCall(const std::string& fn, CallExpr* node) {
    // Atomics operate on a *int (i32) cell.
    if (fn == "atomic_load") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        auto* ld = builder->CreateLoad(llvm::Type::getInt32Ty(*context), cell, "atm.load");
        ld->setAtomic(llvm::AtomicOrdering::Acquire);
        ld->setAlignment(llvm::Align(4));
        return ld;
    }
    if (fn == "atomic_store") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        llvm::Value* v    = evaluateExpr(node->args[1]);
        auto* st = builder->CreateStore(v, cell);
        st->setAtomic(llvm::AtomicOrdering::Release);
        st->setAlignment(llvm::Align(4));
        return llvm::UndefValue::get(llvm::Type::getVoidTy(*context));
    }
    if (fn == "atomic_swap") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        llvm::Value* v    = evaluateExpr(node->args[1]);
        // Alignment from the operand width, so a 64-bit cell isn't under-aligned.
        llvm::MaybeAlign al = module->getDataLayout().getABITypeAlign(v->getType());
        return builder->CreateAtomicRMW(
            llvm::AtomicRMWInst::Xchg, cell, v, al,
            llvm::AtomicOrdering::AcquireRelease);
    }
    if (fn == "atomic_cas") {
        llvm::Value* cell     = evaluateExpr(node->args[0]);
        llvm::Value* expected = evaluateExpr(node->args[1]);
        llvm::Value* desired  = evaluateExpr(node->args[2]);
        llvm::MaybeAlign al = module->getDataLayout().getABITypeAlign(desired->getType());
        llvm::Value* cx = builder->CreateAtomicCmpXchg(
            cell, expected, desired, al,
            llvm::AtomicOrdering::AcquireRelease, llvm::AtomicOrdering::Acquire);
        return builder->CreateExtractValue(cx, 1, "atm.cas.ok"); // success bit (i1)
    }
    throw std::runtime_error("intrinsic '" + fn + "' is declared but has no codegen lowering");
}

void CodeGen::visit(CallExpr* node) {
    // Variadic access: va_start(ap) / va_end(ap) — `ap` is a local va_list, whose
    // alloca is the pointer the intrinsics need.
    if (auto* bid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if ((bid->name == "va_start" || bid->name == "va_end") && node->args.size() == 1) {
            llvm::Value* ap = evaluateLValue(node->args[0]);
            llvm::Function* fn = getOrDeclareFunc(
                bid->name == "va_start" ? "llvm.va_start.p0" : "llvm.va_end.p0",
                llvm::Type::getVoidTy(*context), {llvm::PointerType::get(*context, 0)}, false);
            exprValueStack.push(builder->CreateCall(fn, {ap}));
            return;
        }
    }
    // Algebraic variant construction: `Circle(2.0)`, `Some(x)`.
    if (auto* vid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (!lookupSymbol(vid->name) && adtVariants.count(vid->name)) {
            exprValueStack.push(buildVariant(vid->name, node->args));
            return;
        }
        // Bare generic-variant with inference: infer each type param from the arg
        // whose payload slot IS that param (the type checker has verified all are
        // determined), then build the instance.
        if (!lookupSymbol(vid->name) && genericVariants.count(vid->name)) {
            auto& gi = genericVariants[vid->name];
            EnumDecl* ge = genericEnumDecls[gi.first];
            const auto& payload = ge->payloads[gi.second];
            std::map<std::string, std::string> subs;
            for (size_t i = 0; i < payload.size() && i < node->args.size(); ++i)
                for (const auto& tp : ge->typeParams)
                    if (payload[i] == tp && !subs.count(tp)) {
                        std::string at = getExprEskiuType(node->args[i]);
                        if (!typeParamOverride.empty()) at = substType(at, typeParamOverride);
                        subs[tp] = at;
                    }
            std::vector<std::string> targs;
            for (const auto& tp : ge->typeParams) targs.push_back(subs.count(tp) ? subs[tp] : "int");
            std::string mangled = ensureEnumInst(gi.first, targs);
            std::map<std::string, std::string> sub2;
            for (size_t i = 0; i < ge->typeParams.size() && i < targs.size(); ++i) sub2[ge->typeParams[i]] = targs[i];
            std::vector<llvm::Type*> fts;
            for (const auto& ft : payload) fts.push_back(getTypeFromString(substType(ft, sub2)));
            exprValueStack.push(buildEnumValue(structTypes[mangled], gi.second, fts, node->args));
            return;
        }
    }
    // Intrinsics: a call to an `intrinsic`-declared name lowers to inline IR
    // instead of a call. Gated on intrinsicNames (only populated when the
    // declaring module is imported), so a user function of the same name that
    // was *not* imported as an intrinsic is unaffected.
    if (auto* aid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        const std::string& fn = aid->name;
        if (intrinsicNames.count(fn) && !lookupSymbol(fn)) {
            exprValueStack.push(lowerIntrinsicCall(fn, node));
            return;
        }
    }

    // Template function called without explicit type arguments: infer each type
    // parameter by structurally unifying every parameter type against the concrete
    // argument type. Covers bare params (T max<T>(T a, T b) → max(3,5)) and
    // composite ones (T List_get<T>(List<T>* self, int i) → List_get(&nums, i)).
    if (auto* id = dynamic_cast<IdentExpr*>(node->callee.get())) {
        auto tIt = funcTemplateDecls.find(id->name);
        if (tIt != funcTemplateDecls.end()) {
            FunctionDecl* fd = tIt->second;
            std::set<std::string> tps(fd->typeParams.begin(), fd->typeParams.end());
            std::map<std::string, std::string> subs;
            for (size_t j = 0; j < fd->params.size() && j < node->args.size(); ++j)
                unifyTypeParam(fd->params[j].first, getExprEskiuType(node->args[j]), tps, subs);
            std::vector<std::string> typeArgs;
            for (const auto& tpName : fd->typeParams) {
                auto sIt = subs.find(tpName);
                if (sIt == subs.end()) break;
                typeArgs.push_back(sIt->second);
            }
            if (typeArgs.size() == fd->typeParams.size()) {
                TemplateCallExpr tc(id->name, typeArgs, node->args);
                visit(&tc);
                return;
            }
        }
    }

    // Unified method/interface call: callee is MemberExpr
    if (auto member = dynamic_cast<MemberExpr*>(node->callee.get())) {
        std::string baseType = getExprEskiuType(member->base);
        // Pointer-vs-value base: a pointer-to-struct base supplies the receiver
        // by its *value* (the pointer — load it), a value struct by its *address*.
        // (Mirrors the field-access logic; robust to parameters now living in a
        // stack slot.)
        bool baseIsPtr = (!baseType.empty() && (baseType.front() == '*' || baseType.back() == '*'));
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
        if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
        while (!baseType.empty() && baseType.back() == '*') baseType.pop_back();
        if (baseType.find('<') != std::string::npos) {
            auto [tn2, a2] = splitTemplateType(baseType);
            ensureTemplateInstantiated(mangleTemplate(baseType), tn2, a2);
            baseType = mangleTemplate(baseType);
        }

        // Interface vtable dispatch
        auto ifIt = ifaceMethodOrder.find(baseType);
        if (ifIt != ifaceMethodOrder.end()) {
            // An interface value IS a pointer to the fat {data, vtable} struct,
            // so its *value* (loaded from the variable's slot) is the fat pointer.
            llvm::Value* fatPtr = evaluateExpr(member->base);
            llvm::StructType* fatType = ifaceFatPtrTypes[baseType];
            llvm::Value* dataGEP = builder->CreateStructGEP(fatType, fatPtr, 0);
            llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataGEP);
            llvm::Value* vtGEP   = builder->CreateStructGEP(fatType, fatPtr, 1);
            llvm::Value* vtPtr   = builder->CreateLoad(llvm::PointerType::get(*context, 0), vtGEP);
            const auto& order = ifIt->second;
            size_t idx = 0;
            for (; idx < order.size(); ++idx) if (order[idx] == member->member) break;
            if (idx == order.size())
                throw std::runtime_error("Interface '" + baseType + "' has no method '" + member->member + "'");
            llvm::StructType* vtType = ifaceVtableTypes[baseType];
            llvm::Value* fnGEP = builder->CreateStructGEP(vtType, vtPtr, idx);
            llvm::Value* fnPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), fnGEP);
            std::vector<llvm::Value*> iargs = {dataPtr};
            for (auto& arg : node->args) iargs.push_back(evaluateExpr(arg));

            // Build the correct function type from stored method signature
            llvm::Type* retType = llvm::Type::getVoidTy(*context);
            auto& retTypes  = ifaceMethodReturnTypes[baseType];
            auto& paramLists = ifaceMethodParamEskiuTypes[baseType];
            if (idx < retTypes.size()) retType = getTypeFromString(retTypes[idx]);

            std::vector<llvm::Type*> paramLLVM = {llvm::PointerType::get(*context, 0)}; // self
            if (idx < paramLists.size())
                for (const auto& pt : paramLists[idx])
                    paramLLVM.push_back(getTypeFromString(pt));
            // Pad remaining args as ptr if count doesn't match (variadic safety)
            while (paramLLVM.size() < iargs.size())
                paramLLVM.push_back(llvm::PointerType::get(*context, 0));

            // Use sret if return type is a large aggregate
            bool iSret = needsSret(retType);
            llvm::Value* sretBuf = nullptr;
            if (iSret) {
                sretBuf = entryAlloca(retType, nullptr, "iface.sret");
                iargs.insert(iargs.begin(), sretBuf); // sret pointer goes first
                paramLLVM.insert(paramLLVM.begin(), llvm::PointerType::get(*context, 0));
                retType = llvm::Type::getVoidTy(*context);
            }

            auto* ftype = llvm::FunctionType::get(retType, paramLLVM, false);
            llvm::Value* call = builder->CreateCall(ftype, fnPtr, iargs);
            if (iSret)
                exprValueStack.push(builder->CreateLoad(
                    llvm::cast<llvm::StructType>(getTypeFromString(retTypes[idx])), sretBuf));
            else
                exprValueStack.push(call);
            return;
        }

        // Struct method call
        std::string mangled = baseType + "_" + member->member;
        llvm::Function* mfunc = module->getFunction(mangled);
        if (mfunc) {
            // self: a value-struct receiver passes its address; a pointer receiver
            // passes the pointer it holds (loaded), not the address of its slot.
            llvm::Value* self = baseIsPtr ? evaluateExpr(member->base)
                                          : evaluateLValue(member->base);
            std::vector<llvm::Value*> margs = {self};
            for (auto& arg : node->args) margs.push_back(evaluateExpr(arg));
            exprValueStack.push(builder->CreateCall(mfunc, margs));
            return;
        }
        // Free-function constraint satisfaction: `t.m(x)` on a PRIMITIVE receiver
        // (a bounded type param T:Iface satisfied by a free fn, e.g. `int cmp(int,int)`
        // satisfies `Ord` for int) lowers to `m(t, x)` — the receiver is the first
        // argument, passed by value. Gated to scalar primitives so a mistyped struct
        // method still errors instead of silently hitting a same-named global fn.
        static const std::set<std::string> kScalarPrims = {
            "int","int8","int16","int32","int64","uint","uint8","uint16","uint32",
            "uint64","char","bool","float","double"};
        if (kScalarPrims.count(baseType)) {
            if (llvm::Function* ffunc = module->getFunction(member->member)) {
                std::vector<llvm::Value*> fargs = {evaluateExpr(member->base)};
                for (auto& arg : node->args) fargs.push_back(evaluateExpr(arg));
                llvm::FunctionType* fty = ffunc->getFunctionType();
                for (size_t i = 0; i < fargs.size() && i < fty->getNumParams(); ++i) {
                    llvm::Type* pt = fty->getParamType(i);
                    if (fargs[i]->getType()->isIntegerTy() && pt->isIntegerTy() &&
                        fargs[i]->getType() != pt) {
                        std::string srcTy = (i == 0) ? baseType
                                                     : getExprEskiuType(node->args[i - 1]);
                        fargs[i] = coerceInt(fargs[i], pt, eskiuUnsigned(srcTy));
                    }
                }
                exprValueStack.push(builder->CreateCall(ffunc, fargs));
                return;
            }
        }
        // Not a method: if o.member is a fn-pointer field, fall through to the
        // general indirect-call path (evaluateExpr(callee) yields the fat ptr).
        std::string ft = getExprEskiuType(node->callee);
        if (!(ft.size() > 3 && ft.substr(0, 3) == "fn("))
            throw std::runtime_error("Undefined method: " + baseType + "::" + member->member);
    }

    // A bare name that resolves to a function (and is not shadowed by a local
    // fn-pointer variable) is a direct call — use the function itself, not the
    // decayed closure fat pointer that evaluating it as a value would produce.
    llvm::Value* calleeVal = nullptr;
    if (auto ident = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (!lookupSymbol(ident->name))
            calleeVal = module->getFunction(ident->name);
    }
    if (!calleeVal) calleeVal = evaluateExpr(node->callee);
    if (!calleeVal) throw std::runtime_error("Call target is null");

    // Indirect call through a fat-pointer closure {fn_ptr, env_ptr}
    if (!llvm::isa<llvm::Function>(calleeVal)) {
        std::string eskiuType = getExprEskiuType(node->callee);
        if (eskiuType.size() > 3 && eskiuType.substr(0, 3) == "fn(") {
            // Extract params and return type from "fn(T,...)->R"
            size_t rp = eskiuType.find(")->");
            std::string paramStr = eskiuType.substr(3, rp - 3);
            std::string retStr   = eskiuType.substr(rp + 3);
            std::vector<llvm::Type*> pts;
            llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
            pts.push_back(ptrTy); // env* always first
            if (!paramStr.empty()) {
                size_t pos = 0;
                while (pos < paramStr.size()) {
                    size_t comma = paramStr.find(',', pos);
                    if (comma == std::string::npos) comma = paramStr.size();
                    pts.push_back(getTypeFromString(paramStr.substr(pos, comma - pos)));
                    pos = comma + 1;
                }
            }
            llvm::Type* retTy = getTypeFromString(retStr);
            llvm::FunctionType* fty = llvm::FunctionType::get(retTy, pts, false);

            // Extract fn_ptr and env_ptr from the fat pointer struct
            llvm::StructType* fatTy = llvm::cast<llvm::StructType>(calleeVal->getType());
            llvm::Value* fnPtr  = builder->CreateExtractValue(calleeVal, {0}, "fn.ptr");
            llvm::Value* envPtr = builder->CreateExtractValue(calleeVal, {1}, "env.ptr");

            std::vector<llvm::Value*> iargs = {envPtr};
            for (size_t i = 0; i < node->args.size(); ++i) {
                llvm::Value* av = evaluateExpr(node->args[i]);
                size_t pidx = i + 1;   // env pointer is param 0
                if (pidx < pts.size() && av->getType()->isIntegerTy()
                        && pts[pidx]->isIntegerTy() && av->getType() != pts[pidx]) {
                    av = coerceInt(av, pts[pidx],
                                   eskiuUnsigned(getExprEskiuType(node->args[i])));
                }
                iargs.push_back(av);
            }
            // A void-returning call must not be given a name (LLVM forbids it).
            exprValueStack.push(builder->CreateCall(
                fty, fnPtr, iargs, retTy->isVoidTy() ? "" : "fn.call"));
            return;
        }
        throw std::runtime_error("Call target is not a function");
    }
    llvm::Function* func = llvm::cast<llvm::Function>(calleeVal);

    // Evaluate args, boxing structs as interfaces where the param type demands it
    std::vector<llvm::Value*> args;
    auto ptIt = funcEskiuParamTypes.find(func->getName().str());
    for (size_t i = 0; i < node->args.size(); ++i) {
        bool boxed = false;
        if (ptIt != funcEskiuParamTypes.end() && i < ptIt->second.size()) {
            const std::string& ep = ptIt->second[i];
            if (ifaceFatPtrTypes.count(ep)) {
                // Param expects an interface — evaluate arg as pointer and box it
                std::string argType = getExprEskiuType(node->args[i]);
                if (!argType.empty() && argType.front() == '*') argType = argType.substr(1);
                if (argType.size() > 7 && argType.substr(0, 7) == "struct:") argType = argType.substr(7);
                while (!argType.empty() && argType.back() == '*') argType.pop_back();
                llvm::Value* sPtr = evaluateExpr(node->args[i]); // &struct → ptr
                args.push_back(boxAsInterface(ep, argType, sPtr));
                boxed = true;
            }
        }
        if (!boxed) args.push_back(evaluateExpr(node->args[i]));
    }

    // Widen/truncate integer arguments to match function parameter types. If the
    // callee returns via sret, its real params start at index 1 (param 0 is the
    // hidden sret pointer, prepended to `args` only later), so align with that
    // offset — otherwise the first scalar arg is matched against the sret pointer
    // and an int literal is left unwidened (caught by the IR verifier).
    {
        auto fparams = func->getFunctionType()->params();
        unsigned pbase = funcSretTypes.count(func->getName().str()) ? 1u : 0u;
        for (size_t i = 0; i < args.size() && i + pbase < fparams.size(); ++i) {
            llvm::Type* pt = fparams[i + pbase];
            if (args[i]->getType()->isIntegerTy() && pt->isIntegerTy()
                    && args[i]->getType() != pt) {
                bool uns = i < node->args.size() && eskiuUnsigned(getExprEskiuType(node->args[i]));
                args[i] = coerceInt(args[i], pt, uns);
            }
        }
    }

    // C default argument promotions for the variadic ("...") arguments: an
    // integer narrower than int widens to i32 (sign/zero per its signedness),
    // and a float widens to double. Without this, printf("%d", anInt8) reads a
    // full int from a byte-sized argument slot.
    if (func->getFunctionType()->isVarArg()) {
        auto argUnsigned = [&](size_t i) -> bool {
            if (i >= node->args.size()) return false;
            return eskiuUnsigned(getExprEskiuType(node->args[i]));
        };
        unsigned fixed = func->getFunctionType()->getNumParams();
        llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
        for (size_t i = fixed; i < args.size(); ++i) {
            llvm::Type* at = args[i]->getType();
            if (at->isIntegerTy() && at->getIntegerBitWidth() < 32) {
                // i1 (a bool / comparison result) is 0/1 — always zero-extend.
                bool uns = argUnsigned(i) || at->getIntegerBitWidth() == 1;
                args[i] = uns ? builder->CreateZExt(args[i], i32)
                              : builder->CreateSExt(args[i], i32);
            } else if (at->isFloatTy()) {
                args[i] = builder->CreateFPExt(args[i], llvm::Type::getDoubleTy(*context));
            }
        }
    }

    // sret: alloca for large struct return, pass as hidden arg 0, load result
    auto sretIt = funcSretTypes.find(func->getName().str());
    if (sretIt != funcSretTypes.end()) {
        llvm::Value* sretAlloca = entryAlloca(sretIt->second, nullptr, "sret.tmp");
        args.insert(args.begin(), sretAlloca);
        createMaybeInvoke(func->getFunctionType(), func, args);
        exprValueStack.push(builder->CreateLoad(sretIt->second, sretAlloca));
    } else {
        exprValueStack.push(
            createMaybeInvoke(func->getFunctionType(), func, args));
    }
}

llvm::Function* CodeGen::getOrDeclareFunc(const std::string& name, llvm::Type* retType,
                                           std::vector<llvm::Type*> paramTypes, bool isVarArg) {
    llvm::Function* f = module->getFunction(name);
    if (!f) {
        auto* ft = llvm::FunctionType::get(retType, paramTypes, isVarArg);
        f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }
    return f;
}

void CodeGen::visit(AllocWithExpr* node) {
    // Lower to: (*T) <AllocType>_alloc(allocator, count * sizeof(T))
    // The allocator is a struct providing `*void alloc(... size)`.
    llvm::Value* allocPtr = evaluateExpr(node->allocator);

    std::string at = getExprEskiuType(node->allocator);
    while (!at.empty() && at.front() == '*') at = at.substr(1);
    while (!at.empty() && at.back()  == '*') at.pop_back();
    if (at.rfind("struct:", 0) == 0) at = at.substr(7);

    std::string fnName = at + "_alloc";
    llvm::Function* af = module->getFunction(fnName);
    if (!af)
        throw std::runtime_error("alloc_with: allocator type '" + at +
                                 "' has no alloc method (" + fnName + ")");

    llvm::Type* elemTy = getTypeFromString(node->elemType);
    uint64_t esz = module->getDataLayout().getTypeAllocSize(elemTy);
    llvm::Value* n64 = builder->CreateIntCast(evaluateExpr(node->count),
                            llvm::Type::getInt64Ty(*context), false);
    llvm::Value* total = builder->CreateMul(
        n64, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), esz), "allocw.size");

    // Coerce the size to the alloc method's second parameter type.
    llvm::FunctionType* fty = af->getFunctionType();
    if (fty->getNumParams() >= 2 && fty->getParamType(1) != total->getType())
        total = builder->CreateIntCast(total, fty->getParamType(1), false);

    // Returns *void; the cast to *T is a no-op under opaque pointers.
    exprValueStack.push(builder->CreateCall(af, {allocPtr, total}, "allocw.ptr"));
}

llvm::Value* CodeGen::makeFunctionPointer(llvm::Function* target) {
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    std::string wname = "__fnptr_" + target->getName().str();
    llvm::Function* wrapper = module->getFunction(wname);
    if (!wrapper) {
        // Thunk: (env*, params...) -> ret  that ignores env and calls target.
        llvm::FunctionType* tfty = target->getFunctionType();
        std::vector<llvm::Type*> wparams;
        wparams.push_back(ptrTy);  // env (unused)
        for (llvm::Type* pt : tfty->params()) wparams.push_back(pt);
        llvm::FunctionType* wfty = llvm::FunctionType::get(
            tfty->getReturnType(), wparams, tfty->isVarArg());
        wrapper = llvm::Function::Create(wfty, llvm::Function::InternalLinkage,
                                         wname, module.get());

        llvm::BasicBlock* prev = builder->GetInsertBlock();
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", wrapper);
        builder->SetInsertPoint(entry);
        std::vector<llvm::Value*> callArgs;
        auto ai = wrapper->arg_begin(); ++ai;  // skip env
        for (; ai != wrapper->arg_end(); ++ai) callArgs.push_back(&*ai);
        llvm::Value* r = builder->CreateCall(target, callArgs);
        if (tfty->getReturnType()->isVoidTy()) builder->CreateRetVoid();
        else builder->CreateRet(r);
        if (prev) builder->SetInsertPoint(prev);
    }

    // Build fat pointer {wrapper, null} — same shape lambdas produce.
    llvm::StructType* fatTy = llvm::cast<llvm::StructType>(getTypeFromString("fn()->void"));
    llvm::Value* fatAlloca = entryAlloca(fatTy, nullptr, "fnptr.fat");
    builder->CreateStore(wrapper, builder->CreateStructGEP(fatTy, fatAlloca, 0));
    builder->CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
                         builder->CreateStructGEP(fatTy, fatAlloca, 1));
    return builder->CreateLoad(fatTy, fatAlloca, "fnptr.fat.val");
}

void CodeGen::visit(TemplateCallExpr* node) {
    // Variadic access: va_arg<T>(ap) -> next argument of type T.
    if (node->templateName == "va_arg" && node->args.size() == 1 && node->typeArgs.size() == 1) {
        llvm::Value* ap = evaluateLValue(node->args[0]);
        std::string t = typeParamOverride.empty() ? node->typeArgs[0]
                                                   : substType(node->typeArgs[0], typeParamOverride);
        exprValueStack.push(builder->CreateVAArg(ap, getTypeFromString(t), "va.arg"));
        return;
    }
    // Generic algebraic-variant construction: `Some<int>(5)`, `Left<A,B>(x)`. Type
    // args resolve through the enclosing template's substitutions (so `Left<A,B>`
    // inside select2<A,B> becomes Left<int,int> at instantiation).
    if (genericVariants.count(node->templateName)) {
        auto& gi = genericVariants[node->templateName];      // (genericName, tag)
        std::vector<std::string> args;
        for (const auto& t : node->typeArgs)
            args.push_back(typeParamOverride.empty() ? t : substType(t, typeParamOverride));
        std::string mangled = ensureEnumInst(gi.first, args);
        EnumDecl* ge = genericEnumDecls[gi.first];
        std::map<std::string, std::string> subs;
        for (size_t i = 0; i < ge->typeParams.size() && i < args.size(); ++i)
            subs[ge->typeParams[i]] = args[i];
        std::vector<llvm::Type*> fts;
        for (const auto& ft : ge->payloads[gi.second]) fts.push_back(getTypeFromString(substType(ft, subs)));
        exprValueStack.push(buildEnumValue(structTypes[mangled], gi.second, fts, node->args));
        return;
    }
    auto templ = funcTemplateDecls.find(node->templateName);
    if (templ == funcTemplateDecls.end())
        throw std::runtime_error("Unknown template function: " + node->templateName);

    FunctionDecl* fd = templ->second;
    auto& tp = fd->typeParams;
    // Resolve each explicit type argument through the enclosing template's active
    // substitutions. When this call appears inside another template body (e.g.
    // `mk<T>(n)` inside `esz<T>`), node->typeArgs holds the literal param name
    // "T"; without this it would instantiate `mk_T` (T unresolved → i32). We must
    // not mutate node->typeArgs — the same node is re-visited per instantiation.
    auto resolveArg = [&](const std::string& t) {
        return typeParamOverride.empty() ? t : substType(t, typeParamOverride);
    };
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = resolveArg(node->typeArgs[i]);

    // Mangle the instantiated function name
    std::string mangledName = node->templateName;
    for (const auto& t : node->typeArgs) mangledName += "_" + mangleTemplate(resolveArg(t));

    // Instantiate if not already in module.
    // Save/restore the insert point — we may be inside another function's body.
    if (!module->getFunction(mangledName)) {
        llvm::BasicBlock*          savedBB         = builder->GetInsertBlock();
        llvm::BasicBlock::iterator savedPoint      = builder->GetInsertPoint();
        llvm::Function*            savedFunc       = currentFunction;
        llvm::Value*               savedSretParam  = currentSretParam;
        // Restore (not clear) the override: this call may be nested inside another
        // template body whose substitutions must survive the inner instantiation.
        auto                       savedOverride   = typeParamOverride;

        typeParamOverride = subs;
        auto inst = std::make_shared<FunctionDecl>(mangledName, fd->returnType, fd->params, fd->body);
        inst->accept(this);
        typeParamOverride = savedOverride;

        // Restore caller's context
        currentFunction  = savedFunc;
        currentSretParam = savedSretParam;
        if (savedBB) builder->SetInsertPoint(savedBB, savedPoint);
    }

    llvm::Function* func = module->getFunction(mangledName);
    if (!func) throw std::runtime_error("Template instantiation failed: " + mangledName);

    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) args.push_back(evaluateExpr(arg));

    // Coerce arguments to the instantiated function's parameter types — e.g. a
    // `double` literal passed where T=float substituted the parameter to `float`.
    llvm::FunctionType* fty = func->getFunctionType();
    unsigned pbase = funcSretTypes.count(mangledName) ? 1u : 0u; // skip hidden sret ptr
    for (size_t i = 0; i < args.size(); ++i) {
        unsigned pidx = pbase + (unsigned)i;
        if (pidx >= fty->getNumParams()) break;
        llvm::Type* pt = fty->getParamType(pidx);
        llvm::Value* v = args[i];
        if (!v || v->getType() == pt) continue;
        if (v->getType()->isFloatingPointTy() && pt->isFloatingPointTy())
            args[i] = builder->CreateFPCast(v, pt);
        else if (v->getType()->isIntegerTy() && pt->isIntegerTy())
            args[i] = coerceInt(v, pt, i < node->args.size() && eskiuUnsigned(getExprEskiuType(node->args[i])));
        else if (v->getType()->isIntegerTy() && pt->isFloatingPointTy())
            args[i] = intToFloat(v, pt, i < node->args.size() && eskiuUnsigned(getExprEskiuType(node->args[i])));
        else if (v->getType()->isFloatingPointTy() && pt->isIntegerTy())
            args[i] = builder->CreateFPToSI(v, pt);
    }

    auto sretIt = funcSretTypes.find(mangledName);
    if (sretIt != funcSretTypes.end()) {
        llvm::Value* sretAlloca = entryAlloca(sretIt->second, nullptr, "sret.tmp");
        args.insert(args.begin(), sretAlloca);
        builder->CreateCall(func, args);
        exprValueStack.push(builder->CreateLoad(sretIt->second, sretAlloca));
    } else {
        exprValueStack.push(builder->CreateCall(func, args));
    }
}
