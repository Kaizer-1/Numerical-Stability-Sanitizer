# Evaluation

## Test Cases

The test suite (`tests/cases/`) contains 19 programs. Each is compiled with `-fsanitize=numerical` and run; the harness checks that at least one `NumericalSanitizer` diagnostic appears on stderr (or, for the two negative cases, that none appears).

Run the full suite:

```bash
python3 tests/run_tests.py --clang build/numerical-clang
```

### Test inventory

| # | File | Pattern | Expected result |
|---|---|---|---|
| 01 | `01_add_then_subtract.c` | `(big + tiny) - big` | catastrophic cancellation |
| 02 | `02_subtract_then_subtract.c` | `(big - tiny) - big` | catastrophic cancellation |
| 03 | `03_scaled_intermediate.c` | scaled intermediate cancel | float/shadow divergence |
| 04 | `04_quarter_increment.c` | `(big + 0.25f) - big` | catastrophic cancellation |
| 05 | `05_select_operand.c` | select between two cancel paths | catastrophic cancellation |
| 06 | `06_phi_loop.c` | loop-carried cancellation via PHI | catastrophic cancellation |
| 07 | `07_store_load_scalar.c` | store then reload then cancel | catastrophic cancellation |
| 08 | `08_store_load_array.c` | array store/load then cancel | catastrophic cancellation |
| 09 | `09_double_to_float_truncation.c` | `fptrunc` precision loss | float/shadow divergence |
| 10 | `10_division_intermediate.c` | division then cancellation | catastrophic cancellation |
| 11 | `11_product_then_cancel.c` | product magnifies then cancels | catastrophic cancellation |
| 12 | `12_function_shadow_abi.c` | cross-function shadow propagation | catastrophic cancellation |
| 13 | `13_libm_sqrt_shadow.c` | `sqrtf` shadow model | float/shadow divergence |
| 14 | `14_more_math_models.c` | `sinf`, `expf`, `logf` shadows | float/shadow divergence |
| 15 | `15_vector_float_ops.c` | `<4 x float>` per-lane check | catastrophic cancellation |
| 16 | `16_memcpy_shadow_transfer.c` | shadow preserved across `memcpy` | catastrophic cancellation |
| 17 | `17_memmove_shadow_transfer.c` | shadow preserved across `memmove` | catastrophic cancellation |
| 18 | `18_memset_invalidation.c` | `memset` invalidates shadow (no stale noise) | **no diagnostic** |
| 19 | `19_integer_store_invalidation.c` | integer write invalidates float shadow | **no diagnostic** |

Cases 01–17 are positive-detection tests: the sanitizer must fire. Cases 18–19 are correctness tests: the sanitizer must *not* fire, verifying that invalidation logic prevents false positives.

### Key test descriptions

**01 — `(big + tiny) - big`**

`big = 1e8f`, `tiny = 1.0f`. The float addition `big + tiny` rounds back to `big` at `float` precision, so the subtraction produces `0.0f`. The double shadow correctly produces `1.0`. The sanitizer reports catastrophic cancellation with `cancellation_ratio ≈ 1e8`.

**06 — PHI loop**

The same `(big + tiny) - big` pattern is unrolled across loop iterations via a PHI node to verify that the pass correctly propagates shadow state through loop back-edges.

**12 — Function-call shadow ABI**

A `noinline` helper `float subtract(float x, float y)` performs the subtraction. The pass seeds argument shadows into thread-local slots before the call and the callee reads them on entry, demonstrating interprocedural shadow propagation without LLVM link-time optimisation.

**15 — Vector float ops**

Uses a GCC/Clang `__attribute__((vector_size(16)))` `<4 x float>` type. The pass instruments each lane independently and the runtime reports which lane triggered the cancellation.

**18 / 19 — Negative cases**

Verify that `memset` on a float array and an integer store overlapping a float address both correctly invalidate the corresponding shadow entries so a subsequent reload does not fire a false positive based on stale state.

## Benchmark Comparison

The benchmark harness (`benchmarks/run_benchmarks.py`) compiles each program twice — once without instrumentation (`-O2`) and once with (`-fsanitize=numerical -O2`) — and reports the median execution time over multiple iterations.

Run:

```bash
python3 benchmarks/run_benchmarks.py --clang build/numerical-clang --iterations 3
```

### Benchmark programs

| Program | Description | Key operation |
|---|---|---|
| `dot_product.c` | Dot product of two large float arrays | `fadd` + `fmul` in a tight loop |
| `harmonic.c` | Harmonic sum `1/1 + 1/2 + … + 1/N` | `fdiv` + `fadd` in a tight loop |
| `matmul.c` | Naïve matrix multiply (N×N floats) | `fmul` + `fadd` with two nested loops |

### Measured overhead (Homebrew LLVM 22.1.4, 3 iterations, macOS arm64)

| Program | Baseline | Instrumented | Overhead |
|---|---:|---:|---:|
| `dot_product.c` | 0.005989 s | 0.226089 s | **37.75×** |
| `harmonic.c` | 0.011756 s | 0.449429 s | **38.23×** |
| `matmul.c` | 0.008148 s | 0.041147 s | **5.05×** |

### Interpretation

**Dot product and harmonic sum** are memory-bandwidth or scalar-loop bound. Every `fadd`/`fmul` becomes a pair of shadow operations plus a hash-table store and load, which dominates the loop body. The ~38× overhead is expected for a hash-table-backed shadow scheme on a tight loop.

**Matrix multiply** benefits from the compiler vectorising the inner loop into `<N x float>` operations. The pass instruments these as vector shadow ops, which are cheaper per-operation than N individual hash-table calls, resulting in a much lower overhead (~5×).

**Comparison to related tools:**
- Valgrind Memcheck: ~10–20× overhead on typical programs (no float shadow)
- Herbgrind (Valgrind-based float analysis): often 100–1000× overhead
- This sanitizer: 5–40× overhead depending on float-operation density

The overhead is in the expected range for a compile-time shadow-value tool and is acceptable for debugging sessions on moderate-sized programs.

## Failure Cases

The sanitizer correctly does **not** fire for programs with no numerical instability. The two negative test cases (18 and 19) demonstrate this. Additionally, the sanitizer uses duplicate-report suppression so a single bad loop emits one diagnostic per source site rather than flooding the output.

The sanitizer will **miss** instability in:
- Code that uses `double` throughout (shadow is also double — no gap to detect)
- Libraries compiled without instrumentation, unless math-model wrappers cover the functions used
- Indirect calls through function pointers (prototype ABI only covers direct calls)
- Scalable vector types (only fixed-width vectors are instrumented)
