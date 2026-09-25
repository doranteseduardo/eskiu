#include "codegen.h"
#include "llvm/TargetParser/Triple.h"
#include <functional>
#include <set>

// CodeGen — C ABI lowering for `extern` functions that take or return an aggregate
// (struct/union) by value. Eskiu-to-Eskiu calls pass aggregates as first-class LLVM
// values, which is fine between Eskiu functions but is not what a C compiler expects:
// the C ABI coerces small aggregates into integer/FP registers and passes large ones
// through memory. Each such extern is declared with the lowered C signature, and its
// calls convert the Eskiu-level (logical) values to and from it.
//
// Covered: AArch64 AAPCS64 (Darwin + Linux), x86-64 System V, Windows x64, 32-bit
// ARM AAPCS (soft- and hard-float). The coerced types mirror clang's for the same C
// signature. Other targets keep the first-class lowering.
// Part of the codegen split; see codegen.h.

CodeGen::CAbiTarget CodeGen::cabiTarget() const {
    llvm::Triple t(module->getTargetTriple());
    if (t.isAArch64()) return CAbiTarget::AArch64;
    if (t.getArch() == llvm::Triple::x86_64)
        return t.isOSWindows() ? CAbiTarget::Win64 : CAbiTarget::SysV;
    if (t.isARM() || t.isThumb()) return CAbiTarget::ARM32;
    return CAbiTarget::None;
}

// Only named struct types are C aggregates here: user structs and unions (and ADT
// enums). Literal structs are Eskiu fat values (closures, slices) passed as-is, and
// the Eskiu `va_list` keeps its first-class lowering.
static bool isCAggregate(llvm::Type* ty) {
    auto* st = llvm::dyn_cast<llvm::StructType>(ty);
    return st && !st->isLiteral() && st->getName() != "__va_list";
}

// Flatten an aggregate into its scalar leaves (byte offset, type). A union contributes
// every member at its own offset, since its storage type keeps only one of them.
void CodeGen::cabiLeaves(llvm::Type* ty, uint64_t base,
                         std::vector<std::pair<uint64_t, llvm::Type*>>& out) const {
    const llvm::DataLayout& DL = module->getDataLayout();
    if (auto* st = llvm::dyn_cast<llvm::StructType>(ty)) {
        auto uit = unionMemberTypes.find(st);
        if (uit != unionMemberTypes.end()) {
            for (llvm::Type* m : uit->second) cabiLeaves(m, base, out);
            return;
        }
        // A C-layout bitfield struct: its fields, not its storage elements. Like clang
        // (BitsContainNoUserData), a bitfield counts as data from its first bit through
        // its declared type's width; each such byte in the struct is an `i8` leaf.
        auto lit = st->hasName() ? structLayout.find(st->getName().str()) : structLayout.end();
        auto fit = st->hasName() ? structFields.find(st->getName().str()) : structFields.end();
        if (lit != structLayout.end() && fit != structFields.end() && !lit->second.empty()
                && lit->second.begin()->second.byOffset) {
            uint64_t size = DL.getTypeAllocSize(st);
            for (const auto& f : fit->second) {
                const BitfieldSlot& s = lit->second.at(f.name);
                if (!s.isBitfield) { cabiLeaves(s.storageType, base + s.byteOffset, out); continue; }
                uint64_t first = s.byteOffset * 8 + s.bitOffset;
                uint64_t last = first + s.storageType->getIntegerBitWidth();
                for (uint64_t b = first / 8; b < (last + 7) / 8 && b < size; ++b)
                    out.push_back({base + b, llvm::Type::getInt8Ty(ty->getContext())});
            }
            return;
        }
        const llvm::StructLayout* sl = DL.getStructLayout(st);
        for (unsigned i = 0; i < st->getNumElements(); ++i)
            cabiLeaves(st->getElementType(i), base + sl->getElementOffset(i), out);
        return;
    }
    if (auto* at = llvm::dyn_cast<llvm::ArrayType>(ty)) {
        uint64_t esz = DL.getTypeAllocSize(at->getElementType());
        for (uint64_t i = 0; i < at->getNumElements(); ++i)
            cabiLeaves(at->getElementType(), base + i * esz, out);
        return;
    }
    out.push_back({base, ty});
}

