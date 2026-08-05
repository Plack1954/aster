# Aster and Go implementation grounding sweep

## Purpose and scope

This review compares Aster with the current development source of Go. Go is a
particularly useful benchmark because its canonical repository contains a
production compiler, SSA optimizer, architecture backends, assembler, linker,
runtime, garbage collector, standard library, testing framework, fuzzing
support, and distribution test runner.

The comparison does **not** assume that Aster should adopt Go's language design
or garbage collector. Aster's deterministic manual-memory model remains an
intentional strength. The useful comparison is engineering organization:

- whether structured information remains structured;
- whether semantic metadata has one source of truth;
- whether phase boundaries are explicit and checked;
- whether generated complexity is derived from readable declarations;
- whether limits and runtime invariants are defended;
- whether testing is built into the development process.

For Aster's memory model specifically, see the
[manual memory management evaluation](manual-memory-management-evaluation.md).
For the earlier mature-legacy comparison, see the
[PHP grounding sweep](php-grounding-sweep.md).

## Compared snapshots

Aster was inspected at local commit:

```text
5ef3d8af3c228201a585d2cc2568c7d22020049b
Remove legacy compiler and harden backend correctness
```

Go was cloned read-only from its canonical repository into
`/home/brandon/learning/comparison/go`:

```text
5d7583fe1f947d3746dc9b66da38b62ba9902759
cmd/internal/obj/loong64: add [X]VBITSELV and [X]VBITSELB instructions support
```

The checkout is the Go 1.28 development cycle, as recorded in
[goversion.go](../../comparison/go/src/internal/goversion/goversion.go).

The Go checkout contains approximately 15,700 files and 495,000 lines across Go,
assembly, C, and header sources. Compiler-internal source appears larger when
generated SSA opcode and rewrite files are counted: some generated files exceed
100,000 lines. Those files are products of checked-in generators and rule
declarations, not handwritten dispatch chains.

The installed bootstrap compiler on the review machine was Go 1.22.2, which is
too old to bootstrap the Go 1.28 development tree directly. The Go tree was
therefore inspected rather than rebuilt. Aster's previously recorded strict
ASan/UBSan run remains the runtime evidence for Aster: 341 of 342 tests passed,
with one existing undefined-behavior failure.

## Verdict

Go is a stronger compiler-engineering benchmark than PHP. It makes some of
Aster's weaknesses easier to see, but it also disproves several overly strict
conclusions from the PHP comparison.

Aster still does not look like wholesale AI slop. Its main pipeline—typed AST,
typed CFG IR, verification, C17 and bytecode backends—is coherent. Its cleanup
model, diagnostics, generated-C testing, and deliberate scope remain real
strengths.

Compared with Go, Aster remains unmistakably a compact prototype:

- subsystem boundaries are described more strongly than they are enforced;
- type syntax is flattened into strings and reconstructed;
- built-in semantics are copied across handwritten catalogs;
- pass and opcode metadata are not consistently generated;
- runtime representations rely on dispersed conventions;
- test execution is manually enumerated rather than a repository-wide native
  convention;
- the compiler and embedding lifecycle are not separated cleanly.

Go's source is not small or pristine. It contains substantial global compiler
state, two closely synchronized type checkers, multiple type and IR
representations, architecture-specific special cases, several distinct
built-in and intrinsic catalogs, compiler/runtime private contracts, unsafe
code, assembly, and enormous generated outputs. The difference is not absence
of duplication or coupling. Go usually makes those things deliberate, gives
them an owner, and surrounds fragile synchronization with generators,
duplicate checks, freshness tests, verifiers, or regression tests.

### Material corrections to the earlier assessment

The Go comparison changes four conclusions rather than merely repeating them:

1. **A backend retaining frontend access is not automatically bad
   architecture.** Go's SSA `Func` stores a `Frontend` interface and compiler
   `ir` and `types` objects. The correct requirement is explicit, narrow, and
   tested coupling—not total representational isolation. Aster's real defect is
   that its documentation promises no backend AST interpretation while the CSS
   backend recursively traverses raw AST bodies and other backend dependencies
   are undocumented borrowed links.
2. **One universal built-in registry is not the mature norm.** Go has separate
   handwritten language built-ins, generated pseudo-runtime declarations,
   handwritten architecture intrinsics, and generated SSA operation metadata.
   Aster needs one authoritative source *per semantic category*, stable typed
   identifiers, and mechanical cross-checks. It does not need one giant table
   containing every special operation in the language and runtime.
