# Aster and PHP implementation grounding sweep

## Purpose

This review compares Aster's implementation with the current PHP development
source as a grounding exercise. PHP is treated as what it is: a mature,
human-maintained language implementation whose decisions have been exercised by
large production workloads, multiple operating systems, extensions, embedders,
security research, fuzzers, and decades of compatibility pressure.

The comparison is not intended to make Aster imitate PHP's language semantics.
PHP is dynamically typed, reference-counted, cycle-collected, bytecode-driven,
and shaped by a very different application ecosystem. Aster is statically
typed, deterministic, no-GC, C17-oriented, and deliberately narrower.

The useful questions are:

- Does Aster have a coherent implementation architecture?
- Which parts look like defensible engineering rather than generated filler?
- Where has AI authorship produced duplicated, stringly, or weakly integrated
  code?
- Which implementation shortcuts are acceptable for a young language?
- Which shortcuts must be removed before production use?
- What engineering disciplines can Aster learn from PHP without becoming PHP?

The separate [manual memory management evaluation](manual-memory-management-evaluation.md)
examines Aster's deterministic cleanup model in depth. This document does not
treat manual memory management or the absence of a tracing collector as a
defect.

## Compared snapshots and method

The PHP source was cloned into `../comparison/php-src` from the official
`php/php-src` repository. The inspected snapshot was:

- commit: `c6e74fc13189afbc7c2a02a2af42f9ccf4dabc33`;
- branch: `master`;
- source version: PHP `8.6.0-dev`;
- commit subject: `ext/dba: applied fixers to improve test robustness (#23027)`.

The version is defined in
[php_version.h](../../comparison/php-src/main/php_version.h). The PHP clone was
kept unmodified.

The review inspected representative implementation paths rather than merely
comparing README claims:

- lexical analysis and parsing;
- AST and type representation;
- symbol and type resolution;
- checked IR and verification;
- bytecode encoding and dispatch;
- generated C;
- built-in and extension metadata;
- values, allocation, copying, and cleanup;
- embedding and process lifecycle;
- optimizer structure;
- tests, CI, and fuzzing.

Aster was also configured in a separate `/tmp` build with strict warnings,
warnings-as-errors, AddressSanitizer, and UndefinedBehaviorSanitizer. No Aster
implementation file was changed as part of the comparison.

The repositories differ enormously in scale. At the time of review, the
relevant Aster C, header, build, documentation, and standard-library sources
were roughly 62,000 lines. PHP contained roughly two million relevant C,
header, lexer, and parser lines, plus more than 22,000 `.phpt` tests. Raw size is
not a quality score, but it establishes that equal feature breadth or hardening
would be an unreasonable expectation.

## Verdict

Aster is not wholesale AI slop. Its central architecture is coherent:

```text
source
  -> parsed AST
  -> checked typed AST
  -> typed control-flow IR
  -> IR verification
  -> generated C17 or typed bytecode
```

For a compact experimental language, Aster has substantial engineering behind
it. The ownership model is deliberate, diagnostics retain source information,
the IR is typed, two backends exercise the same lowering, generated C is built
with strict warnings, sanitizers are integrated, and the test suite covers far
more than arithmetic demonstrations.

Aster is nevertheless prototype-grade rather than PHP-grade. Its AI-shaped
debt is concentrated at boundaries:

- structured concepts are sometimes serialized to strings and parsed again;
- built-in metadata is duplicated through name comparisons and magic numbers;
- the documented IR boundary is bypassed by backends;
- fixed-size encodings and arrays are not always validated consistently;
- public contracts and runtime behavior sometimes disagree;
- process-global configuration weakens embedding;
- build and test registration remain manually curated;
- low-level representation invariants have not yet been hardened uniformly.

The concise assessment is:

