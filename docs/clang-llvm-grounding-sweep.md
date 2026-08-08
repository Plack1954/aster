# Aster and Clang/LLVM implementation grounding sweep

> Historical note: this report predates Aster's move-by-default and explicit
> `copy(value)` decision. Its copy-by-default recommendations are superseded by
> [`ownership-and-copy.md`](ownership-and-copy.md).

## Purpose and scope

This review compares Aster with the current development source of Clang and
LLVM as an implementation of the C and C++ language family and as a mature
compiler infrastructure. It is not an argument that Aster should become C++,
target LLVM, acquire C++'s full object model, or copy LLVM's scale.

The useful questions are narrower:

- Is Aster's handwritten compiler architecture conventional?
- Is its deliberate manual memory management implemented sanely?
- Are its type, cleanup, IR, and backend boundaries credible?
- Which apparent shortcuts are ordinary prototype choices?
- Which shortcuts are genuinely fragile or mechanically bad?
- What would a conventional C or C++ implementation do instead?

This report deliberately does not recommend Rust-style ownership syntax,
lifetime parameters, borrow checking, move-only-by-default values, or a tracing
garbage collector. Aster's ordinary value copying, explicit `ref`/`out`
parameters, deterministic destruction, shared native handles, and unsafe raw
pointers are evaluated against C and C++ practice.

For the focused memory-model review, see the
[manual memory management evaluation](manual-memory-management-evaluation.md).
The earlier comparisons are the [PHP grounding sweep](php-grounding-sweep.md)
and [Go grounding sweep](go-grounding-sweep.md).

## Compared snapshots and method

Aster was inspected at local commit:

```text
5ef3d8af3c228201a585d2cc2568c7d22020049b
Remove legacy compiler and harden backend correctness
```

Clang/LLVM was shallow-cloned read-only from the canonical LLVM monorepo into
`/home/brandon/learning/comparison/llvm-project` at:

```text
3d6ac5a9ba252cbaf8d12cecef89d015f1ffe0f7
[mlir][func] Fix a crash in DuplicateFunctionEliminationPass (#209667)
```

The checkout is from 5 August 2026 and identifies itself as the LLVM 24
development series in
[`LLVMVersion.cmake`](../../comparison/llvm-project/cmake/Modules/LLVMVersion.cmake).
It occupies approximately 2.9 GiB and contains about 181,000 checked-out files
across all monorepo projects. The `clang` and `llvm` trees alone contain about
115,000 files.

This was a source architecture review. Building the complete current LLVM
monorepo would be disproportionate to the question and would not add useful
evidence about Aster. The previously recorded Aster sanitizer run remains the
dynamic evidence: 341 of 342 tests passed, with one existing UBSan failure in a
generated-C system I/O test. No Aster implementation source was changed during
this review.

## Blunt verdict

Clang/LLVM makes Aster look **more conventional architecturally** than the PHP
comparison did, while exposing two new concrete frontend problems.

Aster's central choices are not AI slop:

- A handwritten lexer and recursive-descent parser are conventional. Clang
  uses them too.
- A module-lifetime bump arena for syntax and semantic objects is conventional.
  Clang's `ASTContext` does essentially the same thing at a much larger scale.
- Explicit cleanup scheduling is conventional. Clang's code generator has a
  large cleanup-scope stack for normal and exceptional exits.
- Retaining selected typed frontend declarations during code generation is
  conventional. Clang generates LLVM IR directly from its AST and semantic
  declarations.
- Large implementation files and handwritten switches are not automatically
  slop. Clang and LLVM contain many enormous, heavily reviewed examples.
- Aster's semantic CFG does not need to become LLVM's optimizer SSA to be a
  legitimate IR.

The highest-confidence Aster defects after this comparison are:

1. Parser and checker arrays repeatedly grow by exactly one element inside a
   non-freeing arena, causing quadratic copying and quadratic retained memory.
2. The generic-application string splitter tracks angle brackets but not
   parentheses, so a function type containing multiple parameters can be split
   as multiple generic arguments.
3. Parsed type structure is still destroyed into strings and reconstructed;
   Clang shows the mature structural alternative particularly clearly.
