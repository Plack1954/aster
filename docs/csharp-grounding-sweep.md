# Aster versus C#: compiler, runtime, and manual-memory grounding sweep

Date: 2026-08-05

## Scope and method

This is a read-only engineering comparison. No Aster implementation source was
changed and no replacement design was coded.

The comparison uses both major halves of the current open-source C# stack:

- Roslyn, the C# compiler, cloned at
  `/home/brandon/learning/comparison/roslyn`, commit
  `af17bf8b7435b984115f943bee4cce0dc95df42c`;
- the .NET runtime and base libraries, cloned at
  `/home/brandon/learning/comparison/dotnet-runtime`, commit
  `8668a6212a32bde6c14e9c7ffbff27c21881144f`.

Both commits were current `main` snapshots on the date above. Aster was
inspected at commit `5ef3d8af3c228201a585d2cc2568c7d22020049b`.

C# is not being used as an argument that Aster should adopt garbage collection,
finalizers, a borrow checker, Rust-style lifetime syntax, or Rust-style
move-only-by-default semantics. That would be a category error and would
contradict Aster's intended character. Roslyn is useful as a mature compiler
control. CoreCLR is useful both for its managed resource conventions and for
the substantial manually managed C++ runtime underneath them.

The questions are narrower:

1. Are Aster's manual-memory choices recognizable C, C++, or C# engineering?
2. Where does C# expose a real semantic or structural weakness in Aster?
3. Which differences are merely differences of scale or language goals?
4. Which parts look like AI-generated shortcut accumulation rather than a
   deliberate language design?

## Verdict

Aster's deliberate manual memory management is not stupid. The basic model is
conventional and coherent:

- deterministic destruction;
- explicit cleanup in IR;
- deep copies for independent owning containers;
- narrow reference counting for identity-bearing or shared values;
- a shared `NativeHandle` abstraction for external resources;
- explicit `ref` and raw-pointer escape hatches;
- an arena for compiler-lifetime allocation;
- no tracing garbage collector and no source-level lifetime calculus.

CoreCLR itself contains extensive conventional manual resource management.
Its C++ code uses scoped holder types such as `NewHolder`, `NewArrayHolder`,
lock holders, release holders, and state holders. The public managed side uses
`IDisposable`, `using`, and `SafeHandle` to put deterministic release around
resources that the GC cannot manage. Manual cleanup is plainly not an
amateurish choice merely because C# programs usually allocate ordinary objects
on a garbage-collected heap.

The C# comparison does, however, uncover one important semantic defect that the
PHP, Go, and LLVM sweeps did not isolate as clearly:

> Aster's current `ref` implementation is copy-in/copy-out, not true reference
> aliasing.

Both the bytecode VM and generated-C backend copy a referenced caller value
into a callee local and write the local back when the function exits. If two
parameters refer to the same caller place, the two callee locals do not alias
one another. This can change intermediate observations and the final value.
That conflicts with the ordinary meaning of C++ references and C# `ref`
parameters.

The comparison also strengthens three existing findings:

- compressing per-parameter modes into 32-bit masks creates an arbitrary limit
  and a likely undefined shift in the VM;
- Aster's `out` syntax has not yet acquired C#'s definite-assignment semantics;
- Aster's generated async frames preserve every parameter, local, and virtual
  value rather than only state live across suspension.

The latter is a prototype-quality space decision, not evidence that manual
memory management is wrong.

My overall assessment after the C# sweep is:

- **Language thesis:** coherent, recognizably conventional, worth preserving.
- **Manual-memory thesis:** sound for the language Aster wants to be.
- **Compiler architecture:** serious prototype with a credible middle end.
- **Runtime correctness:** not production-ready; some low-level and reference
  semantics are unsettled.
- **AI slop:** real but localized in compressed metadata, string-based
  structure, duplicated boundary knowledge, and optimistic shortcuts.
- **Need for Rust concepts:** none.

## Comparison at a glance