| Dimension | Assessment |
| --- | --- |
| Architectural thesis | Sound and promising. |
| Compiler organization | Coherent, with unfinished semantic boundaries. |
| Runtime implementation | Serious prototype; not production-hardened. |
| AI-shaped debt | Material but localized, not pervasive nonsense. |
| Testing for project size | Strong. |
| Operational maturity | Far below PHP, as expected. |
| Production readiness | No. |

PHP wins overwhelmingly on invariant discipline, centralized/generated
metadata, lifecycle engineering, allocator maturity, testing scale, fuzzing,
platform coverage, and operational history. Aster sometimes has a cleaner
high-level thesis because it is not constrained by PHP's dynamic semantics or
compatibility history. A cleaner thesis does not yet equal a more trustworthy
implementation.

## Architecture comparison

| Area | Aster | PHP |
| --- | --- | --- |
| Frontend | Handwritten lexer, parser, and checker | re2c scanner and Bison parser |
| Semantic representation | Typed CFG-style IR with explicit cleanup operations | `zend_op_array` bytecode over dynamic `zval` values |
| Execution | Portable C17 plus a development bytecode VM | Generated specialized VM, opcache optimizer, and optional JIT |
| Type/value model | Static types and type-directed cleanup | Dynamic tagged values, reference counting, COW, and cycle collection |
| Metadata | Several handwritten name lists and numeric mappings | Stub declarations and VM definitions generate multiple consumers |
| Allocation | Ordinary allocation, deterministic drops, narrow reference counting, explicit arenas | Request allocator, refcounting, cycle GC, bulk request teardown, debug modes |
| Embedding | Compact public API with some process-global configuration | SAPI, module, engine, and request lifecycles with ZTS support |
| Validation | Hundreds of tests, strict C warnings, ASan and UBSan | More than 22,000 `.phpt` tests, platform CI, scheduled jobs, and fuzz targets |

PHP should not be treated as a template for Aster's source language. Aster does
not need PHP's dynamic `zval` semantics, tracing of cyclic value graphs,
copy-on-write arrays, macro density, JIT, or accumulated compatibility behavior.
It should copy PHP's engineering habits: canonical metadata, stable
representations, explicit lifecycles, centralized invariants, discoverable
tests, fuzzing, and hostile validation.

## Strongest demonstrated defect

The sanitizer-enabled Aster build completed successfully. The full CTest run
contained 342 tests:

- 341 passed;
- 1 failed;
- the failure was `system_io_surface_generated_c`.

UBSan reported that
`native_path_change_extension_value` in
[vm_builtins.c](../src/vm_builtins.c) passed a null pointer to `memcpy`:

```c
memcpy(changed + cursor, extension.data, extension.length);
```

An empty Aster string/view can be represented as:

```text
data = NULL
length = 0
```

Although the length is zero, the C library contract does not generally permit
an invalid pointer argument. Consequently this is undefined behavior rather
than a harmless no-op.

The defect is important for two reasons:

1. It is a real sanitizer finding in an existing test, not a hypothetical code
   smell.
2. It exposes a runtime-wide invariant that has not been settled centrally:
   whether empty data always has a valid non-null address or whether every leaf
   operation must guard zero length.

PHP's [zend_string](../../comparison/php-src/Zend/zend_string.h) uses owned
inline storage and a canonical empty string representation. Individual PHP
operations therefore do not have to rediscover whether a zero-length string's
data pointer may be null.

Aster's documentation treats a complete sanitizer-clean test run as a release
gate. The current snapshot does not satisfy that gate.

## High-confidence implementation debt

### 1. Type syntax becomes a string and is parsed again

[parser.c](../src/parser.c) recognizes type grammar but then constructs
normalized strings such as:

```text
Option<T>
Result<T, E>
fn(A, B)->C
*mut T
[T; N]
```

[checker_types.c](../src/checker_types.c) later resolves those strings using
`strcmp`, prefix checks, delimiter searches, substring allocation, and recursive
reparsing.

This is a weak compiler boundary. The parser has already understood the
structure and should preserve it in a dedicated type-syntax representation. A
structural `TypeSyntax` tree would retain nested structure and source spans and
could be resolved once into semantic `Type` objects.