4. Type objects are only partially canonicalized, leaving repeated allocation,
   recursive structural comparison, and linear IR interning on common paths.
5. Internal invariants have very little local executable enforcement. Aster's
   source has one compile-time assertion and essentially no ordinary C
   assertions, while relying on many implicit switch and pointer assumptions.
6. Existing previously identified defects remain: the sanitizer-detected empty
   view operation, magic built-in IDs, unchecked encoded limits, and the public
   recoverable-allocation contract contradicted by process termination.

The correct overall classification remains **serious prototype, not
production compiler**. The new evidence narrows the slop judgment. Aster's
compiler thesis is conventional; some containers and string protocols inside
that thesis are not.

## What Clang changes about the earlier judgment

### Handwritten parsing is not a weakness

PHP's generated scanner and parser made Aster's handwritten frontend look less
formal by comparison. Clang reverses that impression.

Clang's [Lexer](../../comparison/llvm-project/clang/include/clang/Lex/Lexer.h)
and [Parser](../../comparison/llvm-project/clang/include/clang/Parse/Parser.h)
are handwritten. The parser is a large recursive-descent implementation with
explicit lookahead, recovery, scopes, delimiter tracking, diagnostics, and
semantic callbacks. It is tightly connected to `Sema`; parsing and semantic
disambiguation cooperate because C++ requires that complexity.

Aster's handwritten lexer and parser are therefore completely defensible. Its
language is much smaller, its parser is readable for its scope, and keeping a
separate checker is arguably cleaner than Clang's unavoidable C++ coupling.

The problem is not that Aster did not use a parser generator. The problem is
what it does with structured type syntax after successfully parsing it.

### Backend access to frontend objects is conventional

Clang's code-generation entry in
[`ModuleBuilder.cpp`](../../comparison/llvm-project/clang/lib/CodeGen/ModuleBuilder.cpp)
stores an `ASTContext *`, receives `Decl` objects, and calls
`EmitTopLevelDecl`. The code generator traverses functions, variables,
expressions, types, attributes, destructors, and semantic declarations to emit
LLVM IR. There is no doctrine that a mature backend must never see a frontend
object.

This further corrects the overly pure version of the earlier Aster criticism.
An `IrFunction` retaining a borrowed typed declaration is not inherently bad.
Using checked type information to emit a calling convention is also not
inherently bad. Both can be reasonable if lifetime, allowed operations, and
phase ownership are explicit.

Aster's remaining issue is the mismatch between documentation and reality.
[The architecture](architecture.md) says that backends consume IR and never
inspect or reinterpret the raw AST. Yet
[`c_backend_css.c`](../src/c_backend_css.c) recursively scans raw function AST
bodies for static CSS. Either static CSS collection is a frontend/lowering pass
whose result should be recorded before code generation, or the architecture
must explicitly define it as an AST-consuming side pass. The current code is
not necessarily wrong, but the stated boundary is false.

### Large files are not evidence by themselves

Aster has files such as `c_backend.c`, `vm.c`, `checker_calls.c`, and `ir.c`
that range from roughly 2,000 to 4,900 lines. That deserves navigation and
ownership scrutiny, but Clang/LLVM makes a simple size-based criticism
untenable. Current handwritten files include semantic-analysis and target
lowering implementations tens of thousands of lines long.

The relevant questions are instead:

- Does the file have a coherent owner and purpose?
- Are repeated cases generated or mechanically cross-checked where useful?
- Are invariants asserted and tested?
- Can a change be localized without synchronizing unrelated magic values?

Aster sometimes fails those tests, but not merely because a file is large.

## New concrete defect: quadratic arena-retained array growth

Aster's compiler arena is a conventional bump allocator. In
[`common.c`](../src/common.c), it allocates 4 KiB blocks, aligns allocations,
zero-initializes them, and frees the complete chain when the module dies. This
is broadly the same lifetime strategy as Clang's `ASTContext`.

The allocator is not the defect. The parser's container helper is:

```text
allocate space for count + 1 elements from the arena
copy all count existing elements into it
abandon the old array in the arena
append one element
```