| Area | Aster | C# / .NET |
|---|---|---|
| Source syntax | Handwritten lexer and parser | Handwritten parser over generated structural syntax node families |
| Type syntax | Parsed into normalized strings, then interpreted again by the checker | Structural `TypeSyntax` nodes for arrays, pointers, nullable types, tuples, function pointers, and generic arguments |
| Checked representation | Typed CFG-style IR with explicit copy, clone, move, drop, and cleanup operations | Generated bound-node family followed by many semantic lowerings and IL emission |
| Parameter modes | Booleans plus several 32-bit argument masks | A `RefKind` value stored on every `ParameterSymbol` |
| `ref` behavior | Currently copy-in/copy-out in both backends | Alias to the caller's storage |
| `out` behavior | Distinct call syntax, but no all-path assignment proof yet | Definite-assignment analysis forbids reading before assignment and requires assignment before normal exit |
| Async lowering | C frame containing every parameter, local, and IR virtual; VM frame contains all locals plus a fixed stack | State-machine rewriting with release-mode liveness analysis for hoisted variables |
| Ordinary memory | Deterministic copy/drop, deep-copy containers, narrow RC, arenas | Tracing GC for managed objects; deterministic `Dispose` for external resources |
| Native resources | Reference-counted `NativeHandle` with registered C destructor | `SafeHandle`, `IDisposable`, and extensive native C++ RAII holders |
| Compiler context | Module/compiler data plus some process-global configuration | Explicit, largely immutable `CSharpCompilation` values with options, trees, and references |
| Diagnostics | Fixed-size message and attachment storage | Symbolic error codes, resource-backed messages, diagnostic objects, locations, and large test matrices |
| Validation scale | Strong for a small project, but no comparable CI/fuzz estate | Thousands of compiler tests, tens of thousands of runtime test files, platform CI, stress systems, and fuzzing |

## New high-confidence finding: `ref` is not a real alias

### What C# means by `ref`

Roslyn represents parameter passing mode directly. `RefKind.cs` defines
`None`, `Ref`, `Out`, `In`, and read-only reference variants. Every
`ParameterSymbol` owns one `RefKind`; it is not inferred from a side table or a
bit position.

That representation follows the semantics: a C# `ref` parameter denotes the
caller's variable. Reads and writes in the callee access that storage. C++
references and ordinary pointer-based C APIs have the same aliasing property.

Suppose a function conceptually does this:

```text
void observe(ref int first, ref int second) {
    first = 7;
    print(second);
}

int value = 1;
observe(ref value, ref value);
```

Under reference semantics, the print observes `7` because `first` and
`second` denote the same place. The final value is also `7`.

### What the Aster VM does

At function entry, `src/vm.c` stores the caller pointer in `references[i]` but
also copies `*references[i]` into `locals[i]`. All ordinary local loads and
stores subsequently operate on `locals[i]`. `OP_STORE_LOCAL` and
`OP_SET_LOCAL` do not update the pointer target. On frame completion, Aster
loops over the parameters and copies each local back through its saved pointer.

Therefore the two parameters in the example become two independent local
copies containing `1`. Assigning `first` changes only the first copy. Reading
`second` can still observe `1`. During writeback, the first local writes `7`,
then the second local can overwrite it with its stale `1`. Both the observation
and the final state differ from C# and C++ reference semantics.

The bytecode lowering comment in `src/ir_bytecode.c` says a borrowed parameter
must remain an alias, but the alias retained there is only the relationship
between the IR parameter and its callee local slot. The caller storage is still
updated only at frame completion.

### What generated C does

This is not merely a VM implementation discrepancy. `src/c_backend.c` emits a
borrowed parameter read as:

```c
vN = *pN;
```

IR construction then stores that value in the function's corresponding local.
At a return or propagated exception, the C backend emits:

```c
*pN = lN;
```

Thus generated C also implements independent local copies with exit-time
writeback. Passing the same pointer for two parameters does not make the two
locals aliases.