The current approach creates avoidable risks:

- nested generic parsing can drift between parser and checker;
- diagnostics lose precise component spans;
- adding type grammar requires coordinated string-format changes;
- mutability and pointer shape can depend on prefix spelling;
- malformed internal strings become checker problems rather than parser errors.

This is one of the clearest AI-shortcut patterns in the repository: structured
data is flattened because strings are convenient, then complexity reappears as
manual parsing elsewhere.

PHP's frontend is not inherently superior because it uses generators, but it
does preserve a proper syntax structure through its grammar and AST. Its
[zend_ast.h](../../comparison/php-src/Zend/zend_ast.h) gives syntax an explicit
representation rather than using formatted strings as an internal protocol.

### 2. Built-in semantics are duplicated and keyed by magic values

Built-in knowledge is distributed across several independent implementation
sites:

- [checker_calls.c](../src/checker_calls.c) contains large lists of function-name
  comparisons to decide argument borrowing and special behavior;
- [ir_bytecode.c](../src/ir_bytecode.c) maps built-in names to many negative
  numeric identifiers;
- [vm_builtin_exec.c](../src/vm_builtin_exec.c) dispatches those negative IDs
  through a long chain of conditions;
- the C backend contains corresponding special cases;
- standard-library source separately declares the public functions.

This is a synchronization trap. A built-in's name, arity, borrowing policy,
result type, numeric identity, VM behavior, and C behavior can disagree without
one compiler error pointing to the missing entry.

PHP handles a comparable problem with declarative sources. Extension `.stub.php`
files generate argument information and function-entry declarations. VM
semantics are declared in
[zend_vm_def.h](../../comparison/php-src/Zend/zend_vm_def.h) and transformed by
[zend_vm_gen.php](../../comparison/php-src/Zend/zend_vm_gen.php) into opcode
tables and specialized executor forms.

Aster needs one canonical built-in registry from which the checker, IR lowering,
bytecode IDs, VM dispatch metadata, C lowering metadata, and validation tests
are derived. That does not require exposing generation machinery to Aster
programs; it is ordinary compiler implementation hygiene.

### 3. The documented IR boundary is not yet true

[architecture.md](architecture.md) says:

> Backends must consume the IR, never inspect or reinterpret the raw AST.

`IrType.checked_type` in [internal.h](../src/internal.h) is likewise documented
as a bootstrap link that backends never interpret.

The implementation still violates that intended boundary:

- [c_backend_css.c](../src/c_backend_css.c) traverses raw function AST bodies to
  collect static CSS;
- [c_backend_types.c](../src/c_backend_types.c) reads checked-type metadata;
- C emission accesses source function declarations;
- bytecode lowering also consults source declarations and checked types.

Therefore the IR is not a closed semantic product. Some behavior still exists
only in frontend structures, and backends can diverge when they reinterpret it.

The overall move toward one typed IR remains correct. The fix is to finish it:
put parameter modes, visibility/export information, CSS metadata, destructor
identity, callable properties, and every other backend-required fact into owned,
verified IR data. Backend structs should not retain raw AST declarations merely
for convenience.

### 4. Fixed limits are not uniformly validated

Aster deliberately uses several hard bounds:

- 32 nested loop, element, or exception contexts in parts of the compiler;
- 32-bit masks for borrowed, `ref`, and `out` arguments;
- 128 VM frames;
- 1,024 operand values per VM frame;
- compact bytecode fields with limited slot or argument capacity.

Hard limits are acceptable. C implementations routinely use bounded structures
when the limits are explicit, checked, documented, and tested at their exact
boundary.

The problem is inconsistent validation. In [vm.c](../src/vm.c), initial
argument setup tests a 32-bit borrowed mask using:

```c
UINT32_C(1) << (unsigned)i
```

