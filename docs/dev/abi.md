# Eskiu ABI

How Eskiu types and constructs lower to LLVM IR, and the calling/layout
conventions a foreign caller must follow. This reflects the code generator
(`codegen/codegen_*.cpp`); it is the contract for linking Eskiu with C (and, on
the roadmap, Swift/Kotlin over the C ABI).

Eskiu targets the platform's native C ABI via LLVM, so on a given target the
representations below match what a C compiler produces for the equivalent
declarations. There is no separate Eskiu calling convention.

---

## Scalar types

| Eskiu | LLVM | Signed? | Notes |
|-------|------|---------|-------|
| `int`, `int32` | `i32` | signed | default integer width |
| `int8` / `int16` / `int64` | `i8` / `i16` / `i64` | signed | |
| `uint`, `uint32` | `i32` | unsigned | |
| `uint8` / `uint16` / `uint64` | `i8` / `i16` / `i64` | unsigned | |
| `bool` | `i1` | n/a | |
| `char` | `i8` | unsigned | |
| `float` | `float` | n/a | IEEE-754 single |
| `double` | `double` | n/a | IEEE-754 double |
| `string` | `ptr` | n/a | pointer to a NUL-terminated byte buffer |
| `void` | `void` | n/a | return type only |

Signedness is not part of the LLVM type (both `int` and `uint` are `i32`); it
is carried by the operations. Integer **widening** chooses the extension by the
*source* type: `uint*`, `char` and `bool` zero-extend; signed integers
sign-extend. Narrowing truncates. `int`↔float uses signed conversions
(`sitofp`/`fptosi`).

---

## Pointers and `const`

All pointers lower to LLVM's opaque `ptr` (address space 0), regardless of
pointee type or spelling (`int*` and `*int` are identical). Pointer-to-pointer,
casts between pointer types, and `string` are all just `ptr`. A checked nullable
pointer `?*T` has the identical representation to `*T` (a bare `ptr`); its
non-null requirement is enforced by the type checker at compile time and adds no
runtime cost or storage.

A leading `*` and a trailing `[N]` follow the source rule that the array binds
outermost: `*T[N]` is an array of N `ptr` elements, while a pointer to an array is
spelled `T[N]*` (a single `ptr`). For C interop, libc `size_t` parameters and returns
(`strlen`, the size argument of `memcpy`/`memset`/`memmove`/`memcmp`/`memchr`) are
declared `int64` so the extern signatures match the C ABI on 64-bit targets.

`const` has **no ABI effect**. Every `const` qualifier (base/pointee `const T`,
pointer-level `T* const`) is stripped before lowering, so a `const int*` and an
`int*` are the same at the IR/object level. const is purely a compile-time
checking aid.

---

## Aggregates

### Structs

A non-packed struct without bitfields lowers to an LLVM struct of its field
types in declaration order, with the target's **natural alignment** (LLVM
inserts padding). This matches a C `struct` with the same fields.

### Packed structs

`packed struct` (equivalently `#pragma pack(1)`) lowers to an LLVM *packed*
struct: no padding, fields back-to-back.

`#pragma pack(N)` for `N > 1` uses an explicit manual layout that caps each
field's alignment at `N`: a field is placed at the next offset that is a
multiple of `min(natural-align, N)`, with `[k x i8]` padding inserted as needed,
and the total size rounded up to the struct's alignment (`min(max-field-align,
N)`). The result is emitted as a packed LLVM struct (padding is explicit) and
matches the C `#pragma pack(N)` ABI. Field access goes through a
logical→physical index map (padding shifts indices).

### Bitfields

Consecutive bitfields are packed into a storage word of the field's declared
integer type; a non-bitfield field closes the current word. Each field records
its physical slot, bit offset and width. Reads load the word, shift by the bit
offset and mask (sign-extending for signed fields); writes are read-modify-write
of the word.

### Unions

A `union` lowers to a single byte array `[N x i8]` (wrapped in a one-field
struct), where `N` is the size of the largest member. All members share offset
0; a member access reinterprets the storage at that type.

---

## Enums

### Classic enums

A payload-less `enum` is an `i32`. Each member is a compile-time `i32` constant
(explicit `= value` or the running auto-increment).

### Algebraic enums (ADTs)

A payload-bearing enum is a tagged union:

```
{ i32 tag, [N x i64] payload }
```

