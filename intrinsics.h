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

inline const std::set<std::string>& supportedIntrinsics() {
    static const std::set<std::string> names = {
        "atomic_load", "atomic_store", "atomic_swap", "atomic_cas",
    };
    return names;
}

inline bool isSupportedIntrinsic(const std::string& name) {
    return supportedIntrinsics().count(name) != 0;
}