3. **Compiler globals are not evidence of AI slop by themselves.** Go uses
   package-global contexts, packages, symbol maps, diagnostics, and hooks. This
   is reasonable for a command-line compiler designed around one compilation
   process. Aster's globals are a concern specifically where they conflict with
   its public embedding and reentrancy contracts.
4. **Aster's IR is not a failed version of Go's machine SSA.** It is a semantic
   typed CFG with SSA-like instruction results and mutable local slots. That is
   a valid representation for C and bytecode emission; it does not need Go's
   phi-heavy optimizer IR, register allocator, or architecture lowering to be
   sound. Comparisons with Go's SSA verifier must account for this difference in
   layer and purpose.

| Dimension | Aster | Go |
| --- | --- | --- |
| Overall status | Serious experimental compiler | Production toolchain |
| Frontend representation | Handwritten AST with stringified type syntax | Structural syntax nodes and typed objects |
| Middle end | Small typed CFG IR and verifier | Typed IR, escape analysis, walk/order lowering, SSA and many verified passes |
| Backend | Generated C17 plus bytecode VM | Native architecture lowering, assembler and linker |
| Repetitive metadata | Often handwritten in multiple files | Frequently generated from declarative ops, rules, or declarations |
| Runtime | Deterministic cleanup, narrow refcounts, ordinary C allocation | Scheduler, stacks, allocator, concurrent GC, maps, channels and reflection |
| Tests | 342 CTest entries in reviewed build | Thousands of test files, distribution tests, race/MSan/ASan modes and 61 native fuzz functions |
| Implementation maturity | Prototype-grade | Battle-tested and continuously maintained |

## Compiler pipeline comparison

### Go's phase ownership is explicit

Go's compiler entry point in
[gc/main.go](../../comparison/go/src/cmd/compile/internal/gc/main.go) names the
major operations and delegates them to packages with narrow responsibilities:

```text
syntax parsing
  -> types2 checking and unified export data
  -> compiler IR construction
  -> devirtualization and inlining
  -> escape analysis
  -> ordering and architecture-independent lowering
  -> SSA construction
  -> SSA optimization and verification
  -> architecture lowering and register allocation
  -> object emission
  -> assembler/linker
```

The pipeline is not merely documented. Package boundaries correspond to real
source directories such as `syntax`, `types2`, `noder`, `ir`, `escape`, `walk`,
`ssa`, `ssagen`, architecture packages, `obj`, and `link`.

Aster's high-level pipeline is also good, but large files and direct
cross-references blur responsibilities. The C and bytecode backends still read
AST declarations and checked types even though the architecture says backends
consume only IR.

Go does **not** enforce total isolation here. Its SSA
[Func](../../comparison/go/src/cmd/compile/internal/ssa/func.go) contains a
`Frontend` callback interface, a compiler `*ir.Func`, `*ir.Name` values, and
compiler `*types.Type` values. The interface deliberately exposes services such
as runtime symbol lookup, write-barrier state, string symbols, diagnostics, and
the source compiler function. That is controlled coupling, not a closed IR in
the absolute sense.

The grounded lesson is therefore narrower: phase dependencies should be
declared through stable representations or narrow interfaces. Aster's borrowed
declaration links may be acceptable bootstrap machinery if documented and
constrained. The CSS backend's recursive walk over raw function AST bodies is a
stronger violation because it makes a backend rediscover semantic content that
the alleged backend-complete IR does not contain.

### Go has more than one IR, but each has a purpose

Go does not force every job into one universal representation:

- the syntax tree preserves parsed language structure;
- typed compiler IR represents language operations and declarations;
- unified IR is the serialized package/export format;
- SSA represents per-function value and memory flow;
- architecture lowering introduces machine-specific operations;
- object data is handed to the assembler and linker.

This is more complex than Aster needs. The lesson is not to add layers for
prestige. Each representation should have a stated contract. Aster's typed IR
can remain much smaller while either owning backend facts or requesting a small
set of explicitly documented services through a narrow interface.

## Structural types versus string protocols

Go's syntax tree has concrete nodes for array, slice, pointer, function,
interface, map, channel, index, and generic forms. Its semantic
[Type](../../comparison/go/src/cmd/compile/internal/types/type.go) stores an
explicit kind plus kind-specific data, cached layout, pointer data, methods,
underlying type, and ABI register requirements.

