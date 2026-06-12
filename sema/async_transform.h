#pragma once
#include "../ast/ast.h"

// AsyncTransform — lowers `async fn` + `await` into a resumable state machine
// (see docs/dev/async-design.md §4). Runs after type checking, before codegen:
// each async function is replaced by a frame struct, a `__<name>_resume` function
// (an if-chain over the resume state), and a constructor that returns *Future<T>.
// The result is ordinary Eskiu AST that normal codegen handles.
//
// Awaits are lowered across all control flow — `if`/`else`, `while`, C-style
// `for`, `switch`, and `for`-`in` (desugared to a counted `for`), including
// `break`/`continue` and early `return`. An `await` must be bound in a `let`
// (the desugaring pass normalizes the other forms first).
class AsyncTransform {
public:
    void run(Program* program);
};
