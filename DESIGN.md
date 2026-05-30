# Design

## Problem

Single-precision floating-point arithmetic silently loses bits. Classic pathologies include catastrophic cancellation (two nearly-equal numbers subtract to almost zero, destroying relative precision) and accumulated rounding error in iterative algorithms. These bugs are hard to find because the program produces a plausible-looking answer — just the wrong one.

## Approach: Float-to-Double Shadow Values

Every `float` SSA value in the instrumented program is paired with a `double` shadow value computed by performing the same operation in higher precision. After each float operation the pass inserts a runtime check that compares the float result to the double shadow. When they diverge beyond a configurable threshold a diagnostic is emitted.

This is the same fundamental idea used by research tools like Herbgrind and the upstream LLVM NSan prototype, but implemented here as a stand-alone LLVM pass plugin rather than a patched Clang driver, making it easier to build and deploy alongside any Homebrew or system LLVM installation.

### Shadow propagation rules

| IR construct | Shadow rule |
|---|---|
| `float` constant | cast to `double` at compile time |
| `fadd`, `fsub`, `fmul`, `fdiv`, `frem` | same op on double shadows |
| `fneg` | negate double shadow |
| `fptrunc` to `float` | use the source double shadow directly if available |
| `phi` node | shadow phi node with matching incoming edges |
| `select` | shadow select on same condition |
| float `load` | runtime lookup in shadow table by address |
| float `store` | runtime write to shadow table keyed by address |
| direct call returning `float` | thread-local return-shadow slot |
| float argument passing | thread-local argument-shadow slots |
| `memcpy` / `memmove` | runtime shadow-range copy |
| `memset` | runtime shadow-range invalidation |
| `sqrtf`, `sinf`, `cosf`, … (17 functions) | runtime double-precision math model |

### Catastrophic cancellation detection

For `fadd` and `fsub`, the runtime additionally computes the ratio of the largest input magnitude to the result magnitude. When this ratio exceeds `NSAN_CANCELLATION_RATIO` (default 1 × 10⁶) and the result also diverges from the shadow, the diagnostic is labelled *catastrophic cancellation* rather than generic *float/shadow divergence*.

### Memory model

Float addresses are the hash-table keys. Each shadow-table entry stores the `double` shadow value and a validity flag. A secondary byte-level table records which bytes belong to which float entry so that raw-memory operations (`memcpy`, `memset`) can correctly transfer or invalidate shadow state for the affected float-sized regions.

This is object-oriented rather than fully byte-addressable (ASan-style 1:1 byte maps), but it correctly handles the common memory-manipulation patterns that appear in numeric code.

## Alternatives Considered

### 1. MPFR-backed shadow values

**What:** replace the `double` shadow with arbitrary-precision arithmetic via MPFR.

**Trade-off:** detects far more precision loss, including cases where `double` is itself insufficient. Cost is a large external dependency, 10–100× higher instrumentation overhead, and much harder runtime integration. The assignment scope explicitly called for `double` shadows, and a working `double`-based tool already catches the most important class of bugs (catastrophic cancellation) in a presentation-friendly way.

### 2. Patching the Clang driver

**What:** add `numerical` as a first-class sanitizer inside upstream Clang's driver so `-fsanitize=numerical` is handled natively by the compiler rather than by a wrapper script.

**Trade-off:** cleaner user experience, correct handling of multi-stage compilation, and interoperability with other sanitizer flags. Requires modifying Clang source, rebuilding Clang, and maintaining a fork or patch series. The wrapper approach delivers the same user-visible flag with no Clang source changes, which is appropriate for a prototype.

### 3. Valgrind / binary instrumentation

**What:** instrument the binary at load time (Valgrind plugin, Intel SDE, DynamoRIO) rather than at compile time.

**Trade-off:** no source changes or recompilation needed, wider coverage of third-party code. Valgrind-style full shadow memory is 20–100× slower in practice and cannot take advantage of LLVM's type and debug information to produce precise source-level diagnostics. Compile-time instrumentation gives exact file/line/column locations and the compiler's knowledge of which values are floats.

### 4. Source-level rewriting

**What:** use a Clang AST rewriter or source-to-source transformer to insert double-precision shadow code directly in C.

**Trade-off:** no LLVM pass infrastructure needed. However, it cannot track temporaries, SSA values, or compiler-generated code (e.g., vector lowering, intrinsics, inlined math). IR-level instrumentation sees the full, optimised representation that actually executes.

### 5. Fully byte-addressable shadow memory

**What:** maintain a shadow byte for every byte of the program's address space, as ASan does.

**Trade-off:** complete and correct for any pointer arithmetic. Requires either a large contiguous shadow mapping (requires careful virtual-address layout) or a hash table with byte-level granularity. The current object-keyed approach is simpler, covers all the test cases, and correctly handles `memcpy`/`memset` for the common case where float-aligned accesses dominate numeric code.

## Design Decisions

**Thread safety via spin locks.** The shadow table and the one-report-per-site table are protected by C11 `atomic_flag` spin locks. This is correct for prototype-scale multithreaded programs. A production sanitizer would use sharded locking or lock-free structures to reduce contention in highly parallel numeric kernels.

**Duplicate-report suppression.** The runtime hashes (file, line, column, opcode) and records the first report for each site. Subsequent hits at the same site are silently dropped. This prevents a single bad loop from flooding the terminal while still surfacing every distinct source location.

**Text and JSON output.** Human-readable text is the default. Setting `NSAN_REPORT_FORMAT=json` emits one JSON object per line for post-processing by dashboards or CI systems.

**Per-lane vector checking.** Fixed-width float vectors (`<4 x float>`, etc.) are handled by extracting each lane, loading/storing per-lane shadows, and checking each lane independently. This reuses the scalar runtime without a separate vector shadow ABI.
