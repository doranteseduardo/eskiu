#pragma once
#include <string>
#include <set>

// ============================================================================
// Compiler-provided intrinsics (the `intrinsic` function qualifier).
//
// Single source of truth, shared by the type checker and codegen:
//   - the type checker rejects an `intrinsic` declaration whose name is not
//     listed here, up front, with a clear error (so `intrinsic` is never a
//     keyword that "type-checks but fails at codegen");
//   - codegen lowers each listed name in CodeGen::lowerIntrinsicCall.
//
// `intrinsic` is NOT a user extension point: adding an intrinsic means adding
// its name HERE *and* a lowering case in lowerIntrinsicCall. The two must stay
// in step — a name listed here with no lowering trips the backstop throw in
// codegen (a compiler bug), and a lowering with no entry here is unreachable.
// ============================================================================

// The atomic intrinsics are a regular family: `atomic_<op>` on an `int` cell,
// `atomic_<op>64` on an `int64` cell and `atomic_<op>_ptr` on a pointer cell, plus
// `atomic_fence()`. The ordering is load acquire, store release, the rest acq_rel (a
// failed cas acquire), fence seq_cst; a `_relaxed` suffix after the cell kind
// (`atomic_fetch_add_relaxed`, `atomic_load64_relaxed`) makes the op monotonic (C's
// memory_order_relaxed), on success and failure alike.
enum class AtomicKind { I32, I64, Ptr };
struct AtomicIntrinsic { std::string op; AtomicKind kind = AtomicKind::I32; bool relaxed = false; };

inline bool parseAtomicIntrinsic(const std::string& name, AtomicIntrinsic& out) {
    if (name == "atomic_fence") { out = {"fence", AtomicKind::I32}; return true; }
    if (name.rfind("atomic_", 0) != 0) return false;
    std::string rest = name.substr(7);
    bool relaxed = rest.size() > 8 && rest.compare(rest.size() - 8, 8, "_relaxed") == 0;
    if (relaxed) rest.resize(rest.size() - 8);
    AtomicKind k = AtomicKind::I32;
    if (rest.size() > 4 && rest.compare(rest.size() - 4, 4, "_ptr") == 0) { k = AtomicKind::Ptr; rest.resize(rest.size() - 4); }
    else if (rest.size() > 2 && rest.compare(rest.size() - 2, 2, "64") == 0) { k = AtomicKind::I64; rest.resize(rest.size() - 2); }
    static const std::set<std::string> intOps = {"load", "store", "swap", "cas", "fetch_add", "fetch_sub",
                                                 "fetch_and", "fetch_or", "fetch_xor"};
    static const std::set<std::string> ptrOps = {"load", "store", "swap", "cas"};
    if (!(k == AtomicKind::Ptr ? ptrOps : intOps).count(rest)) return false;
    out = {rest, k, relaxed};
    return true;
}

inline bool isSupportedIntrinsic(const std::string& name) {
    AtomicIntrinsic a;
    return parseAtomicIntrinsic(name, a);
}