- `tag` is the 0-based variant index (`i32`).
- `payload` is `[N x i64]`, where `N = ceil(maxPayloadBytes / 8)` (at least 1),
  `maxPayloadBytes` being the largest variant's naturally-aligned field layout.
- Construction stores the tag, then writes each variant field into the payload
  area (coercing the value to the field type). `match` reads the tag, branches,
  and reads payload fields per arm.

Generic ADTs (`Option<T>`, `Either<A,B>`) are monomorphized per instantiation:
each concrete instance (`Option_int`) gets its own `{i32, [N x i64]}` sized for
that instance, exactly like template structs.

---

## Functions and calling convention

Parameters and results use the platform C ABI as produced by LLVM for the
lowered types. Scalars and pointers pass in registers per the target ABI.

**Struct return (sret).** A function returning a struct larger than **16 bytes**
(`getTypeAllocSize > 16`) returns `void` and takes a hidden pointer as its first
parameter; the caller allocates the result buffer and passes its address.
Structs ≤ 16 bytes are returned by value (the target ABI splits them into
registers as usual). This is the System V / AArch64 rule and is C-compatible.

**Variadics.** A `...` parameter makes the LLVM function `isVarArg`. The built-in
`va_list` is the struct `{ ptr, ptr, ptr, i32, i32 }` (32 B, 8-aligned), a
superset of the x86-64 (24 B) and AArch64 (32 B) layouts, so one type serves
both; `va_start`/`va_arg<T>`/`va_end` lower to the corresponding LLVM
intrinsics/instruction. Variadic arguments follow C default promotions:
integers narrower than 32 bits widen to `i32` (sign- or zero-extended), `bool`
zero-extends, and `float` widens to `double`.

---

## Function pointers, closures and interfaces

These use **fat pointers**: a two-`ptr` struct `{ ptr, ptr }`.

**Function values / closures.** A `fn(A,B)->R` value is `{ fn, env }`: a code
pointer plus an environment pointer. A lambda compiles to a function whose first
parameter is the `env*`, followed by the declared parameters; captured variables
are loaded from the env struct (one field per capture, in capture order). A bare
top-level function used as a value is wrapped by a thunk `__fnptr_<name>` with
`env == null`.

A **non-escaping** closure (only called or passed to a non-`escaping` parameter)
keeps its env on the stack (zero cost). An **escaping** closure (returned,
stored, or passed to an `escaping` parameter) heap-allocates the env;
`free_closure(f)` releases it. The choice is fixed at compile time by escape
analysis.

**Interfaces.** An interface value is `{ data, vtable }`: a pointer to the
underlying struct plus a pointer to a per-(interface, struct) vtable constant.
The vtable is an array of function pointers, one per interface method in
declaration order. A method call loads `data` and `vtable`, indexes the vtable
by method position, and calls the function pointer with `data` as the first
argument.

**Slices.** A slice `T[]` is also a fat pointer, but `{ ptr, i64 }`: a pointer to
the first element plus an element count. `s[i]` indexes through the pointer,
`s.len` reads field 1, and the slice is passed and returned by value like any
small struct. A slice aliases its backing storage (it is built by slicing a fixed
array or a raw pointer with `a[lo..hi]`), so it neither owns nor copies the data.

---

## Name mangling

Template and generic-enum instances are monomorphized with a deterministic
mangling: in the instance type, spaces are removed and `<`, `>`, `,` become `_`.

| Source | Mangled instance |
|--------|------------------|
| `List<int>` | `List_int` |
| `Pair<int, float>` | `Pair_int_float` |
| `Option<string>` | `Option_string` |

Struct methods are emitted as `Struct_method`; interface vtable entries as
`Interface_method`. Non-template top-level functions keep their source names
(Eskiu does not overload, so no signature mangling is needed). `extern` and
`intrinsic` declarations use their exact C symbol names: that is how Eskiu
calls into C.

---

## Async frames

An `async` function is lowered (before code generation) to a resumable state
machine: a heap-allocated **frame struct** holding the live locals plus a resume
state, a `resume` function, and a constructor that returns a `*Future<T>`. The
`Future<T>`/`FutureHdr` layout and the completion/waker handshake are specified
in [async-design.md](async-design.md); that document is the authoritative
contract for the runtime side.

---

## See also

- [../lang/grammar.md](../lang/grammar.md): the surface syntax these rules lower.
- [async-design.md](async-design.md): the `Future<T>` ABI and scheduler contract.
- [architecture.md](architecture.md): the compiler pipeline overview.
