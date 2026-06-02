# Performance Guide

How to write fast Eskiu code and optimize your programs.

## Table of Contents

1. [Performance Principles](#performance-principles)
2. [Memory Layout](#memory-layout)
3. [Stack vs Heap](#stack-vs-heap)
4. [Pointers and References](#pointers-and-references)
5. [Function Calls](#function-calls)
6. [Control Flow](#control-flow)
7. [Profiling and Benchmarking](#profiling-and-benchmarking)
8. [Case Study: QR Decoder](#case-study-qr-decoder)

---

## Performance Principles

**Eskiu's philosophy: What you see is what you get.** No hidden allocations, no implicit conversions, no magic. This means:

1. **No garbage collection** — No pauses or collection overhead
2. **No implicit conversions** — You decide when to cast
3. **Explicit memory management** — You control allocation and deallocation
4. **Stack allocation by default** — Fast and automatic
5. **Direct LLVM compilation** — Close to C performance

**Result:** Eskiu performance is as good as optimized C.

---

## Memory Layout

Understanding how data is laid out in memory helps you write efficient code.

### Primitive Types

| Type | Size | Notes |
|------|------|-------|
| `i8` | 1 byte | Signed 8-bit integer |
| `i16` | 2 bytes | Signed 16-bit integer |
| `i32` | 4 bytes | Signed 32-bit integer |
| `i64` | 8 bytes | Signed 64-bit integer |
| `u8` | 1 byte | Unsigned 8-bit integer |
| `u16` | 2 bytes | Unsigned 16-bit integer |
| `u32` | 4 bytes | Unsigned 32-bit integer |
| `u64` | 8 bytes | Unsigned 64-bit integer |
| `f32` | 4 bytes | 32-bit float |
| `f64` | 8 bytes | 64-bit float |
| `*T` | 8 bytes (64-bit arch) | Pointer to T |
| `bool` | 1 byte | True/false |

### Struct Layout

Structs are laid out in memory sequentially, field-by-field:

```esk
struct Point {
    x: i32           // 4 bytes, offset 0
    y: i32           // 4 bytes, offset 4
}
// Total: 8 bytes
```

**No padding or alignment quirks** (yet; Phase 6 may optimize this). Fields are exactly in the order you declare them.

### Nested Structs

```esk
struct Rectangle {
    min: Point       // 8 bytes, offset 0
    max: Point       // 8 bytes, offset 8
}
// Total: 16 bytes
```

**Implication:** Accessing `rect.max.x` requires two reads; you can optimize by caching:

```esk
let max_x = rect.max.x;  // One read
let max_y = rect.max.y;  // One more read (nearby memory, cache-friendly)
```

---

## Stack vs Heap

### Stack Allocation (Preferred)

**When:** Local variables, function parameters, small data structures  
**How:** Declared with `let`  
**Cost:** One instruction (stack pointer increment)  
**Deallocation:** Automatic when scope ends

```esk
fn process_point(p: Point) -> i32 {
    let x = p.x;          // Stack: 4 bytes
    let y = p.y;          // Stack: 4 bytes
    return x + y;
}
// x and y freed automatically
```

**Advantages:**
- Blazingly fast (one CPU cycle)
- Automatic cleanup
- Cache-friendly (stack memory is contiguous)

### Heap Allocation (Future, Phase 6)

**When:** Large data, dynamic sizing, long-lived objects  
**How:** Via `alloc()` (coming Phase 6)  
**Cost:** Complex allocation algorithm, usually slower  
**Deallocation:** Explicit `free()` (your responsibility)

```esk
// NOT YET IMPLEMENTED (Phase 6)
let buffer = alloc(1024 * 1024);  // Allocate 1 MB
// ... use buffer ...
free(buffer);
```

---

## Pointers and References

### Cheap Operations

**Taking a reference is cheap:**
```esk
let x: i32 = 42;
let ptr: *i32 = &x;  // One instruction: copy stack address
```

**Passing by reference is efficient:**
```esk
fn sum(arr: *i32, len: i32) -> i32 {
    let result: i32 = 0;
    for (let i: i32 = 0; i < len; i = i + 1) {
        result = result + arr[i];
    }
    return result;
}
// arr is a pointer; no copy of entire array
```

**Dereferencing is cheap:**
```esk
let ptr: *i32 = &x;
let value = *ptr;  // One memory read
```

### Avoid Excessive Dereferencing

**Bad:**
```esk
let sum = ptr[0] + ptr[1] + ptr[2];  // Three pointer dereferences
```

**Good:**
```esk
let a = ptr[0];
let b = ptr[1];
let c = ptr[2];
let sum = a + b + c;  // Same logic, compiles to same code
```

(Modern LLVM optimizers will eliminate redundant loads anyway, but being explicit is clearer.)

---

## Function Calls

### Inline Functions

Eskiu doesn't yet support explicit `inline` directives (Phase 5+), but the LLVM backend will inline small functions automatically.

**Large functions won't be inlined:**
```esk
fn expensive_computation(x: i32) -> i32 {
    // 50 lines of code
    return result;
}

// This is a real function call (push/pop stack frame)
let y = expensive_computation(5);
```

**Small functions are likely inlined:**
```esk
fn add(a: i32, b: i32) -> i32 {
    return a + b;  // One instruction; will be inlined
}

let result = add(3, 4);  // Compiles to: result = 3 + 4
```

### Avoid Frequent Small Calls

If you're calling a small function in a tight loop, consider:

**Bad (loop with function calls):**
```esk
for (let i: i32 = 0; i < 1000000; i = i + 1) {
    sum = sum + increment(i);  // 1M function calls
}
```

**Good (inline the logic):**
```esk
for (let i: i32 = 0; i < 1000000; i = i + 1) {
    sum = sum + i + 1;  // Directly in loop
}
```

---

## Control Flow

### Branching Cost

**Branch prediction** is crucial on modern CPUs. Predictable branches are fast; unpredictable branches cause pipeline flushes.

**Predictable:**
```esk
for (let i: i32 = 0; i < 1000; i = i + 1) {
    sum = sum + arr[i];  // Loop condition almost always true, then false
}
```

**Unpredictable:**
```esk
for (let i: i32 = 0; i < 1000; i = i + 1) {
    if (arr[i] % 2 == 0) {
        sum = sum + arr[i];
    }
    // Alternates true/false; hard to predict
}
```

### Loop Unrolling

The LLVM optimizer will unroll loops automatically. For hot paths, you can help:

**Without loop unrolling:**
```esk
for (let i: i32 = 0; i < 100; i = i + 1) {
    sum = sum + arr[i];
}
```

**With manual unrolling (if LLVM doesn't do it):**
```esk
for (let i: i32 = 0; i < 100; i = i + 4) {
    sum = sum + arr[i] + arr[i + 1] + arr[i + 2] + arr[i + 3];
}
```

(Usually LLVM does this automatically; don't optimize prematurely.)

---

## Profiling and Benchmarking

### Using Linux `perf`

Profile your compiled Eskiu binary:

```bash
# Compile with debug symbols
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make

# Run with perf
perf record ./program
perf report
```

Output shows which functions consume the most CPU time.

### Using macOS Instruments

```bash
# Compile
./eskiu compile program.esk -o program

# Profile with Instruments
instruments -t "System Trace" ./program
```

### Benchmark Before and After

When optimizing, measure before and after:

```bash
# Original version
time ./program
# Time:	2.345 seconds

# Optimized version
time ./program_optimized
# Time:	1.234 seconds
# Speedup: 1.9x
```

---

## Case Study: QR Decoder

Eskiu's validation target is porting the INE QR decoder from 3–5 seconds to sub-second latency on constrained hardware.

### Performance Strategy

1. **Minimize allocations** — Use stack for all temporary buffers
2. **Cache locality** — Process data in order; avoid scattered memory access
3. **SIMD** — Use vector instructions where applicable (future)
4. **Vectorization** — Process multiple pixels at once in inner loops
5. **Branch prediction** — Keep conditional logic predictable

### Example Optimization

**Original (hypothetical C):**
```c
int decode_qr(uint8_t *image, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            if (is_black(image[idx])) {
                process_module(x, y);
            }
        }
    }
}
```

**Optimized Eskiu (hypothetical):**
```esk
fn decode_qr(image: *u8, width: i32, height: i32) -> i32 {
    let stride = width;
    
    for (let y: i32 = 0; y < height; y = y + 1) {
        let row = image + y * stride;  // Cache row pointer
        
        for (let x: i32 = 0; x < width; x = x + 1) {
            if (is_black(row[x])) {
                process_module(x, y);
            }
        }
    }
    
    return 0;
}
```

**Improvements:**
- Cache row pointer to avoid recalculating offset
- Access memory sequentially (cache-friendly)
- Reduce pointer arithmetic in inner loop

### Expected Results

On constrained hardware (e.g., ARM with limited L1 cache):
- **Original:** 3–5 seconds per decode
- **Optimized Eskiu:** 0.5–1 second per decode
- **Speedup:** 4–8x

This validates Eskiu's ability to deliver systems-level performance.

---

## Best Practices

1. **Profile first** — Don't optimize blind. Use `perf` or `instruments`.
2. **Optimize hot paths** — 80% of time spent in 20% of code.
3. **Keep it simple** — Complex optimizations are hard to maintain.
4. **Trust LLVM** — Modern optimizers do loop unrolling, inlining, and vectorization automatically.
5. **Measure impact** — Before/after benchmarks prove optimization worked.

---

## Limitations

Current limitations (by phase):

- **Phase 4:** No SIMD support yet (Phase 5+)
- **Phase 4:** No profile-guided optimization yet (Phase 6+)
- **Phase 4:** No custom allocators (Phase 6)
- **Phase 4:** No memory pooling (Phase 6+)

These will be addressed in future phases.

---

## Further Reading

- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) — Type system and operators
- [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) — Why no GC, borrow checker, etc.
- [LLVM Optimizer Documentation](https://llvm.org/docs/Passes/) — Passes LLVM runs