Aster's parser understands type grammar but serializes the result into strings
such as `Option<T>`, `fn(A)->B`, `*mut T`, and `[T;N]`. Its checker then parses
those strings again in [checker_types.c](../src/checker_types.c).

The Go comparison makes this Aster decision difficult to defend. It is not
necessary simplicity; it moves a parser into the checker and turns an internal
compiler boundary into a text protocol. Aster should introduce a small
`TypeSyntax` tree containing:

- named and qualified types;
- generic applications;
- pointers and mutability;
- fixed arrays;
- slices;
- function parameter and return types;
- exact source spans for every component.

Resolution should transform that syntax once into canonical semantic types.
Display names should be formatting output, never semantic input.

## IR and verification

### Aster's IR direction remains correct

Aster's IR is a real CFG with typed virtual values, blocks, terminators,
ownership operations, aggregate operations, exception edges, source spans, and
layout metadata. That is appropriate for both generated C and the VM. It is one
of the strongest reasons Aster is not an incoherent project.

It is not full machine-optimizer SSA. Instruction results are single-assignment,
but mutable local slots carry some state across control-flow joins instead of
requiring phi nodes for every source variable. That is a conventional and
defensible tradeoff for a semantic IR whose destinations are C and a compact
VM. Aster already computes reachability and dominators and checks that virtual
values dominate their uses; absence of Go-style phi machinery is not itself a
missing feature.

### Go's verifier is far more comprehensive

Go's SSA [checker](../../comparison/go/src/cmd/compile/internal/ssa/check.go)
validates, among other things:

- block uniqueness and function ownership;
- predecessor/successor cross-links;
- control count and control types for every block kind;
- opcode argument counts;
- exact auxiliary-data encodings;
- value ownership by blocks;
- use counts;
- dominance and phi placement;
- memory-value consistency;
- architecture and lowering invariants.

Aster's verifier validates many useful structural and typed properties,
including CFG reachability, dominators, and dominance of virtual-value uses.
Some ownership state is still protected by backend live flags and runtime
initialized flags. That can remain a reasonable implementation choice.
Nevertheless, the verifier should grow stronger around:

- whether backend-required type metadata is complete;
- whether every encoded slot, argument, mask, and ID fits its representation;
- whether ownership-affecting instructions are legal for the type;
- whether export, destructor, CSS, and parameter-mode facts are either IR-owned
  or obtained through a documented typed interface;
- whether any raw-AST traversal in a backend is intentional and covered by a
  declared phase contract.

### Pass order should be declared and checked

Go's [SSA pass table](../../comparison/go/src/cmd/compile/internal/ssa/compile.go)
names each pass, marks required passes, supports controlled debugging, and
records ordering constraints such as lowering before register allocation or
scheduling before late nil-check processing.

Aster does not need Go's long optimization pipeline because the C compiler is
its primary machine optimizer. It would still benefit from a small declarative
pipeline:

```text
construct -> verify -> canonicalize -> local optimize -> verify -> backend
```

Each pass should state what invariants it requires and preserves. This prevents
an optimization or backend preparation step from silently depending on the
accidental order of calls in a large driver function.

## Generated metadata versus duplicated knowledge

This remains a sharp difference between the repositories, but the first review
stated the remedy too broadly.

Go has huge generated code, especially architecture opcode tables and rewrite
functions. The handwritten source of truth lives in:

- architecture operation descriptions;
- generic operation descriptions;
- rewrite-rule files;
- generation programs under `ssa/_gen`;
- pseudo-runtime declarations used to generate compiler-known signatures.

The generator emits opcode enums, metadata tables, block kinds, allocators, and
rewrite implementations. Tests regenerate important outputs and fail if the
checked-in file is stale. For example,
[builtin_test.go](../../comparison/go/src/cmd/compile/internal/typecheck/builtin_test.go)
regenerates compiler runtime declarations and compares the bytes with the
checked-in result.

Aster currently repeats built-in names and behavior across the checker,
bytecode mapping, VM numeric dispatch, C lowering, and standard-library
declarations. Magic negative IDs connect those sites.

Go does not derive every special operation from one universal registry. Its
language built-ins are a small handwritten table in
[`universe.go`](../../comparison/go/src/cmd/compile/internal/typecheck/universe.go).
Pseudo-runtime declarations are generated and protected by a byte-for-byte
freshness test. Architecture intrinsics use a separate, large handwritten
registry with duplicate detection and behavioral tests. SSA operations and
rewrite rules use their own generators. These categories have different
owners because they describe different semantics.