[`parser_grow_array`](../src/parser.c) is used for declarations, imports,
statements, parameters, fields, generic parameters, call arguments, array
literals, match arms, interpolation parts, element properties, and element
children. There are 23 direct call sites. Similar grow-by-one copies also occur
in generic instantiation and type-resolution code.

Because an arena cannot free the abandoned arrays, a list of `N` elements
retains arrays of sizes 1, 2, 3, through `N`. Both allocated elements and copied
elements are proportional to `N(N+1)/2`.

For a block containing 10,000 statement pointers, the arrays alone retain
about 400,040,000 bytes before allocator overhead and alignment—approximately
381 MiB. Building that list also copies about 50 million pointers. A source
file does not need to be malicious for large generated declarations or markup
children to make this visible.

Clang combines its bump allocator with capacity-aware temporary containers
such as
[`SmallVector`](../../comparison/llvm-project/llvm/include/llvm/ADT/SmallVector.h),
then places final-sized arrays or trailing storage into the AST arena. Growth
is geometric and temporary storage can be released. A conventional C
implementation would do the same with a small `{items, count, capacity}`
builder allocated through `realloc`, followed by one final arena allocation if
stable arena ownership is desired.

This is high-confidence AI-shaped implementation debt: a locally simple helper
that looks correct on small tests but combines disastrously with the allocator
chosen around it.

## New likely correctness defect: generic splitting ignores parentheses

Aster's string-based type representation creates a more immediate correctness
risk than previously documented.

[`split_generic_application`](../src/checker_types.c) divides
`Name<A,B,...>` into argument strings. It increases nesting depth for `<`,
decreases it for `>`, and treats a comma at angle depth zero as an argument
boundary. It does not track parentheses or square brackets.

Aster's normalized function types use syntax such as:

```text
fn(i32,i32)->i32
```

Therefore a user-defined type application conceptually shaped as:

```text
Box<fn(i32,i32)->i32>
```

reaches the generic splitter with the comma inside the function parameter list
at angle depth zero. Static inspection indicates that it will be interpreted as
two generic arguments instead of one. The repository tests cover delegates and
ordinary generic containers, but no test was found combining a multi-parameter
function type with a user-defined generic application.

This review did not add a reproducer because the exercise prohibits coding, so
this is classified as a **high-confidence latent defect**, not a dynamically
demonstrated failure.

The defect is a direct consequence of converting a type tree into text and
then writing multiple partial parsers for that text. The function-type resolver
elsewhere already tracks parentheses, angle brackets, and brackets separately;
the generic splitter implements a different nesting model.

## Type representation: Clang confirms the strongest earlier criticism

Clang separates several concepts that Aster currently compresses into strings:

- syntax and declarator structure as written;
- semantic `Type` nodes;
- `QualType`, which associates qualifiers with a type;
- `TypeLoc`, which preserves source-level type location information;
- canonical types used for identity and fast comparison;
- target layout computed through `ASTContext` and target information.

`ASTContext` owns folding sets for pointer, array, function, template,
reference, vector, and many other type forms. For example,
[`getPointerType`](../../comparison/llvm-project/clang/lib/AST/ASTContext.cpp)
profiles the pointee type, reuses an equivalent existing node, and records the
canonical form. Canonical type equality can then become pointer equality.

Aster does eventually have structured semantic `Type` objects, and applied
named types receive some interning. However, the parser first formats type
syntax into strings. The checker then allocates fresh objects for many
function, pointer, slice, list, dictionary, option, result, task, and array
resolutions. `same_type` recursively compares structure, while IR interning
linearly scans existing IR types and recursively compares candidate identities.

This is not yet a demonstrated performance crisis at Aster's scale. It is a
clear scalability and maintainability debt:

- parsing and resolution allocate intermediate strings and duplicate types;
- diagnostics lose precise spans for nested type components;
- structural equality is repeated instead of canonicalized once;
- partial string parsers can disagree, as the generic-function case shows;
- type-system evolution requires modifying several text protocols.

The appropriate repair remains a small structural `TypeSyntax` tree followed
by canonical semantic type construction. This is normal compiler engineering,
not Rust-inspired language design, and it does not change Aster's user-visible
memory semantics.