without first establishing `i < 32`. A normal function with more than 32
arguments appears able to reach an undefined shift even when those arguments
are not borrowed. This is a likely latent defect based on direct source
inspection; a new reproducer was not created during the no-code comparison.

Every fixed encoding should have:

- a named constant;
- a frontend or bytecode-construction diagnostic;
- verifier enforcement;
- decoder/runtime defensive enforcement;
- boundary tests at maximum minus one, maximum, and maximum plus one.

### 5. VM frame allocation is deliberately simple but too coarse

The VM finds the maximum local count across all functions and uses that as the
stride for every one of its 128 synchronous frames. It also reserves 1,024
`LangValue` operand slots for every frame. With the current value size, the
operand storage alone is approximately 3 MiB.

This provides simple indexing and predictable upper bounds. It is not absurd
for an initial VM. Its weakness is that one pathological function inflates the
local allocation for every possible frame, while tiny functions receive the
same operand capacity as large ones.

PHP's [zend_op_array](../../comparison/php-src/Zend/zend_compile.h) records
per-function variables and temporary requirements. Execute frames are sized for
the function actually invoked, and the VM stack grows according to runtime
needs.

Aster should calculate per-function frame requirements during bytecode lowering
and allocate or reuse frames accordingly. It can remain bounded and
deterministic without reserving the maximum layout for every frame.

### 6. The embedding allocation contract contradicts the allocator

[lang.h](../include/lang/lang.h) says `lang_vm_new()` returns `NULL` on a
recoverable allocation failure. [vm_values.c](../src/vm_values.c) implements it
using a helper that prints an error and calls `exit(2)` when `calloc` fails.

Fail-fast allocation can be reasonable for a standalone generated executable.
It is not interchangeable with failure-returning behavior inside an embedding
library. A host process must be able to rely on the public contract.

The API should separate policies explicitly:

- standalone executable allocation may fail fast;
- embedding constructors documented as recoverable must return failure;
- language-level fallible operations should return their declared error;
- impossible internal states should trap consistently.

### 7. Process-global configuration weakens reentrancy

[common.c](../src/common.c) stores configured standard-library and executable
paths in mutable static globals. Public setter calls therefore affect every
subsequent compiler operation in the process.

That prevents cleanly independent compiler instances and complicates concurrent
embedding. PHP has extensive global state too, but it organizes that state into
engine, module, request, and optional thread-local lifecycles.

Aster should introduce an explicit compiler/project context owning:

- standard-library discovery configuration;
- executable and project paths;
- target information;
- diagnostics and allocation state;
- module caches and search paths.

Convenience process-global wrappers can remain for the CLI, but the core API
should be reentrant.

### 8. Test registration and validation are too manually curated

Aster's [CMakeLists.txt](../CMakeLists.txt) individually describes a large
number of tests and generated-C fixtures. This has produced meaningful coverage,
but the manifest itself becomes difficult to review and maintain.

PHP's `.phpt` runner discovers tests close to each extension or engine
subsystem. PHP also has scheduled and platform-specific workflows and multiple
fuzz targets under
[sapi/fuzzer](../../comparison/php-src/sapi/fuzzer).

Aster should preserve its existing categories while moving repeated mechanics
into data-driven discovery:

- positive run tests;
- expected diagnostic tests;
- VM/generated-C comparison tests;
- leak-checked generated-C tests;
- integration tests with explicit prerequisites;
- fuzz corpora for lexer, parser, checker, IR verifier, bytecode decoder, and VM.

The repository did not contain visible CI workflow configuration or a fuzzer at
the inspected snapshot. Sanitizers being available locally is good; running
them continuously is what turns them into an engineering control.

## Secondary prototype concerns

These findings are real but less urgent than the boundary problems above.

### Linear lookup

Function lookup, duplicate checks, generic-instantiation lookup, and IR type
interning contain linear scans. That is reasonable while the compiler and
programs are small. It will become expensive for substantial multi-module
applications, especially when performed repeatedly during type checking.