Go proves that code generation is not itself slop, but it also proves that not
all handwritten catalogs need to be merged. Generated code is valuable when it
replaces mechanical repetition and has a readable canonical source. Aster
should define one authoritative registry per category and use those registries
to produce or validate, as applicable:

- names and stable IDs;
- arity and parameter modes;
- type signatures;
- ownership/copy behavior;
- may-throw and side-effect properties;
- VM dispatch declarations;
- C-backend lowering declarations;
- documentation and consistency tests.

## Runtime comparison

Go's runtime is vastly more complex because its promises are larger: goroutine
scheduling, segmented/growing stacks, asynchronous preemption, maps, channels,
interfaces, reflection, a concurrent garbage collector, finalization, race
instrumentation, cgo, signals, profilers, and many operating systems and
architectures.

Aster should not copy that runtime. Its simpler deterministic design is a
legitimate advantage:

- no tracing collector;
- explicit type-directed drops;
- conventional C resource handles;
- predictable scope cleanup;
- a portable-C deployment path;
- a bounded development VM.

What Aster should copy is Go's invariant discipline. Go centralizes allocator,
stack, scheduler, string, slice, map, interface, and ABI rules in named runtime
subsystems. Aster still has memory behavior spread across VM object switches,
built-in execution, C-runtime string templates, generated type helpers, and
native adapters. The sanitizer-detected null empty-view passed to `memcpy` is a
direct symptom of a representation rule that leaf functions must currently
remember themselves.

The correct response remains to harden Aster's manual memory model, not replace
it with Go's GC.

## Limits and allocation

Go has many practical limits, but they are generally named, checked, and tied to
the target or runtime configuration. Stack variables, object sizes, ABI
registers, branch ranges, symbol formats, and architecture encodings have
specific diagnostics or internal checks.

Aster's limits are reasonable for a bounded language and compact VM, but they
are more ad hoc:

- fixed 32-entry compiler context arrays;
- 32-bit parameter masks;
- 128 frames;
- 1,024 operands per frame;
- compact bytecode fields;
- one maximum-local stride reserved for every synchronous frame.

The likely undefined 32-bit shift in [vm.c](../src/vm.c) is more concerning than
the existence of the limit itself. Every limit should be named once and checked
at syntax/checking, IR verification, bytecode construction, and runtime decode
boundaries as appropriate.

Go's per-function SSA and stack layout also reinforces the earlier conclusion
that Aster should calculate per-function VM frame requirements rather than
letting one unusually large function inflate all 128 frames.

## Testing and repository discipline

The Go checkout contains:

- about 1,896 `_test.go` files;
- about 3,500 files under the top-level language/toolchain test tree;
- 61 native `Fuzz...` entry points;
- compiler, SSA, runtime, architecture, assembler, linker, and standard-library
  tests;
- a `dist test` runner with race, MSan, and ASan builder modes;
- generated-file freshness tests;
- benchmarks and architecture-specific test data.

Go's CI does not primarily live in GitHub Actions; the source itself references
the Go builder and trybot infrastructure. This is a useful warning against
judging process solely by the presence of `.github/workflows`.

Aster's 342 tests are genuinely strong for its size, especially the dual-backend
and cleanup coverage. The gap is institutionalization:

- CMake registers many cases manually;
- there is no visible repository fuzzer;
- sanitizer availability is not the same as continuous sanitizer enforcement;
- generated or duplicated catalogs lack freshness tests;
- malformed bytecode and hostile size inputs need deeper coverage.

The next step is data-driven test discovery plus fuzz targets for the lexer,
parser, checker, IR verifier, bytecode decoder, and VM.

## Go is not a perfect codebase

The comparison should not turn into Go worship.

### Duplicate type-checker implementations

The compiler's own
[`types2` README](../../comparison/go/src/cmd/compile/internal/types2/README.md)
says plainly that there are "two almost identical typecheckers": internal
`types2` over the compiler syntax tree and public `go/types` over `go/ast`.
Changes often apply to both. This is real maintenance duplication, but many
files can be generated from `types2`, and the implementations share annotated
test data. The lesson for Aster is not that duplication is forbidden. It is
that unavoidable duplication needs an explicit synchronization mechanism.

### Multiple type representations

Go has syntax types, `types2` types, compiler backend types, reflection types,
ABI metadata, and serialized unified IR types. Conversions between them are
complex and have historical baggage.

### Global compiler state