## Manual memory management comparison

### The compiler arena is sane

Clang's
[`ASTContext`](../../comparison/llvm-project/clang/include/clang/AST/ASTContext.h)
states that AST objects are never individually destructed. They are allocated
from an `llvm::BumpPtrAllocator` and released when the complete context is
destroyed. Its `Deallocate` operation is effectively empty. Non-trivially
destructible side objects can register destruction callbacks.

Aster's `LangArena` follows the same central idea using ordinary C. AST nodes,
declarations, checker types, generic clones, CSS nodes, and strings share a
module lifetime and are freed together. For trivially destructible C records,
not running individual destructors is exactly appropriate.

This is deliberate manual memory management done normally. It should be kept.
The grow-by-one arrays should be fixed without abandoning the arena.

### Aster's source-level cleanup model is also conventional

Clang's code generator uses `EHScopeStack`, `RunCleanupsScope`, cleanup kinds,
and explicit destroy operations to ensure C++ objects are cleaned on ordinary
scope exit, returns, branches, and exception unwinding. The machinery is much
more complex because C++ promises far more.

Aster's typed IR explicitly represents local moves, loads, drops, value clones,
and cleanup-relevant operations. The C backend tracks initialized/live state,
and the VM tracks ownership in its values and objects. That is directionally
similar to the conventional work a C++ compiler must perform. Aster's smaller
model is a strength, not a primitive mistake.

### Destructor-bearing copies need a C++ rule, not a Rust rule

C++ does not make every type with a destructor move-only. Ordinary classes
remain copyable when their copy operations are available. Memberwise copy is
safe when members have sound copy semantics. Resource wrappers conventionally
provide a copy constructor, use shared ownership, or delete copying.

Clang even has warnings such as `-Wdeprecated-copy-dtor` for implicit copy
operations in the presence of user-declared or user-provided destructors. The
language permits the operation in many cases; the compiler helps identify a
likely Rule-of-Three/Rule-of-Five mistake.

Aster should stay within that model:

- ordinary values copy normally;
- containers and owning value fields recursively copy;
- `NativeHandle` supplies shared-resource semantics;
- raw pointers remain explicitly unsafe and non-owning by default;
- a user-defined copy operation can express custom duplication;
- copying may be disabled for a true unique owner, as with a deleted C++ copy
  constructor;
- a warning can flag a custom destructor plus raw-pointer state without
  imposing borrow checking or move-only-by-default semantics.

That is conventional C++ resource design. Nothing in the Clang comparison
justifies adding Rust lifetime syntax or ownership ceremony to Aster.

## IR and verification

LLVM IR is a target-independent optimizer IR in strict SSA form. Its
[`Verifier.cpp`](../../comparison/llvm-project/llvm/lib/IR/Verifier.cpp) checks
function signatures, instruction types, dominance, phi nodes, control-flow
structure, exception regions, attributes, intrinsics, metadata, calling
conventions, target extensions, debug information, and many other contracts.
The current file is more than 8,000 lines because LLVM IR is a public compiler
interface used by many producers and transformations.

Aster's IR serves a different layer. It is a typed semantic CFG with
single-assignment virtual instruction results plus mutable local slots. It
preserves ownership operations needed by its C and bytecode backends. It is not
trying to perform register allocation or machine optimization.

The comparison therefore supports two conclusions simultaneously:

1. Aster does not need LLVM-style phi construction, hundreds of passes,
   target-machine opcodes, or an LLVM-sized verifier.
2. Once Aster declares an IR invariant, it should enforce that invariant as
   aggressively as its scale permits.

Aster's verifier is better than a superficial comparison suggests. It checks
opcode validity, result and operand types, local indices, calls, aggregates,
ownership-related operations, CFG structure, reachability, dominators, and
dominance of virtual-value uses. That is substantial.

The maturity gap appears in the layers around it. Across Aster's C source,
there is one `_Static_assert` and effectively no ordinary `assert` calls.
Clang/LLVM uses debug assertions extensively for local preconditions in
addition to full verifiers and recoverable input validation. Raw assertion
counts are not a quality score, but Aster's near-total absence is notable for a
compiler with many tagged unions, parallel arrays, magic IDs, fixed masks, and
phase-dependent pointers.

