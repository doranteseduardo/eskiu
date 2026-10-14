// `eskiuc repl` support: the memory layout of a value, for printing it once the JIT
// has run the input (see repl.cpp), and handing the module over to the JIT.
#include "codegen.h"
#include "../ast/type_qual.h"
#include "../template_utils.h"
#include "llvm/IR/DataLayout.h"

std::unique_ptr<llvm::Module> CodeGen::takeModule(std::unique_ptr<llvm::LLVMContext>& ctx) {
    builder.reset();
    ctx = std::move(context);
    return std::move(module);
}

std::shared_ptr<CodeGen::ReplLayout> CodeGen::replLayout(const std::string& type0, int depth) {
    auto out = std::make_shared<ReplLayout>();
    std::string t = tyq::strip(type0);
    if (!t.empty() && t[0] == '?') t = t.substr(1);
    for (const char* pfx : {"struct:", "union:", "enum:", "interface:"}) {
        std::string p = pfx;
        if (t.rfind(p, 0) == 0) { t = t.substr(p.size()); break; }
    }
    out->name = t;
    if (depth > 6 || t.empty() || t == "void") return out;
    std::string d = expandAlias(t);
    if (d != t) {
        auto l = replLayout(d, depth + 1);
        if (l->kind == ReplLayout::Opaque) l->name = t;
        return l;
    }
    const llvm::DataLayout& DL = module->getDataLayout();

    // A fixed-size array `T[N]` or a slice `T[]`. In C order the leftmost bracket is the
    // outer dimension: `int[2][3]` holds two `int[3]`.
    if (t.back() == ']') {
        size_t open = t.find('[');
        size_t close = open == std::string::npos ? open : t.find(']', open);
        if (open != std::string::npos && close != std::string::npos) {
            std::string dim = t.substr(open + 1, close - open - 1);
            std::string elemTy = t.substr(0, open) + t.substr(close + 1);
            auto elem = replLayout(elemTy, depth + 1);
            llvm::Type* et = nullptr;
            try { et = getTypeFromString(elemTy); } catch (...) {}
            if (!et || !et->isSized()) return out;
            out->elem = elem;
            out->stride = DL.getTypeAllocSize(et);
            if (dim.empty()) {
                out->kind = ReplLayout::Slice;
                out->dataOff = 0;
                out->lenOff = DL.getPointerSize();
                out->lenSize = 8;
                return out;
            }
            uint64_t n = 0;
            if (!resolveArrayDim(dim, n)) return out;
            out->kind = ReplLayout::Array;
            out->count = n;
            return out;
        }
    }
    if (t == "string") { out->kind = ReplLayout::CStr; return out; }
    if (isPointerType(t)) { out->kind = ReplLayout::Ptr; return out; }
    if (t == "bool") { out->kind = ReplLayout::Bool; return out; }
    if (t == "char") { out->kind = ReplLayout::Char; return out; }
    if (t == "float") { out->kind = ReplLayout::Float; return out; }
    if (t == "double") { out->kind = ReplLayout::Double; return out; }
    static const std::map<std::string, std::pair<unsigned, bool>> ints = {
        {"int", {4, true}}, {"int8", {1, true}}, {"int16", {2, true}}, {"int32", {4, true}},
        {"int64", {8, true}}, {"uint", {4, false}}, {"uint8", {1, false}},
        {"uint16", {2, false}}, {"uint32", {4, false}}, {"uint64", {8, false}},
    };
    if (auto it = ints.find(t); it != ints.end()) {
        out->kind = it->second.second ? ReplLayout::Int : ReplLayout::UInt;
        out->size = it->second.first;
        return out;
    }
    if (auto it = plainEnumDecls.find(t); it != plainEnumDecls.end()) {
        out->kind = ReplLayout::Enum;
        out->size = 4;
        for (const auto& m : it->second->members) out->enumNames.emplace(m.second, m.first);
        return out;
    }

    llvm::Type* lt = nullptr;
    try { lt = getTypeFromString(t); } catch (...) { return out; }
    auto* st = llvm::dyn_cast_or_null<llvm::StructType>(lt);
    if (!st || !st->hasName() || st->isOpaque()) return out;
    std::string sname = st->getName().str();

    // An algebraic enum: { i32 tag, payload area }, each variant's payload laid out as a
    // struct of its field types at element 1.
    EnumDecl* ed = nullptr;
    std::map<std::string, std::string> subs;
    if (auto it = adtEnumDecls.find(sname); it != adtEnumDecls.end()) ed = it->second;
    else if (auto ia = enumInstanceArgs.find(sname); ia != enumInstanceArgs.end()) {
        if (auto g = genericEnumDecls.find(ia->second.first); g != genericEnumDecls.end()) {
            ed = g->second;
            for (size_t i = 0; i < ed->typeParams.size() && i < ia->second.second.size(); ++i)
                subs[ed->typeParams[i]] = ia->second.second[i];
        }
    }
    if (ed) {
        out->kind = ReplLayout::Variant;
        uint64_t payOff = DL.getStructLayout(st)->getElementOffset(1);
        for (size_t i = 0; i < ed->members.size(); ++i) {
            ReplLayout::Alt alt;
            alt.name = ed->members[i].first;
            std::vector<llvm::Type*> tys;
            std::vector<std::string> names;
            if (i < ed->payloads.size())
                for (const auto& p : ed->payloads[i]) {
                    std::string ft = subs.empty() ? p : substType(p, subs);
                    llvm::Type* ftl = nullptr;
                    try { ftl = getTypeFromString(ft); } catch (...) {}
                    if (!ftl || !ftl->isSized()) { tys.clear(); names.clear(); break; }
                    tys.push_back(ftl);
                    names.push_back(ft);
                }
            if (!tys.empty()) {
                const llvm::StructLayout* vl = DL.getStructLayout(llvm::StructType::get(*context, tys));
                for (size_t k = 0; k < tys.size(); ++k)
                    alt.payload.push_back({"", payOff + vl->getElementOffset(k),
                                           replLayout(names[k], depth + 1)});
            }
            out->alts.emplace(ed->members[i].second, alt);
        }
        return out;
    }

    auto sf = structFields.find(sname);
    if (sf == structFields.end()) return out;
    const auto& fields = sf->second;
    auto fieldOffset = [&](size_t i, uint64_t& off) {
        auto sl = structLayout.find(sname);
        if (sl != structLayout.end()) {
            auto slot = sl->second.find(fields[i].name);
            if (slot == sl->second.end() || slot->second.isBitfield) return false;
            if (slot->second.byOffset) { off = slot->second.byteOffset; return true; }
            if (slot->second.physIndex >= st->getNumElements()) return false;
            off = DL.getStructLayout(st)->getElementOffset(slot->second.physIndex);
            return true;
        }
        if (i >= st->getNumElements()) return false;
        off = DL.getStructLayout(st)->getElementOffset(i);
        return true;
    };
    auto fieldNamed = [&](const std::string& n, uint64_t& off, std::string& ty) {
        for (size_t i = 0; i < fields.size(); ++i)
            if (fields[i].name == n && fields[i].bitWidth == 0 && fieldOffset(i, off)) {
                ty = fields[i].type;
                return true;
            }
        return false;
    };

    // `String` (stdlib <string>) prints as its text, `List<T>` as its elements.
    uint64_t dOff = 0, nOff = 0;
    std::string dTy, nTy;
    if (sname == "String" && fieldNamed("data", dOff, dTy) && fieldNamed("len", nOff, nTy)) {
        out->kind = ReplLayout::Str;
        out->dataOff = dOff;
        out->lenOff = nOff;
        out->lenSize = 8;
        return out;
    }
    auto inst = templateInstanceArgs.find(sname);
    if (inst != templateInstanceArgs.end() && inst->second.first == "List" &&
        inst->second.second.size() == 1 && fieldNamed("data", dOff, dTy) &&
        fieldNamed("size", nOff, nTy)) {
        const std::string& et = inst->second.second[0];
        llvm::Type* elt = nullptr;
        try { elt = getTypeFromString(et); } catch (...) {}
        if (elt && elt->isSized()) {
            out->kind = ReplLayout::List;
            out->dataOff = dOff;
            out->lenOff = nOff;
            out->lenSize = (unsigned)DL.getTypeAllocSize(getTypeFromString(nTy));
            out->stride = DL.getTypeAllocSize(elt);
            out->elem = replLayout(et, depth + 1);
            return out;
        }
    }

    out->kind = ReplLayout::Struct;
    for (size_t i = 0; i < fields.size(); ++i) {
        uint64_t off = 0;
        ReplLayout::Field f;
        f.name = fields[i].name;
        if (fields[i].bitWidth != 0 || !fieldOffset(i, off)) {
            f.layout = std::make_shared<ReplLayout>();
            f.layout->name = fields[i].type;
        } else {
            f.offset = off;
            f.layout = replLayout(fields[i].type, depth + 1);
        }
        out->fields.push_back(f);
    }
    return out;
}