// Homogeneous floating-point aggregate: 1..4 leaves of one FP type, densely packed.
// Returned as `[N x fp]` (asArray) or the literal struct `{ fp, ... }` clang uses for
// HFA results and for 32-bit ARM hard-float arguments.
static llvm::Type* hfaType(const std::vector<std::pair<uint64_t, llvm::Type*>>& leaves,
                           uint64_t size, const llvm::DataLayout& DL, bool asArray) {
    if (leaves.empty()) return nullptr;
    llvm::Type* base = leaves[0].second;
    if (!base->isFloatTy() && !base->isDoubleTy()) return nullptr;
    uint64_t esz = DL.getTypeAllocSize(base);
    std::set<uint64_t> offs;
    for (const auto& l : leaves) {
        if (l.second != base || l.first % esz != 0) return nullptr;
        offs.insert(l.first);
    }
    uint64_t n = offs.size();
    if (n > 4 || size != n * esz) return nullptr;
    if (asArray) return llvm::ArrayType::get(base, n);
    return llvm::StructType::get(base->getContext(), std::vector<llvm::Type*>(n, base));
}

CodeGen::CAbiArg CodeGen::classifyCAbi(llvm::Type* ty, bool isReturn, CAbiTarget tgt,
                                       unsigned& freeInt, unsigned& freeSSE) const {
    const llvm::DataLayout& DL = module->getDataLayout();
    llvm::LLVMContext& ctx = *context;
    CAbiArg r;
    if (!isCAggregate(ty)) {
        // A scalar (or a literal fat value) consumes argument registers on SysV.
        if (tgt == CAbiTarget::SysV && !isReturn) {
            std::vector<std::pair<uint64_t, llvm::Type*>> ls;
            cabiLeaves(ty, 0, ls);
            for (const auto& l : ls) {
                if (l.second->isFloatingPointTy()) { if (freeSSE) --freeSSE; }
                else if (freeInt) --freeInt;
            }
        }
        return r;
    }
    uint64_t size = DL.getTypeAllocSize(ty);
    uint64_t align = DL.getABITypeAlign(ty).value();
    if (size == 0) return r;
    std::vector<std::pair<uint64_t, llvm::Type*>> leaves;
    cabiLeaves(ty, 0, leaves);
    auto indirect = [&](CAbiArg::Kind k, uint64_t al) {
        r.kind = isReturn ? CAbiArg::Sret : k; r.ty = ty; r.align = (unsigned)al;
        return r;
    };

    switch (tgt) {
    case CAbiTarget::AArch64: {
        if (llvm::Type* h = hfaType(leaves, size, DL, !isReturn)) {
            r.kind = CAbiArg::Coerce; r.ty = h;
            // AAPCS64 (not Darwin) keeps a stack-passed HFA in 8-byte-aligned slots.
            if (!isReturn && !llvm::Triple(module->getTargetTriple()).isOSDarwin())
                r.stackAlign = align >= 16 ? 16 : 8;
            return r;
        }
        if (size > 16) return indirect(CAbiArg::Indirect, align);
        r.kind = CAbiArg::Coerce;
        llvm::Type* i64 = llvm::Type::getInt64Ty(ctx);
        if (size > 8) r.ty = llvm::ArrayType::get(i64, 2);
        else r.ty = isReturn ? (llvm::Type*)llvm::IntegerType::get(ctx, (unsigned)size * 8) : i64;
        return r;
    }
    case CAbiTarget::Win64: {
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            r.kind = CAbiArg::Coerce; r.ty = llvm::IntegerType::get(ctx, (unsigned)size * 8);
            return r;
        }
        return indirect(CAbiArg::Indirect, align);
    }
    case CAbiTarget::ARM32: {
        std::string tt = llvm::Triple(module->getTargetTriple()).str();
        bool hard = tt.size() >= 2 && tt.compare(tt.size() - 2, 2, "hf") == 0;
        if (hard) {
            if (llvm::Type* h = hfaType(leaves, size, DL, false)) { r.kind = CAbiArg::Coerce; r.ty = h; return r; }
        }
        if (isReturn) {
            if (size <= 4) { r.kind = CAbiArg::Coerce; r.ty = llvm::Type::getInt32Ty(ctx); return r; }
            return indirect(CAbiArg::Sret, align);
        }
        if (size > 64) return indirect(CAbiArg::ByVal, std::min<uint64_t>(std::max<uint64_t>(align, 4), 8));
        r.kind = CAbiArg::Coerce;
        if (align <= 4) r.ty = llvm::ArrayType::get(llvm::Type::getInt32Ty(ctx), (size + 3) / 4);
        else r.ty = llvm::ArrayType::get(llvm::Type::getInt64Ty(ctx), (size + 7) / 8);
        return r;
    }
    case CAbiTarget::SysV: {
        // Classify each eightbyte INTEGER or SSE (INTEGER wins a merge); anything over
        // 16 bytes or with a misaligned field goes to memory.
        // In memory: a result via sret; an argument byval, except that once the integer
        // registers are exhausted an eightbyte-sized one is passed as an integer (it lands
        // in the same stack slot; clang does the same).
        auto memory = [&]() {
            if (!isReturn && freeInt == 0 && align <= 8 && size <= 8) {
                r.kind = CAbiArg::Coerce; r.ty = llvm::IntegerType::get(ctx, (unsigned)size * 8);
                return r;
            }
            return indirect(CAbiArg::ByVal, isReturn ? align : std::max<uint64_t>(align, 8));
        };
        if (size > 16) return memory();
        enum Cls { None, Int, Sse };
        Cls cls[2] = {None, None};
        for (const auto& l : leaves) {
            uint64_t lsz = DL.getTypeAllocSize(l.second);
            uint64_t lal = DL.getABITypeAlign(l.second).value();
            if (l.first % lal != 0 || l.first / 8 != (l.first + lsz - 1) / 8) return memory();
            Cls c = l.second->isFloatingPointTy() ? Sse : Int;
            Cls& slot = cls[l.first / 8];
            if (slot == None || c == Int) slot = c;
        }
        unsigned nEb = size > 8 ? 2 : 1;
        auto leafAt = [&](uint64_t off) -> llvm::Type* {
            for (const auto& l : leaves) if (l.first == off) return l.second;
            return nullptr;
        };
        auto ebType = [&](unsigned eb) -> llvm::Type* {
            uint64_t o = eb * 8;
            if (cls[eb] == Sse) {
                llvm::Type* t0 = leafAt(o);
                if (!t0 || t0->isDoubleTy()) return llvm::Type::getDoubleTy(ctx);
                llvm::Type* t1 = (size - o > 4) ? leafAt(o + 4) : nullptr;
                if (t1 && t1->isFloatTy()) return llvm::FixedVectorType::get(t0, 2);
                return t0;
            }
            llvm::Type* t0 = leafAt(o);
            if (t0 && (t0->isPointerTy() || t0->isIntegerTy(64))) return t0;
            if (t0 && t0->isIntegerTy()) {
                unsigned w = std::max(8u, t0->getIntegerBitWidth());
                bool clean = true;
                for (const auto& l : leaves)
                    if (l.first >= o + w / 8 && l.first < o + 8) clean = false;
                if (clean && w <= 32) return llvm::IntegerType::get(ctx, w);
            }
            return llvm::IntegerType::get(ctx, (unsigned)std::min<uint64_t>(size - o, 8) * 8);
        };
        unsigned needInt = 0, needSSE = 0;
        for (unsigned eb = 0; eb < nEb; ++eb) {
            if (cls[eb] == Sse) ++needSSE; else ++needInt;
        }
        if (!isReturn) {
            if (needInt > freeInt || needSSE > freeSSE) return memory();
            freeInt -= needInt; freeSSE -= needSSE;
        }
        llvm::Type* lo = ebType(0);
        if (nEb == 1) { r.kind = CAbiArg::Coerce; r.ty = lo; return r; }
        llvm::Type* hi = ebType(1);
        // The high part must start at offset 8: widen a 4-byte low part.
        uint64_t hiStart = (DL.getTypeAllocSize(lo) + DL.getABITypeAlign(hi).value() - 1)
                         / DL.getABITypeAlign(hi).value() * DL.getABITypeAlign(hi).value();
        if (hiStart != 8)
            lo = lo->isFloatTy() ? llvm::Type::getDoubleTy(ctx) : (llvm::Type*)llvm::Type::getInt64Ty(ctx);
        r.ty = llvm::StructType::get(ctx, {lo, hi});
        r.kind = isReturn ? CAbiArg::Coerce : CAbiArg::Expand;
        return r;
    }
    case CAbiTarget::None:
        break;
    }
    return r;
}