Aster should add conventional C assertions for internal-only facts while
retaining diagnostics or verifier failures for malformed user input. Examples
include parallel-array counts, non-null fields required by an opcode, phase
state, index ranges already proven by a caller, and enum/table synchronization.
Release safety must not depend solely on assertions, but debug builds should
make invariant violations fail close to their source.

## Generated metadata and handwritten special cases

Clang/LLVM makes heavy use of TableGen and `.def` files for diagnostics,
attributes, AST node families, built-ins, intrinsics, target instructions,
registers, calling conventions, passes, and rewrite patterns. The checkout has
hundreds of `.td` files across Clang and LLVM.

It also contains many handwritten registries, target switches, visitors, and
special cases. Mature compiler code does not require everything to come from
one generator. The useful discipline is one authoritative owner per semantic
category, stable generated identifiers where cross-file synchronization is
risky, and tests that catch stale outputs or missing cases.

For Aster, that means:

- IR opcodes may have their own authoritative table;
- language built-ins may have another;
- runtime native calls may have another;
- C-only lowering helpers need not be forced into a VM registry;
- every magic numeric bridge should nevertheless be generated or validated.

The current negative built-in IDs that connect checker recognition, bytecode
lowering, and VM dispatch remain poor because their agreement is largely
conventional rather than mechanically proven.

## Diagnostics and error contracts

Clang diagnostics are declared by stable IDs in subsystem-specific TableGen
files. They carry severity, warning groups, categories, typed substitutions,
source ranges, fix-its, and notes. This machinery is far beyond what Aster
needs today, but it illustrates the benefit of diagnostics as data rather than
free-form strings alone.

Aster's diagnostics are good for a prototype: they have severity, a primary
span, up to four secondary spans, notes, and help text. The implementation takes
source presentation seriously.

The limitations are fixed and silent. Messages, notes, and help are stored in
256-byte arrays; secondary labels use 128 bytes; fifth and later notes or
secondary spans are ignored. Long generic and qualified type names can already
approach these limits. Before tools depend on diagnostics, Aster should either
use owned variable-length text or expose explicit truncation and stable
diagnostic kinds.

LLVM also distinguishes recoverable errors (`Error`, `Expected<T>`) from fatal
internal failures. Aster does not need those C++ classes, but its C API needs the
same contract discipline. `lang_vm_new()` is documented to return `NULL` for a
recoverable allocation failure, while its allocator normally prints an error
and exits the process. That remains an embedding defect, not a philosophical
argument that compilers may never terminate on internal failure.

## Compiler and embedding contexts

Clang's `CompilerInstance` owns or references explicit diagnostics, file and
source managers, preprocessor, AST context, semantic analyzer, AST consumer,
module state, and output files. `ASTContext` and `LLVMContext` provide explicit
lifetime and uniquing boundaries. Clang and LLVM still have registries and some
global state, but independent compilation state has an identifiable owner.

Aster already has useful context objects: `Module`, `Checker`, `IrModule`, and
`LangVM`. The process-global standard-library and executable-path configuration
is therefore an unnecessary exception. It is acceptable for the CLI, but
surprising for an embedding API that may host independent compiler instances.
Moving per-instance configuration into a compiler/project context remains a
conventional C/C++ recommendation.

## Testing and hostile validation

The scale difference is enormous. The current `clang/test` and `llvm/test`
trees contain roughly 99,500 files, with about 1,350 files under the two unit
test trees. LLVM's `lit` runner discovers tests and executes them in parallel;
`FileCheck` makes exact IR and diagnostic expectations local to source inputs.
The verifier alone has more than 400 dedicated test files. The monorepo also
contains many fuzzing tools for Clang parsing, formatting, LLVM assembly,
bitcode, instruction selection, optimizers, disassemblers, debug formats, YAML,
and demanglers. Build options include assertions, expensive checks, and several
sanitizers.

Aster's 342 configured tests remain excellent for a 47,000-line experimental C
implementation. Dual execution through the bytecode VM and generated C is a
particularly valuable differential oracle that Clang does not directly offer
in the same form.