The compiler uses package globals for its link context, current packages,
target, symbol maps, diagnostics, and hooks between packages. It is designed as
a command-line toolchain process, not a clean reentrant compiler library.

This means global state is not an AI-slop signal. The criticism of Aster is
contract-specific: Aster exposes an embedding API and should hold
configuration that may differ between embedded instances in explicit contexts.

### Private compiler/runtime coupling

The compiler knows many runtime entry points and representation details. Go
manages that coupling with pseudo-runtime declarations, generators, ABI
packages, link directives, and tests, but it remains sophisticated and fragile
systems code.

### Generated size is enormous

Generated SSA rewrite files are difficult to review directly. Their quality
depends on the generators, declarative rules, deterministic output, and
freshness tests. Generation relocates complexity; it does not make it disappear.

These weaknesses make the comparison more credible. Go is not clean because it
has no debt. It is clean where repeated complexity has explicit ownership and
mechanical enforcement.

## What Aster gets right relative to Go

Aster should retain several differences:

- deterministic cleanup rather than a tracing collector;
- a small runtime suitable for C interoperation;
- portable C as the primary machine-code strategy;
- no custom assembler or linker requirement;
- a bytecode VM for fast development and differential validation;
- explicit allocation and copy costs;
- a much narrower feature and platform target;
- no attempt to implement goroutines, reflection, channels, and a universal
  managed heap before applications require them.

Aster's typed ownership operations are also more explicit than Go's source
semantics require. That is appropriate for Aster's thesis. Go should be used to
improve the implementation discipline around those operations, not erase them.

## Priority order after the Go comparison

### 1. Replace type strings with structural syntax

This is now the clearest frontend priority. Preserve parsed structure and spans;
resolve once; use formatting only for diagnostics.

### 2. Create authoritative metadata sources by category

Separate language built-ins, runtime calls, VM opcodes, and backend-only
intrinsics when their semantics differ. Within each category, generate or
mechanically validate every consumer. Replace handwritten negative-ID
synchronization with stable typed identifiers and cross-table tests.

### 3. Make the IR boundary truthful and explicit

Eliminate backend rediscovery of semantics by recursively walking raw AST
bodies. IR-owned type metadata or a narrow typed frontend interface may remain
where useful, but document it as part of the actual architecture and verify the
required facts. Do not claim a completely closed IR while retaining hidden
borrowed dependencies.

### 4. Strengthen verification and limit checks

Validate every encoding, parameter mask, slot index, ownership operation,
destructor identity, CFG edge, and backend-required field.

### 5. Declare the compilation pass pipeline

Make required invariants and ordering explicit, even if Aster retains only a
small number of passes.

### 6. Harden runtime invariants

Fix the current sanitizer failure, settle empty-data representation, centralize
checked allocation, align OOM behavior with public contracts, and retain the
manual-memory architecture.

### 7. Replace blanket VM frame arenas

Compute per-function frame requirements and keep bounded execution through
validated limits rather than universal maximum strides.

### 8. Introduce explicit embedding contexts

Move standard-library paths, target state, module loading, caches, and
diagnostics out of process-global configuration.

### 9. Institutionalize generation and hostile testing

Add freshness tests, fuzz targets, continuous sanitizer configurations, and
data-driven test discovery.

### 10. Optimize lookup only after boundaries stabilize

Then replace repeated linear symbol/type scans with deterministic hash-based
tables and canonical interning.

## Final assessment

The Go comparison is more useful than the PHP comparison because it both
confirms Aster's clearest defects and prevents an unrealistically pure standard
from being applied to a compiler. Aster's central design is legitimate. Its
integration debt is real, but narrower than the first version of this report
claimed.

The most AI-shaped Aster code is not the typed IR, deterministic cleanup,
portable-C strategy, presence of globals, or mere existence of frontend links.
It is glue that became semantic infrastructure without enforcement: formatted
type strings, magic built-in IDs shared by convention, unchecked encoded
limits, contradictory runtime view invariants, and undocumented raw-AST
backend behavior.

Go's main lesson is simple:

> Repetition, global state, frontend callbacks, and handwritten tables are not
> inherently slop. They become trustworthy when their scope is deliberate,
> their contracts are explicit, and synchronization is mechanically checked.

Aster should remain smaller than Go and should not imitate Go's GC or runtime
ambition. If it adopts Go's discipline around representation, generation,
verification, and testing, its existing architecture can mature without losing
the deterministic, C-oriented identity that makes it distinctive.