bool CodeGen::buildCAbiSig(llvm::FunctionType* logical, CAbiSig& sig) const {
    CAbiTarget tgt = cabiTarget();
    if (tgt == CAbiTarget::None) return false;
    bool any = isCAggregate(logical->getReturnType());
    for (llvm::Type* p : logical->params()) any = any || isCAggregate(p);
    if (!any) return false;
    sig.logical = logical;
    unsigned freeInt = 6, freeSSE = 8;
    llvm::Type* rt = logical->getReturnType();
    if (!rt->isVoidTy()) sig.ret = classifyCAbi(rt, true, tgt, freeInt, freeSSE);
    if (sig.ret.kind == CAbiArg::Sret && freeInt) --freeInt;
    for (llvm::Type* p : logical->params())
        sig.params.push_back(classifyCAbi(p, false, tgt, freeInt, freeSSE));
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    std::vector<llvm::Type*> irParams;
    if (sig.ret.kind == CAbiArg::Sret) irParams.push_back(ptrTy);
    for (size_t i = 0; i < sig.params.size(); ++i) {
        const CAbiArg& a = sig.params[i];
        switch (a.kind) {
        case CAbiArg::Direct: irParams.push_back(logical->getParamType((unsigned)i)); break;
        case CAbiArg::Coerce: irParams.push_back(a.ty); break;
        case CAbiArg::Expand:
            for (llvm::Type* e : llvm::cast<llvm::StructType>(a.ty)->elements()) irParams.push_back(e);
            break;
        default: irParams.push_back(ptrTy); break;
        }
    }
    llvm::Type* irRet = sig.ret.kind == CAbiArg::Sret ? llvm::Type::getVoidTy(*context)
                      : sig.ret.kind == CAbiArg::Coerce ? sig.ret.ty : rt;
    sig.lowered = llvm::FunctionType::get(irRet, irParams, logical->isVarArg());
    return true;
}