### Why this matters

Copy-in/copy-out is a legitimate parameter convention in some languages. It is
not inherently broken. The problem is calling it `ref`, documenting in-place
mutation, and comparing it to C/C++-style references while allowing aliasable
places. In that setting programmers will reasonably expect reference
semantics.

This affects more than a contrived same-variable call:

- two fields or projections may overlap;
- native callbacks can observe caller state during the call;
- nested calls may receive one alias while another local copy remains stale;
- exception-time writeback makes partial mutations visible in an order that
  may not match source operations;
- destructor-bearing values can make delayed replacement and alias collision
  substantially harder to reason about.

A conventional fix does not require borrow checking. The backend can represent
a `ref` parameter as a pointer/reference to storage and lower loads, stores,
field accesses, and indexed accesses through it. This is precisely normal C,
C++, and C# implementation territory. If Aster deliberately prefers
copy-in/copy-out, it should name and document that convention explicitly and
define overlap/writeback order.

## Per-parameter modes should not be 32-bit masks

Aster represents parameter facts at several layers using masks:

- `Type.borrowed_argument_mask`;
- `Type.mutable_borrow_argument_mask`;
- `Type.out_argument_mask`;
- expression-level `ref_argument_mask` and `out_argument_mask`;
- `BytecodeFunction.borrowed_parameter_mask`;
- compact native-call encodings with still tighter practical space.

This produces multiple subtly different limits. Function values diagnose
`ref` or `out` parameters at position 32 and beyond. Direct function
declarations do not have a corresponding general arity limit. Bytecode
construction silently records borrowed direct parameters only below 32.

More seriously, `src/vm.c` tests the mask during argument setup using
`UINT32_C(1) << i` without first proving `i < 32`. A direct function with more
than 32 arguments can therefore reach an invalid C shift even when the extra
parameter is not a reference parameter. Later cleanup paths do contain
`slot < 32` guards, showing that the limit is known but not enforced at the
entry boundary.

Roslyn's design is the straightforward control: each parameter has a small
`RefKind` enum value. Aster can do the same in C with an enum or byte in each IR
and bytecode parameter descriptor. That is not a Rust-like type system. It is
ordinary structural metadata and removes:

- the arbitrary 32-parameter semantic cliff;
- undefined bit shifts;
- synchronized parallel masks;
- loss of mode information during lowering;
- the need to pack unrelated facts into instruction integers.

If compact bytecode is important, a separate encoded parameter descriptor
table is still cheap. Function arity is normally small, and correct metadata is
more important than saving a handful of bytes per function.

## `out`: honestly documented, still incomplete

Aster already distinguishes `out` from `ref` in syntax and function type
matching. That is useful. Its documentation also explicitly admits two missing
C# facilities:

- inline output declarations;
- compile-time proof that every normally returning path assigns the output.

Roslyn shows the implementation discipline behind the second facility.
`FlowAnalysis/DefiniteAssignment.cs` tracks assignment state and issues
`ERR_ParamUnassigned` when an output parameter is not assigned before control
leaves the method. Its diagnostic resources separately cover use of an
unassigned output parameter. This is CFG dataflow, not ownership theory.

At present, Aster requires the caller's `out` argument to be an existing,
initialized mutable local. The callee begins with the incoming value in the
same way as `ref`, and both backends write a replacement back at exit. In
practical semantics, `out` is therefore largely a signature/call-site label on
copy-in/copy-out `ref` behavior.

This is not hidden AI slop because the limitation is stated in
`docs/language.md`. It is nevertheless a production gap. A C#-conventional
completion would:

1. mark `out` parameter storage unavailable on function entry;
2. reject reads before assignment;
3. require it to be assigned on every normal return path;
4. define what happens on exception propagation;
5. replace or destroy any previous caller-owned value exactly once.

Aster already has checked CFG IR and initialized-slot machinery. Those are
appropriate foundations for this analysis.