PHP uses `HashTable` pervasively for dynamic symbol and metadata lookup. Aster
should introduce hash-based symbol tables and type interning after correctness
boundaries are stable. Doing it earlier would optimize around interfaces that
still need redesign.

### Module concatenation

The loader combines source files into one buffer and records source segments for
diagnostics. This is compact and functional, but it means module separation is
partly reconstructed through segment and module-name metadata rather than
represented as independent compilation units from the beginning.

This is acceptable for a small bootstrap compiler. It becomes limiting for
incremental compilation, caching, parallel checking, isolated module lifetimes,
and precise dependency invalidation.

### Documentation and version drift

At the reviewed snapshot, project version references and status documents were
not entirely synchronized, and some layout descriptions still reflected older
representations. This is unsurprising during a large refactor, especially one
that removed the old direct compiler, but release documentation must describe
the executable implementation rather than the intended destination.

## What Aster gets right

The comparison found substantial work that does not look sloppy.

### The central compilation pipeline is appropriate

A typed AST followed by verified typed IR is a defensible foundation. It gives
Aster one place to decide types, checked arithmetic, control flow, exception
edges, ownership operations, aggregate layout, and destructor identity before
backend-specific lowering.

Using portable C17 as the primary deployment backend is also rational. It gives
Aster mature machine optimization, platform support, debugging tools, linkers,
sanitizers, and library interoperability without requiring a custom native code
generator or LLVM dependency.

### Deterministic cleanup is designed seriously

Moves, clones, drops, cleanup plans, destructor identities, and exceptional
cleanup edges are represented deliberately. Generated C and the VM both track
whether an owning local remains initialized. Cleanup tests cover early returns,
loops, errors, exceptions, nested destructors, native handles, and traps.

This is a strength, not a deficiency relative to PHP. Aster should retain its
manual-memory-backed deterministic model and harden it according to the
companion memory evaluation.

### Diagnostics are treated as a product feature

AST nodes retain source spans. Diagnostics support contextual information rather
than only emitting generic parse failures. Invalid layouts, unsupported clones,
nesting limits, unsafe operations, and backend restrictions are often diagnosed
explicitly.

### Strict native compilation is built in

Aster itself and generated C are compiled with demanding warning sets,
warnings-as-errors, ASan, and UBSan support. Generated translation units are
actually compiled and executed in tests. That is far stronger than testing only
the VM while assuming generated C is equivalent.

### Test breadth is strong for the project size

The suite covers:

- lexer, parser, checker, IR, verifier, and bytecode behavior;
- expected-error fixtures;
- generated C and VM execution;
- deterministic cleanup and leak checking;
- aggregates, generics, modules, functions, and exceptions;
- files, processes, SQLite, HTTP, typed HTML, CSS, browser/Wasm, and project
  manifests;
- selected integration paths using real adapters.

The sanitizer failure should not erase this strength. It demonstrates why the
test infrastructure is valuable: it found a genuine runtime invariant bug.

### The project records rejected experiments

Architecture and decision documents discuss non-goals, backend priorities,
measured dispatch experiments, removal of superseded paths, and the decision not
to add a custom JIT. Removing an optimization that regresses a supported
compiler is evidence of restraint.

### Removing the parallel direct compiler was correct

Maintaining an AST-to-bytecode path alongside typed-IR-to-C would have created
two semantic implementations. Deleting the legacy path and routing execution
through IR is directionally correct. The remaining task is to make the IR
boundary genuine rather than merely central.

## What PHP demonstrates

PHP's implementation should not be romanticized. It contains heavy macro use,
large generated executor files, global engine state, reference counting,
copy-on-write edge cases, cycle collection, and compatibility constraints that
Aster should not volunteer to acquire.

Its maturity is visible in how it manages unavoidable complexity:

- VM handlers originate from a canonical definition and are generated into
  specialized dispatch forms.