// The ABI attributes (sret / byval + alignment) at their lowered parameter indices.
void CodeGen::addCAbiAttrs(const CAbiSig& sig,
                           const std::function<void(unsigned, llvm::Attribute)>& add) const {
    llvm::LLVMContext& ctx = *context;
    unsigned idx = 0;
    if (sig.ret.kind == CAbiArg::Sret) {
        add(0, llvm::Attribute::getWithStructRetType(ctx, sig.ret.ty));
        add(0, llvm::Attribute::getWithAlignment(ctx, llvm::Align(sig.ret.align)));
        idx = 1;
    }
    for (const auto& a : sig.params) {
        if (a.stackAlign)
            add(idx, llvm::Attribute::getWithStackAlignment(ctx, llvm::Align(a.stackAlign)));
        if (a.kind == CAbiArg::ByVal) {
            add(idx, llvm::Attribute::getWithByValType(ctx, a.ty));
            add(idx, llvm::Attribute::getWithAlignment(ctx, llvm::Align(a.align)));
        }
        idx += a.kind == CAbiArg::Expand
             ? llvm::cast<llvm::StructType>(a.ty)->getNumElements() : 1;
    }
}

llvm::Function* CodeGen::declareCAbiExtern(const std::string& name, const CAbiSig& sig) {
    llvm::Function* fn = llvm::Function::Create(sig.lowered, llvm::Function::ExternalLinkage,
                                                name, module.get());
    addCAbiAttrs(sig, [&](unsigned i, llvm::Attribute a) { fn->addParamAttr(i, a); });
    externAbi[name] = sig;
    return fn;
}

// Reinterpret `v` as `to` through memory (clang's coerced load/store): store it into a
// slot big and aligned enough for both types, then load the other type back.
llvm::Value* CodeGen::cabiReinterpret(llvm::Value* v, llvm::Type* to) {
    if (v->getType() == to) return v;
    const llvm::DataLayout& DL = module->getDataLayout();
    llvm::Type* from = v->getType();
    llvm::Type* big = DL.getTypeAllocSize(from) >= DL.getTypeAllocSize(to) ? from : to;
    llvm::AllocaInst* slot = entryAlloca(big, nullptr, "cabi.coerce");
    slot->setAlignment(std::max(DL.getABITypeAlign(from), DL.getABITypeAlign(to)));
    if (DL.getTypeAllocSize(from) < DL.getTypeAllocSize(to))
        builder->CreateStore(llvm::Constant::getNullValue(to), slot);
    builder->CreateStore(v, slot);
    return builder->CreateLoad(to, slot);
}