## Async lowering: one validated choice and one coarse shortcut

### The validated choice

Aster rejects `ref` and `out` parameters on async functions. C# does the same:
Roslyn's resources report that async methods cannot have `ref`, `in`, or `out`
parameters, and its async capture walker asserts that ordinary async parameters
are not references.

This is a good restriction. Suspending a function while retaining a reference
to caller stack storage would require a more complicated lifetime contract.
Aster avoids that contract without inventing a borrow checker.

### The coarse shortcut

Roslyn computes variables live across `await` or `yield`. In release builds it
hoists those variables into the generated state machine. Debug builds retain
some extra values for debugging quality, but the release rule is explicitly
liveness-based.

Aster's generated-C async frame contains:

- every parameter;
- every local;
- a live flag for every cleanup-tracked local;
- every IR virtual value.

At every pending `await`, it copies all of them into the frame. On resume, it
restores all of them before dispatching to the continuation state. The frame is
zero-initialized, so this inspection did not find an uninitialized-read defect
in that mechanism. It is instead a potentially large space and copy cost.

The bytecode async path is similarly broad: it allocates storage for all
function locals, reference slots, initialized flags, HTML tracking, and a fixed
1,024-value operand stack per suspended operation.

This resembles the VM's blanket frame allocation issue. A function with many
temporaries pays for them for the entire suspension even if only one small
value is live across an `await`. The remedy is conventional compiler liveness:
compute the suspension-live set, assign frame fields only to that set, and
persist matching live/cleanup state. No source-language ownership change is
required.

## Type syntax: Roslyn reinforces the structural case

Roslyn's `Syntax.xml` declares a real hierarchy rooted at `TypeSyntax`.
Predefined, array, pointer, nullable, tuple, function-pointer, reference, and
generic type components are represented by nodes and typed fields. For
example, an array type contains an `ElementType` node and rank specifiers; a
function pointer contains a structured parameter list.

Aster's parser instead assembles type syntax into canonical-looking strings
such as `Option<T>`, `fn(...)->...`, `*mut T`, and `[T;N]`. The checker then
recognizes prefixes, searches delimiters, splits substrings, allocates more
strings, and recursively resolves them.

This is not merely less elaborate than Roslyn. It loses structure that the
parser already knew and makes the checker implement a second partial type
parser. A concrete fragility remains in `split_generic_application`:
it tracks nested angle brackets but not parentheses or brackets when deciding
whether a comma separates generic arguments. A function type used as one
generic argument can therefore be split at a comma inside its parameter list.
Another function-type parser elsewhere does track parentheses, angle brackets,
and brackets, demonstrating inconsistent delimiter knowledge.

Roslyn also generates much of its syntax and bound-node boilerplate from
`Syntax.xml` and `BoundNodes.xml`. Aster does not need Roslyn's enormous syntax
API, immutable green/red tree machinery, or compatibility surface. It would
benefit from a small `TypeSyntax` union containing source spans and structural
children. That is basic compiler construction, not imitation for its own sake.

## Parser collection growth is again below the mature baseline

Roslyn uses pooled `ArrayBuilder<T>` instances and immutable arrays throughout
the compiler. The underlying builders have capacity and `EnsureCapacity`
operations and are returned to pools where suitable.

Aster's `parser_grow_array` allocates exactly `count + 1` elements in the
module arena and copies all prior elements on every append. Because an arena
does not reclaim the previous array, every intermediate allocation remains
until the module is destroyed. The parser calls this helper directly at more
than twenty collection sites, including declarations, arguments, fields,
blocks, match arms, and interpolation parts.

This gives a long source list quadratic copying and quadratic retained arena
space. The arena itself is a sensible compiler-lifetime allocator; using it as
a grow-by-one vector is the mistake. Conventional remedies include:

- a temporary geometric-capacity vector followed by one arena freeze;
- arena-backed chunks linked during construction and flattened once;
- geometric allocation where abandoned buffers are accepted but far fewer.