- Function signatures and argument information originate from extension stubs.
- `zval`, `zend_string`, `HashTable`, and `zend_op_array` have stable,
  centralized representation contracts.
- The allocator has request lifetimes, size classes, limits, debug behavior, and
  bulk teardown.
- The engine distinguishes startup, module, request, and shutdown phases.
- Tests live near their owning extensions and run across platforms and build
  modes.
- Fuzzers directly attack parsers, executors, serializers, JSON, and JIT paths.
- Generated complexity is accepted where it reduces duplicated handwritten
  complexity.

The lesson is not that mature code is visually small or elegant. The lesson is
that complexity should have ownership, one source of truth, stable invariants,
and continuous adversarial validation.

## Grounded priority order

Before adding major language surface, Aster should complete the following work
in order.

### Priority 0: restore the correctness gate

1. Establish one empty-string, buffer, and slice invariant.
2. Fix the sanitizer-detected `memcpy` defect.
3. Audit zero-length memory operations across both backends and every native
   adapter.
4. Run the complete GCC and Clang sanitizer suite continuously.

### Priority 1: close semantic boundaries

1. Replace formatted type-name reparsing with a structural type-syntax tree.
2. Make typed IR fully backend-facing and remove backend access to AST
   declarations and checked types.
3. Give every IR type explicit copy, drop, layout, parameter-mode, destructor,
   and backend metadata.
4. Verify those contracts before either backend runs.

### Priority 2: remove duplicated semantic catalogs

1. Introduce one declarative built-in registry.
2. Generate or validate checker policies, bytecode IDs, VM dispatch metadata,
   C-lowering metadata, arities, and tests from it.
3. Remove magic negative IDs from human-maintained control flow.
4. Keep VM opcode declarations and names in one canonical definition.

### Priority 3: harden bounded execution

1. Name and document every fixed limit.
2. Diagnose limit violations during checking or bytecode construction.
3. Verify encoded ranges and defend again during decoding/execution.
4. Use per-function local and operand requirements rather than one maximum
   stride for every frame.

### Priority 4: make embedding lifecycle explicit

1. Move standard-library paths and compiler configuration into instance-owned
   contexts.
2. Align allocation failures with public API contracts.
3. Define compiler, VM, module, request, and shutdown lifetimes.
4. Preserve convenience CLI globals only as wrappers around the explicit API.

### Priority 5: institutionalize hostile testing

1. Add repository CI across GCC and Clang, debug/release modes, and sanitizer
   configurations.
2. Add fuzz targets for every untrusted structural boundary.
3. Make repetitive test registration data-driven.
4. Add allocation-failure, maximum-limit, malformed-bytecode, and long-running
   memory tests.
5. Keep VM/generated-C differential behavior as a release gate.

### Priority 6: improve scale after correctness

1. Replace repeated linear symbol lookups with scoped hash tables.
2. Intern structural types through a canonical hash-based table.
3. Introduce independent module units and caching only when real projects demand
   them.
4. Measure allocator traffic, compiler time, generated-code size, VM frame use,
   and application peak memory before optimizing further.

## Final assessment

Aster is a credible compiler prototype with a real architecture. Its AI origin
is visible, especially where the implementation reaches across subsystem
boundaries or repeats knowledge that should be declarative. The result is not an
incoherent pile of generated code. Large portions show consistent intent,
careful testing, and sensible restraint.

The correct response is neither to dismiss Aster nor to declare it production
ready. It should be treated as a promising implementation entering a hardening
phase.

PHP establishes the appropriate standard for that phase: not PHP's feature set
or runtime semantics, but its discipline. Aster needs fewer sources of truth,
closed intermediate representations, stable low-level invariants, explicit
lifecycle ownership, automated test discovery, continuous sanitizers, and
fuzzing.

If those changes are made before broad feature expansion, Aster's central
thesis can survive contact with production. If they are deferred while the
language surface continues to grow, the localized AI-shaped debt will spread
into every backend and become much more expensive to remove.
