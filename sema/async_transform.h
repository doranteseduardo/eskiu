#pragma once
#include "../ast/ast.h"

// AsyncTransform — lowers `async fn` + `await` into a resumable state machine
// (see docs/dev/async-design.md §4). Runs after type checking, before codegen:
// each async function is replaced by a frame struct, a `__<name>_resume` function
// (an if-chain over the resume state), and a constructor that returns *Future<T>.
// The result is ordinary Eskiu AST that normal codegen handles.
//
// v1 supports a single `await` bound in a `let`:
//     async T f(params) { <stmts> let x = await CALL(...); <stmts> return E; }
// Unsupported shapes raise a clear error rather than miscompiling.
class AsyncTransform {
public:
    void run(Program* program);
};