This is one of the clearest AI-shaped implementation shortcuts: the helper is
locally simple and broadly reusable, but its aggregate cost was not considered.

## Manual memory management versus .NET resource management

### CoreCLR does not abolish manual resource discipline

The managed heap and Aster's object model solve different problems. Still,
the .NET source is valuable because it makes the resource boundary explicit.

CoreCLR's native `holder.h` supplies wrappers parameterized by acquire and
release operations. `NewHolder<T>` deletes one object at scope exit;
`NewArrayHolder<T>` uses `delete[]`; other holders release locks, decrement
counts, restore state, or call interface `Release`. This is deterministic C++
RAII around manually managed resources.

Aster's explicit IR cleanup, reverse-order scope destruction, error-path
cleanup, and narrow resource wrappers belong to the same engineering family.
The syntax and implementation language differ; the core idea does not.

### `NativeHandle` is directionally similar to `SafeHandle`

.NET's `SafeHandle` wraps an operating-system handle in an identity-bearing
object. It records whether it owns the handle, combines a reference count with
closed/disposed state, uses atomic transitions, blocks new references once
release is committed, releases the underlying handle once, and implements
`IDisposable`. A finalizer is a managed-runtime fallback, not the desired
normal release path.

Aster's `NativeHandle` is deliberately smaller:

- a native payload and destructor are registered together;
- copies retain shared identity;
- the last deterministic drop invokes the destructor;
- VM and generated C both guard reference-count overflow;
- ordinary aggregate copies can safely share the external resource.

That is a good design for a deliberately manual language. Aster does not need
to copy SafeHandle's GC finalizer or all of its hostile multithreaded state
machine. It does need an explicit concurrency contract. The current reference
counts are non-atomic, so either handles are thread-confined or retain/release
must become synchronized before values cross threads.

### C# does not make disposable value types noncopyable

C# structs copy by value. A struct may implement `IDisposable`, and copying it
does not automatically become illegal. This is conventional but can be a
footgun when a struct directly owns an exclusive unmanaged resource. Mature C#
code normally puts the resource identity in a reference object such as
`SafeHandle`; struct copies then share that object rather than duplicating a raw
handle and independently closing it.

That supports a conventional Aster policy without importing Rust:

- plain data and safely copyable owners may retain ordinary copying;
- external shared resources should normally use `NativeHandle`;
- a user-defined copy operation should be available for types requiring deep
  or special copying;
- a type may disable implicit copying, like a C++ deleted copy constructor;
- compiler warnings can flag a destructor-bearing type whose implicit field
  copy is suspicious.

These are rule-of-three/rule-of-five and `IDisposable`-style concerns. They do
not require affine types, lifetime parameters, or a borrow checker.

### The current destructor rule is still too optimistic

Aster documentation says a user-defined destructor is copied along with a
value, comparing this to a normal C++ class copy. The dangerous part is that a
normal C++ class with a destructor and raw exclusive resource generally needs
a deliberate copy constructor, deleted copy operation, or shared resource
member. Blind memberwise copy plus two destructor executions is the classic
rule-of-three bug.

Aster's built-in types avoid much of this:

- containers deep-copy;
- strings and tasks use narrow shared identity;
- `NativeHandle` shares resource identity;
- `Arena` is already noncopyable.

The unresolved case is a user aggregate with a custom destructor and fields
whose copied representation does not itself encode safe sharing. Production
hardening should add conventional copy control. It should not remove manual
memory management or force every value into a Rust-like ownership regime.

## Architecture lessons from Roslyn

### Generated descriptions are useful when the domain is closed

Roslyn's syntax and bound-node families are declared in XML and used to
generate repetitive node, visitor, and update code. This makes field shape and
nullability centrally reviewable. Roslyn still contains large handwritten
registries and lowering switches; mature code does not mean every fact comes
from one universal schema.

The grounded Aster lesson is category-specific generation:

- one structural declaration for type syntax nodes;
- one descriptor per built-in family or backend ABI category;
- generated opcode names/metadata where multiple consumers must agree;
- handwritten semantic implementation where behavior is genuinely distinct.

This is more precise than demanding one giant built-in registry.

### A compiler context should own configuration

`CSharpCompilation` is an explicit object containing options, syntax trees,
references, and semantic state. Operations such as `WithOptions`,
`WithReferences`, and `AddSyntaxTrees` produce compilation values with clearly
scoped state and deliberate reuse.

Aster has proper module and loader structures, but executable-path and standard
library configuration still begin in mutable process-global variables in
`src/common.c`. That weakens concurrent and multi-tenant embedding. The
conventional fix is an explicit compiler/options context. It does not require
making every structure immutable or cloning Roslyn's workspace model.

### The IR contract needs to match reality

Aster's architecture document says backends must consume IR and never inspect
or reinterpret raw AST. The current implementation still keeps a declaration
pointer in `IrFunction` and `BytecodeFunction`; the CSS backend walks source
function bodies, and C function type emission consults `IrType.checked_type`
for reference-parameter masks despite the field comment saying backends never
interpret it.

Roslyn does not prove that a backend must be isolated from all frontend
objects. Roslyn's emit and lowering layers legitimately operate on bound nodes
and symbols. The lesson is instead to state and enforce one real boundary.
Aster may either:

- finish making the IR self-contained; or
- explicitly define typed declarations and symbols as supported backend input.

The present mismatch between documented invariant and implementation is the
problem, not the mere existence of a frontend link.

### Assertions and diagnostics are part of implementation discipline

Roslyn-generated bound nodes emit debug assertions for non-null fields and
valid immutable arrays. Roslyn and CoreCLR contain extensive assertions and
contracts around internal assumptions. Aster's `src` tree presently contains
only one direct `assert`-family occurrence. Runtime checks and IR diagnostics
do cover many failures, so this count is not a complete quality measure, but
important internal invariants are often left implicit.

Aster diagnostics are impressive for the project's size: spans, secondary
spans, notes, help, and source-aware runtime failures are all present. Their
public representation nonetheless truncates messages to 256 bytes and allows
only four secondary spans and four notes. Roslyn uses symbolic error codes,
resource-backed message text, location-bearing diagnostic objects, and large
diagnostic test suites. Aster need not implement localization now, but it
should avoid silently discarding diagnostic information at arbitrary fixed
limits.

## Existing defects that C# does not excuse

The C# comparison does not erase earlier concrete findings.

### Empty string and zero-length memory operations

At the inspected Aster commit, the prior sanitizer build completed but one of
342 tests failed under UBSan. `src/vm_builtins.c` passes
`extension.data` to `memcpy` even when `extension.length` is zero and the empty
string view is represented with a null data pointer. A zero length does not
make a null argument valid for a C library parameter declared non-null.

This is a low-level representation invariant failure. Decide centrally whether
empty views use canonical non-null storage or whether every memory operation
must guard zero length, then apply the rule consistently.

### Allocation failure contract

The public header says `lang_vm_new()` may return null after recoverable
allocation failure. Its implementation calls `vm_allocate`, which terminates
the process on ordinary allocation failure. An embedded runtime must choose one
contract and honor it.

### Fixed VM storage

The VM reserves a 128-frame arena, a 1,024-value operand stack per frame, and a
uniform local stride based on the largest function. Async frames reserve
another fixed 1,024-value stack. Bounded execution is a defensible language
choice, but limits must be explicit, validated before use, and allocated in
proportion to the function where practical.

## What does not count as AI slop

The following differences from Roslyn/.NET are not evidence of bad code by
themselves:

- A handwritten parser;
- large C switch statements for a compact compiler;
- using an arena for compiler-lifetime objects;
- a bytecode interpreter instead of IL plus a tiered JIT;
- no general garbage collector;
- deterministic manual destruction;
- reference-counted strings, tasks, cancellation state, and native handles;
- a C17 backend;
- rejecting async reference parameters;
- a small diagnostic representation;
- fewer abstraction layers than Roslyn;
- lack of Roslyn-compatible incremental IDE syntax trees.