llvm::Value* CodeGen::emitCAbiCall(llvm::Function* fn, const CAbiSig& sig,
                                   const std::vector<llvm::Value*>& args, bool allowInvoke) {
    std::vector<llvm::Value*> ir;
    llvm::AllocaInst* sretSlot = nullptr;
    if (sig.ret.kind == CAbiArg::Sret) {
        sretSlot = entryAlloca(sig.ret.ty, nullptr, "cabi.sret");
        ir.push_back(sretSlot);
    }
    for (size_t i = 0; i < args.size(); ++i) {
        llvm::Value* v = args[i];
        if (i >= sig.params.size()) { ir.push_back(v); continue; }   // variadic tail
        const CAbiArg& a = sig.params[i];
        switch (a.kind) {
        case CAbiArg::Direct: ir.push_back(v); break;
        case CAbiArg::Coerce: ir.push_back(cabiReinterpret(v, a.ty)); break;
        case CAbiArg::Expand: {
            llvm::Value* pair = cabiReinterpret(v, a.ty);
            for (unsigned e = 0; e < llvm::cast<llvm::StructType>(a.ty)->getNumElements(); ++e)
                ir.push_back(builder->CreateExtractValue(pair, {e}));
            break;
        }
        default: {                                   // a caller-owned copy in memory
            llvm::AllocaInst* copy = entryAlloca(v->getType(), nullptr, "cabi.arg");
            copy->setAlignment(std::max(copy->getAlign(), llvm::Align(a.align)));
            builder->CreateStore(v, copy);
            ir.push_back(copy);
            break;
        }
        }
    }
    llvm::Value* call = allowInvoke ? createMaybeInvoke(sig.lowered, fn, ir)
                                    : builder->CreateCall(sig.lowered, fn, ir);
    if (auto* cb = llvm::dyn_cast<llvm::CallBase>(call))
        addCAbiAttrs(sig, [&](unsigned i, llvm::Attribute a) { cb->addParamAttr(i, a); });
    llvm::Type* rt = sig.logical->getReturnType();
    if (sig.ret.kind == CAbiArg::Sret) return builder->CreateLoad(rt, sretSlot);
    if (sig.ret.kind == CAbiArg::Coerce) return cabiReinterpret(call, rt);
    return call;
}

// C calls an Eskiu function through a raw pointer (`(*void)f`): the thunk takes the
// lowered C signature, rebuilds the Eskiu-level values and calls `target` with the
// Eskiu convention (first-class aggregates, hidden result pointer for a large struct),
// then hands the result back the C way.
llvm::Function* CodeGen::cabiCallbackThunk(llvm::Function* target) {
    std::string name = target->getName().str();
    if (externAbi.count(name) || target->isVarArg()) return target;
    auto sretIt = funcSretTypes.find(name);
    bool eskSret = sretIt != funcSretTypes.end();
    llvm::FunctionType* fty = target->getFunctionType();
    std::vector<llvm::Type*> lps(fty->param_begin() + (eskSret ? 1 : 0), fty->param_end());
    llvm::Type* lret = eskSret ? sretIt->second : fty->getReturnType();
    CAbiSig sig;
    if (!buildCAbiSig(llvm::FunctionType::get(lret, lps, false), sig)) return target;
    std::string tname = "__cabi_" + name;
    if (llvm::Function* have = module->getFunction(tname)) return have;
    llvm::Function* thunk = llvm::Function::Create(sig.lowered, llvm::Function::InternalLinkage,
                                                   tname, module.get());
    addCAbiAttrs(sig, [&](unsigned i, llvm::Attribute a) { thunk->addParamAttr(i, a); });

    llvm::IRBuilderBase::InsertPointGuard guard(*builder);
    builder->SetInsertPoint(llvm::BasicBlock::Create(*context, "entry", thunk));
    auto ai = thunk->arg_begin();
    llvm::Value* sretOut = nullptr;
    if (sig.ret.kind == CAbiArg::Sret) sretOut = &*ai++;
    std::vector<llvm::Value*> args;
    for (size_t i = 0; i < sig.params.size(); ++i) {
        const CAbiArg& a = sig.params[i];
        switch (a.kind) {
        case CAbiArg::Direct: args.push_back(&*ai++); break;
        case CAbiArg::Coerce: args.push_back(cabiReinterpret(&*ai++, lps[i])); break;
        case CAbiArg::Expand: {
            auto* st = llvm::cast<llvm::StructType>(a.ty);
            llvm::Value* pair = llvm::UndefValue::get(st);
            for (unsigned e = 0; e < st->getNumElements(); ++e)
                pair = builder->CreateInsertValue(pair, &*ai++, {e});
            args.push_back(cabiReinterpret(pair, lps[i]));
            break;
        }
        default: args.push_back(builder->CreateLoad(lps[i], &*ai++)); break;
        }
    }
    llvm::Value* r = nullptr;
    if (eskSret) {
        llvm::AllocaInst* tmp = entryAlloca(lret, nullptr, "cabi.res");
        args.insert(args.begin(), tmp);
        builder->CreateCall(target, args);
        r = builder->CreateLoad(lret, tmp);
    } else {
        r = builder->CreateCall(target, args);
    }
    if (sig.ret.kind == CAbiArg::Sret) {
        builder->CreateStore(r, sretOut);
        builder->CreateRetVoid();
    } else if (lret->isVoidTy()) {
        builder->CreateRetVoid();
    } else {
        builder->CreateRet(sig.ret.kind == CAbiArg::Coerce ? cabiReinterpret(r, sig.ret.ty) : r);
    }
    return thunk;
}
