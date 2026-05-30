# Implementation

## Overview

The sanitizer is split into two components that communicate through a small C API defined in `include/NumericalSanitizer/NumericalSanitizer.h`:

1. **LLVM pass plugin** (`lib/NumericalSanitizerPass.cpp`) — runs at compile time, transforms the IR
2. **C runtime library** (`runtime/numerical_sanitizer_runtime.c`) — linked into the instrumented binary, provides shadow memory and diagnostic logic

A `numerical-clang` wrapper script (`tools/numerical-clang.in`, generated into `build/numerical-clang`) presents the user-facing `-fsanitize=numerical` flag by stripping it, injecting `-fpass-plugin=…` and `-l nsan_runtime`, and forwarding everything else to `clang`.

## LLVM Pass Plugin

### Registration

`NumericalSanitizerPass` is an LLVM new-pass-manager `FunctionPass` registered via `llvmGetPassPluginInfo()`. It hooks into two pipeline extension points:

- `registerPipelineParsingCallback` — allows `numerical-sanitizer` as an explicit pass name
- `registerPipelineStartEPCallback` — automatically runs the pass at the start of the optimisation pipeline for every function

The pass skips functions that are declarations only and functions whose names start with `__nsan_` (the runtime itself, to prevent recursive instrumentation).

### Shadow Map

The pass maintains a `DenseMap<Value*, Value*>` called `Shadow` that maps each instrumented `float` SSA value to its corresponding `double` shadow value within the same function. Lookups fall back to `FPExt` (widening cast) for values that have no explicit shadow entry, such as function arguments not yet seeded from the call-site ABI or float constants.

### Instruction Handling (in order of the worklist)

**PHI nodes** are handled first in a two-pass scheme: a shadow `PHINode` is created in the first pass (to handle cycles), and incoming shadow values are filled in the second pass once the shadow of each predecessor value is known.

**`MemCpyInst` / `MemMoveInst` / `MemSetInst`** (LLVM memory intrinsics) are handled before general instructions. After each intrinsic the pass inserts a call to `__nsan_copy_shadow_bytes`, `__nsan_move_shadow_bytes`, or `__nsan_forget_shadow_bytes` respectively.

**`LoadInst`** on a `float` type: after the load the pass inserts a call to `__nsan_shadow_load_float(ptr, actual)`, which returns the stored shadow or falls back to `(double)actual`. For `<N x float>` vectors the pass lowers this to N per-lane GEP + scalar shadow-load calls, building a `<N x double>` shadow vector via `InsertElement`.

**`StoreInst`** on a `float` type: before the store the pass inserts a call to `__nsan_shadow_store_float(ptr, shadow)`. For non-float stores the pass calls `__nsan_forget_shadow_bytes(ptr, size)` to conservatively invalidate any shadow that might overlap the written range.

**`BinaryOperator`** (`fadd`, `fsub`, `fmul`, `fdiv`, `frem`): after the float op the pass emits the same op on the double shadows and calls `__nsan_check_binary_float(actual, shadow, lhs_shadow, rhs_shadow, op, file, func, line, col)`. The binary check is used (rather than the simpler unary check) so the runtime can compute the cancellation ratio from the input magnitudes.

**`UnaryOperator`** (`fneg`): shadow negation + `__nsan_check_float`.

**`SelectInst`** on float: shadow select on the same condition + `__nsan_check_float`.

**`CastInst`** (`fptrunc` to `float`): if the source is already a `double` or `<N x double>` the source value becomes the shadow directly (no loss). Otherwise the float result is widened with `FPExt`. Check via `__nsan_check_float`.

**`CallInst`**: the pass classifies calls as follows:

1. Calls to `__nsan_*` — skipped entirely.
2. Calls to `memcpy`, `memmove`, `memset` by name — handled with the same shadow-range calls as the LLVM intrinsic case (for libc calls not lowered to intrinsics).
3. Calls to recognised float math functions (`sqrtf`, `sinf`, `cosf`, `tanf`, `expf`, `logf`, `powf`, `fabsf`, `floorf`, `ceilf`, `fmodf`, `atanf`, `atan2f`, `asinf`, `acosf`, `sinhf`, `coshf`) and their LLVM intrinsic equivalents — the pass calls the corresponding `__nsan_shadow_*f` runtime model to compute the shadow.
4. All other calls — the pass seeds argument shadows into thread-local slots via `__nsan_set_arg_shadow(index, shadow)` before the call, clears them via `__nsan_clear_arg_shadows(count)` after the call, and reads the return shadow via `__nsan_get_return_shadow(actual)` after the call.

**`ReturnInst`** returning `float`: the pass inserts `__nsan_set_return_shadow(shadow)` before the return so callers can consume it.

### Source Location