Aster's typed CFG IR, verifier, source spans, explicit cleanup operations,
dual execution paths, strict warning build, sanitizer support, and broad tests
are substantial work. They are inconsistent with the idea that the repository
is wholesale generated nonsense.

The AI-shaped debt appears where a locally easy encoding substitutes for a
stable structural contract:

- type trees flattened into strings and reparsed;
- arrays grown one element at a time inside a non-freeing arena;
- parameter modes compressed into masks with silent cliffs;
- copy-in/copy-out labeled as ordinary reference aliasing;
- all async values saved instead of computing liveness;
- backend metadata recovered from bootstrap AST/checker pointers despite the
  documented IR boundary;
- repeated name comparisons and numeric built-in IDs across subsystems;
- fixed-size limits that are guarded in some paths but not others.

That is material technical debt, but it is localized and repairable without
changing Aster's personality.

## Grounded production priority

### Priority 0: settle correctness semantics

1. Choose true alias semantics for `ref`, or explicitly rename and specify
   copy-in/copy-out including overlap and exception writeback.
2. Replace parameter-mode masks with per-parameter descriptors and eliminate
   every out-of-range shift and truncated mode.
3. Complete `out` definite-assignment rules using ordinary CFG dataflow.
4. Establish one empty string/slice pointer invariant and make the sanitizer
   suite clean.
5. Make public allocation-failure contracts match implementation behavior.
6. Add differential tests specifically for overlapping `ref` arguments,
   exception-time reference mutation, and destructor-bearing reference values.

### Priority 1: remove structural shortcuts

1. Introduce a structural `TypeSyntax` representation with spans.
2. Replace arena grow-by-one arrays with capacity-aware construction.
3. Compute async suspension liveness and persist only required locals and
   temporaries.
4. Make the backend input contract honest: closed IR or explicitly supported
   typed declaration metadata.
5. Validate all VM arity, frame, local, stack, mask, and compact-encoding
   limits before execution.

### Priority 2: harden manual resource behavior

1. Add conventional user-defined copy support and a way to disable implicit
   copying for selected types.
2. Keep `NativeHandle` as the normal shared external-resource boundary.
3. Document whether reference-counted values are thread-confined; use atomic
   retain/release only where cross-thread sharing is supported.
4. Specify assignment, copy, and destructor behavior for custom
   destructor-bearing aggregates.
5. Extend tests from final printed output to copy counts, destructor order,
   shared-handle lifetime, and alias observations.

### Priority 3: improve engineering scale

1. Move process-global compiler configuration into an explicit context.
2. Generate metadata for closed repeated categories rather than synchronizing
   strings and numeric IDs manually.
3. Add continuous sanitizer CI and fuzz parser, type resolution, IR verification,
   bytecode decoding, and VM execution.
4. Make test discovery data-driven.
5. Add internal assertions for invariants that should be impossible after
   checking or IR verification.

## Final answer on manual memory management

Aster should keep manual memory management.

The C# and .NET source does not suggest replacing it with GC or importing Rust.
It suggests making the existing model more explicit and mechanically reliable:

- use deterministic cleanup;
- wrap shared external identity in a reference-counted handle;
- use direct references as real aliases;
- give special resource-owning types deliberate copy behavior;
- allow copying to be disabled where ordinary memberwise copy is unsafe;
- use CFG dataflow for `out` and initialized-state checks;
- make allocation, empty-value, thread, and failure invariants central;
- test aliasing and cleanup under exceptional exits.

Those are conventional lessons from C, C++, C#, Roslyn, and CoreCLR. They
preserve exactly what is interesting about Aster while removing the prototype
shortcuts that currently make parts of the runtime unsafe or semantically
surprising.