The gaps are now more specific than “needs more tests”:

- no discovered test covers large parser lists and compiler peak memory;
- no discovered test covers a multi-parameter function type nested in a
  user-defined generic application;
- no visible fuzzer targets lexer/parser/checker/IR/bytecode boundaries;
- test registration is manually centralized in CMake;
- the documented sanitizer release gate currently fails;
- there is little assertion-enabled internal invariant testing.

These should be addressed before simply increasing example count.

## Grounded priority order after Clang/LLVM

### 1. Replace grow-by-one arena arrays

Use geometric temporary builders and finalize once into arena storage where
stable lifetime is required. Audit the parser, type resolver, generic
instantiator, and any other code that copies an arena array merely to append one
element. Add allocation-count and peak-memory stress tests.

### 2. Replace type strings with structural syntax

Preserve nested syntax and spans through parsing. Resolve it once into
canonical semantic types. This fixes an architectural weakness and removes the
class of delimiter disagreement represented by the generic/function defect.

### 3. Fix and test nested generic/function parsing immediately

Even before the structural type conversion is complete, every string splitter
must consistently track angle, parenthesis, and bracket nesting. Add negative
and positive coverage for function types, arrays, qualified names, and nested
generic applications.

### 4. Canonicalize semantic types

Intern pointer, function, container, option/result, array, and applied named
types within an explicit type context. Let equality use canonical identity on
the common path. Replace linear IR type lookup after correctness boundaries are
stable.

### 5. Harden existing runtime and encoding invariants

Fix the demonstrated empty-view sanitizer failure. Validate 32-bit masks,
shifts, slot counts, frame requirements, allocation arithmetic, and every
serialized index. Align public allocation behavior with the embedding API.

### 6. Add local executable invariants

Use `_Static_assert` for table sizes and representation facts, and ordinary C
assertions for internal phase and data-structure preconditions. Keep user input
errors recoverable and checked in release builds.

### 7. Give metadata mechanical owners

Use separate authoritative sources for IR opcodes, built-ins, runtime calls,
and backend-only special operations. Generate or validate numeric mappings and
add freshness tests.

### 8. Make the documented backend boundary truthful

Typed declaration and type references may remain if their role is explicit.
Move semantic rediscovery such as static CSS extraction into a declared pass,
or document the AST-consuming side pass honestly.

### 9. Improve diagnostic and embedding contracts

Remove silent diagnostic truncation where practical, add stable diagnostic
kinds when tooling needs them, and move instance-varying configuration out of
process globals.

### 10. Institutionalize hostile testing

Add source-boundary and bytecode fuzzers, assertion-enabled CI, continuous
ASan/UBSan runs, data-driven test discovery, and compiler-memory stress tests.

## Final assessment

Clang/LLVM is the most favorable comparison to Aster's architectural thesis so
far. Aster resembles a conventional small C/C++ compiler in the places that
matter:

- handwritten recursive-descent frontend;
- arena-owned compiler objects;
- structured semantic types after checking;
- explicit cleanup lowering;
- typed CFG and verification;
- direct use of typed declarations during some backend work;
- ordinary C resource ownership and deterministic destruction.

It also supplies the clearest evidence of genuinely poor glue. A bump arena is
good; repeatedly copying every prefix of a list into that arena is not. Parsing
types structurally is good; rendering them into strings and maintaining
inconsistent delimiter parsers is not. A verifier is good; leaving almost all
local invariants implicit is not.

The manual memory management verdict remains positive. Aster should keep its
deterministic ownership model, ordinary value copies, deep-copying containers,
shared `NativeHandle`, unsafe raw pointers, explicit arenas, and C-oriented
runtime. Conventional copy constructors or deleted copy operations are enough
for exceptional resource-owning user types. No Rust-like type system is
required.

The revised slop verdict is therefore precise:

> Aster is not architecturally AI slop. It contains several AI-shaped local
> shortcuts whose costs were hidden by small tests, especially grow-by-one
> arena containers and strings used as nested type structures.

Those problems are repairable without changing the language's identity. The
correct direction is more disciplined C compiler engineering, not a different
ownership philosophy.