Debug info is extracted from the `DILocation` attached to each instruction (`Loc->getFilename()`, `Loc->getLine()`, `Loc->getColumn()`). These are materialised as global string pointers and integer constants passed to the check functions.

### Compatibility

The pass uses `starts_with` vs. `startswith` selected by `LLVM_VERSION_MAJOR` to support both LLVM ≤ 21 and LLVM ≥ 22 API names. The pass-plugin header location similarly has a compile-time fallback for older LLVM trees.

## Runtime Library

### Shadow Table

The primary shadow table is a flat open-addressed hash table of `1 << 20` (`~1M`) slots. Each `ShadowSlot` stores:
- `key` — the float pointer (the address of the float in the target program's memory)
- `value` — the `double` shadow
- `state` — `EMPTY`, `KNOWN`, or `UNKNOWN`

`UNKNOWN` entries act as tombstones for addresses whose shadow has been invalidated (e.g., by a `memset`).

A second open-addressed table (`ShadowByteTable`) records which bytes belong to which float entry. Each `ShadowByteSlot` stores the byte address, the base float address, and the offset within the float. This table is used by `__nsan_shadow_load_float` to verify that all four bytes of the float address are covered by a consistent shadow entry before trusting the lookup.

Both tables are protected by a single `atomic_flag` spin lock (`ShadowTableLock`).

### Thread-Local Shadow ABI

```c
static _Thread_local double ArgShadows[64];
static _Thread_local unsigned char ArgShadowValid[64];
static _Thread_local double ReturnShadow;
static _Thread_local unsigned char ReturnShadowValid;
```

`__nsan_set_arg_shadow(i, s)` writes slot `i` and marks it valid. `__nsan_get_arg_shadow(i, actual)` reads slot `i` if valid (clearing it) or falls back to `(double)actual`. `__nsan_set_return_shadow` / `__nsan_get_return_shadow` work the same way for return values.

### Divergence Check

```c
bool diverged(float actual, double shadow, ...) {
    double abs_error = fabs((double)actual - shadow);
    double rel_error = abs_error / fmax(fabs(shadow), 1.0);
    if (isnan different or isinf different) return true;
    return abs_error > AbsErrorThreshold && rel_error > RelErrorThreshold;
}
```

The check requires *both* thresholds to be exceeded to reduce false positives for very small numbers where absolute error is tiny but relative error is large against a near-zero shadow.

### Catastrophic Cancellation

In `__nsan_check_binary_float` the runtime computes:

```c
double cancellation_ratio = max(|lhs_shadow|, |rhs_shadow|) / max(|shadow|, DBL_MIN);
```

If this ratio ≥ `NSAN_CANCELLATION_RATIO` (default 10⁶) **and** the result diverges, the diagnostic kind is `catastrophic cancellation`.

### Duplicate Suppression

A separate `ReportedSites` table (`1 << 14` slots) hashes `(file, line, column, opcode)`. The first report at each site is emitted; subsequent hits are silently dropped. This prevents loops from producing thousands of identical warnings.

### Math Models

Each `__nsan_shadow_*f` function takes the float result and its double shadow argument(s) and recomputes using the double-precision `<math.h>` function (e.g., `sqrt(x)` for `__nsan_shadow_sqrtf`). The float result parameter is accepted but unused (present to allow future MPFR-style backends to validate the float result independently).

### Memory Range Operations

`__nsan_copy_shadow_bytes(dst, src, size)` and `__nsan_move_shadow_bytes` iterate the shadow table, collect all entries whose key falls fully within `[src, src+size)`, invalidate the destination range, then write those entries at `[dst + offset, …]`.

`__nsan_forget_shadow_bytes(addr, size)` iterates the table and marks every overlapping slot `UNKNOWN`, then tombstones the corresponding byte-table entries.

## Build System

`CMakeLists.txt` uses `add_llvm_pass_plugin` (from `AddLLVM.cmake`) to build the shared-library plugin and a standard `add_library(STATIC)` for the runtime. `configure_file` substitutes the plugin and runtime paths into `tools/numerical-clang.in` to produce `build/numerical-clang`. `enable_testing()` + `add_test()` wires the test runner into CTest.

## File Map

| File | Role |
|---|---|
| `lib/NumericalSanitizerPass.cpp` | LLVM function pass — full IR transformation |
| `runtime/numerical_sanitizer_runtime.c` | C runtime — shadow memory, checks, diagnostics |
| `include/NumericalSanitizer/NumericalSanitizer.h` | Shared API declarations (op codes, runtime prototypes) |
| `tools/numerical-clang.in` | Template for the compiler wrapper script |
| `tools/nsan_dashboard.py` | Terminal dashboard for live demo |
| `CMakeLists.txt` | Build configuration |
| `tests/run_tests.py` | Test harness |
| `benchmarks/run_benchmarks.py` | Benchmark harness |
