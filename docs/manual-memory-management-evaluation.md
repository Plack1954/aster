# Manual memory management evaluation

> Historical note: this evaluation predates Aster's move-by-default and
> explicit `copy(value)` decision. Its evidence about the inspected
> implementation remains useful, but its copy-by-default recommendations and
> descriptions are superseded by
> [`ownership-and-copy.md`](ownership-and-copy.md).

## Scope and position

Aster deliberately avoids a tracing garbage collector. That is a valid and
useful design choice, not a defect to be explained away. Deterministic cleanup,
visible allocation costs, ordinary value semantics, explicit mutable
references, arenas, native resource handles, and unsafe raw pointers are all
well-established techniques in C, C++, and C# systems programming.

This evaluation is therefore **pro manual memory management**. It does not
recommend a Rust-style borrow checker, lifetime annotations, source-level
ownership qualifiers, pervasive explicit moves, or mandatory `clone()` calls.
Those mechanisms would conflict with Aster's stated C/C++-like value model and
C#-shaped application surface.

For the broader compiler and runtime comparison that motivated this focused
review, see the [PHP grounding sweep](php-grounding-sweep.md).

The question is narrower and more useful:

> Is Aster's chosen deterministic memory model implemented coherently enough
> to become a trustworthy production language?

The answer is: **the foundation is conventional and sound, but several
contracts and low-level invariants still need production hardening.** The right
response is to finish this design, not replace it with garbage collection or a
different language philosophy.

## Executive verdict

Aster does not expose raw `malloc` and `free` as its normal application model.
It implements compiler-managed deterministic destruction:

- ordinary assignments and parameters have value semantics;
- immutable strings and identity-bearing handles can share storage through
  narrow reference counting;
- ordinary owning containers copy their contents;
- compiler-created ownership transfers avoid redundant temporary copies;
- cleanup-managed locals are destroyed automatically on every exit path;
- native resources are wrapped in handles with deterministic C destructors;
- arenas provide explicit grouped lifetime management;
- raw pointers remain available inside an explicitly unsafe boundary.

This is best described as **automatic deterministic memory management built on
manual C allocation**, broadly in the family of C++ RAII and C#'s deterministic
resource patterns. It is not tracing garbage collection, and it is not the
error-prone model where application authors must remember a matching `free` at
every call site.

The strongest part of the implementation is its cleanup pipeline:

```text
lexical scopes and expression exits
        |
        v
checker-produced reverse-order cleanup plans
        |
        v
typed IR move / clone / store / drop operations
        |
        +-----------------------+
        |                       |
        v                       v
generated-C live flags     VM initialized-slot flags
        |                       |
        +-----------+-----------+
                    v
          type-directed destruction
```

That is a respectable architecture. Cleanup decisions are made before the
backends, ownership-affecting operations are explicit in typed IR, and both
backends implement the same small state machine.

The principal weaknesses are:

1. A destructor-bearing user type may still receive a default fieldwise copy
   even when its destructor makes that copy unsafe.
2. Empty byte/string views do not have one uniformly enforced pointer
   invariant; UBSan already finds a real violation.
3. Allocation failure behavior is inconsistent between the public embedding
   API, VM helpers, generated executables, and a few recoverable operations.
4. Allocation-size overflow checking is present in many paths but is not
   centralized or universal.
5. The object-copy and drop policies are duplicated across the VM and generated
   C and therefore require stronger parity tests and canonical metadata.
6. The current `Arena` provides grouped lifetime but allocates each block
   separately, so it is not yet the fast bump allocator its name may imply.
7. Reference counts are deliberately narrow and non-atomic. This is correct for
   the current single-threaded execution model, but must become an explicit
   concurrency contract before multi-threaded executors are introduced.

None of these findings argues against manual memory management. They are the
normal hardening tasks required by a manual-memory runtime.

## Intended source-language model

The normative description in [values-and-cleanup.md](values-and-cleanup.md)
is intentionally simple:

- scalars copy directly;
- `string` copies retain immutable shared storage;
- `Buffer` and ordinary owning collections deep-copy;
- `NativeHandle` copies share one underlying native resource;
- aggregates recursively copy fields or payloads;
- `Arena` is noncopyable;
- `ref T` is an explicit mutable reference;
- pointers are unsafe and their validity remains the programmer's
  responsibility;
- destructors run automatically in reverse declaration order.

There is no source-level invalidation after an ordinary assignment. Aster does
not ask the programmer to distinguish every copy from every ownership transfer.
Instead, it preserves observable value semantics while allowing the compiler to
transfer fresh temporaries and returned locals internally.

That separation is important:

- **Source semantics:** ordinary copying, reference parameters, deterministic
  destruction.
- **Compiler implementation:** moves and live-state changes that remove copies
  without changing observable behavior.

An `IR_OP_LOCAL_MOVE` is therefore not an invitation to add move syntax to the
language. It is an implementation operation similar to copy elision or return
value optimization in C++.

## Cleanup architecture

### Checker cleanup plans

The checker records cleanup-requiring bindings in deterministic reverse order.
`set_cleanup_plan` in [checker.c](../src/checker.c) walks active locals backward,
excludes borrowed locals, and records stable binding identities rather than
source names. Stable identities are important because shadowed variables must
not be confused during later lowering.

Cleanup plans are attached to:

- ordinary block exits;
- `return`;
- `break` and `continue`;
- thrown exceptions;
- propagated `Result` errors;
- `try`/`catch` transfer boundaries;
- loop-owned iterators and element builders.

This is the correct layer at which to calculate lexical destruction order. The
checker understands scopes, declarations, exceptional transfer, and whether a
parameter is borrowed. Backends should not have to reconstruct that analysis.

### Typed IR ownership operations

The IR contains distinct operations for:

- loading a local without consuming it;
- moving a local and emptying its source slot;
- storing into a local;
- conditionally dropping a local;
- cloning an owning value;
- discarding an owning temporary;
- moving an aggregate field or union payload;
- beginning either an owning or borrowed iterator.

The semantics in [ir.md](ir.md) are appropriate:

- `local_move` transfers a value and leaves the source empty;
- `local_drop` destroys only an initialized owner;
- `value_clone` distinguishes a borrowed view from an owning temporary;
- cleanup operations appear directly on normal and exceptional control-flow
  edges.

This is substantially better than emitting ad hoc cleanup from each backend.
It gives the IR verifier and differential tests something concrete to inspect.

The current verifier checks instruction signatures, result types, local
indexes, definitions, dominance, and ownership-encoding fields. Runtime live
state still provides the final protection against conditionally initialized or
already-moved locals. That division is reasonable: not every conditional
initialization fact needs to become a complicated static proof when one local
boolean can implement the semantics directly.

### Generated-C live flags

Generated C gives each cleanup-managed local an adjacent live flag. The
lowering in [c_backend.c](../src/c_backend.c) follows a conventional pattern:

- storing into an already-live slot first drops its old value;
- a successful store marks the destination live;
- a move clears the source flag;
- a drop checks the flag, invokes the typed drop helper, and clears the flag.

This design handles branch-dependent initialization and early exits without
requiring unsafe assumptions from the C compiler. It is simple, inspectable,
and compatible with portable C17.

Generated drop helpers in
[c_backend_module.c](../src/c_backend_module.c) recursively clean containers,
fields, arrays, and active union payloads. Collection elements and struct fields
are visited in reverse order. User destructor bodies run before automatic field
cleanup, matching the conventional C++ destruction shape in which the class
destructor body executes before member destruction.

Live flags may look conservative compared with an optimizing native compiler,
but they are not a bad design. Ordinary C compilers can remove many redundant
flag stores and tests when control flow makes the state obvious. Correctness and
inspectability matter more than shaving a boolean from an early compiler.

### VM initialized state

The VM represents the same state dynamically. Each frame has local values plus
an initialized array. Moves clear initialization, stores replace live values,
and drops only act on initialized owning objects. Frame completion and trap
unwinding visit remaining live object locals.

`vm_value_drop_owned` and `vm_object_free` in
[vm_values.c](../src/vm_values.c) centralize recursive VM destruction. Arrays,
structs, vectors, dictionaries, queues, buffers, arenas, native handles,
iterators, tasks, strings, builders, and HTML each have an explicit destruction
case.

The VM is more dynamically represented than generated C, but it does not infer
ownership from arbitrary payload bits. Bytecode locals carry destructor and
borrowing metadata, while initialized state says whether the slot currently
owns a value. This is a sound development-runtime design.

## Memory policy by value category

| Value category | Current policy | Evaluation |
| --- | --- | --- |
| Integers, floats, booleans, enums | Direct value copy | Conventional and correct. |
| Raw pointers | Direct non-owning copy inside unsafe code | Conventional C semantics; no automatic lifetime guarantee should be implied. |
| Borrowed string/slice ABI views | Pointer plus length, call-scoped | Good FFI representation, but the empty-pointer invariant must be uniform. |
| Immutable `string` | Reference-counted immutable handle | Appropriate for application code; cheap ordinary copies and deterministic release. |
| `StringBuilder` | Unique mutable storage, deep clone where copying is required | Conventional, although source copy costs should remain visible and measured. |
| `Buffer` | Owning byte allocation, deep copy | Clear value semantics; appropriate for mutable buffers. |
| Lists, dictionaries, sets, queues, stacks | Owning storage with recursive value copy | Conventional C++-style container semantics; potentially expensive but not conceptually wrong. |
| Structs, arrays, options, results, unions | Recursive field/payload copy and destruction | Sound when every member has a coherent copy contract. |
| `NativeHandle` | Shared identity with reference-counted exactly-once C destructor | Strong conventional solution for files, sockets, SQLite handles, and other external resources. |
| `Arena` | Noncopyable grouped owner | Correct policy; allocator implementation needs performance work. |
| `Task` | Shared identity with reference-counted result/exception/frame state | Appropriate because a task is an observable shared completion handle. |
| Cancellation state | Shared reference-counted identity | Matches ordinary C# cancellation semantics. |
| `Html` and builders | Owning runtime storage with explicit composition/clone/drop paths | Specialized but coherent; complexity requires continued backend parity testing. |

This hybrid is not inconsistent merely because different types choose different
copy mechanisms. C++ and C# libraries do the same:

- immutable data is commonly shared;
- mutable value containers commonly copy;
- external resources commonly live behind shared or unique handles;
- views borrow storage;
- arenas group many allocations under one lifetime.

The important requirement is that each type's policy be stable, documented,
and implemented identically by both backends.

## Strong implementation decisions

### Deterministic cleanup covers non-local exits

A cleanup model is only useful if it survives more than the happy path. Aster
explicitly addresses normal fallthrough, early return, loop exits, propagated
errors, exceptions, cancellation, and VM traps. Tests exist for normal scope
cleanup, return cleanup, loop cleanup, exception transfer, nested destructors,
temporary discards, imported destructor identity, native callback failure, and
trap unwinding.

This is considerably more serious than a prototype that merely frees locals at
the closing brace.

### Native handles are the correct resource boundary

`lang_native_handle_value` associates an opaque C payload with an exactly-once
C destructor. Copies retain shared handle identity; the final drop invokes the
native destructor. This is the appropriate default for operating-system and
library resources exposed to application code:

- file streams;
- sockets;
- database connections and prepared statements;
- request objects;
- other opaque library handles.

It avoids shallow-copying naked resource pointers through ordinary structs and
gives Aster's normal copy syntax safe behavior. It resembles a conventional
reference-counted C handle or C++ `shared_ptr`, without requiring source-level
ownership ceremony.

Reference-count overflow is checked in both the VM and generated C. Destruction
is null-tolerant. The embedding header states whether arguments and returned
views are borrowed or owned. Those are all good manual-memory practices.

### Strings use narrow reference counting

Immutable strings are an excellent place to use reference counting. They are
copied frequently, cannot be mutated through aliases, and appear throughout
application APIs. Deep-copying every string assignment would be wasteful;
tracing all application objects would undermine the language's deterministic
model.

Generated C retains and releases string objects directly. Builders own mutable
capacity and can transfer their completed allocation into immutable string
storage. This is a sensible cost model.

### Aggregates have type-directed cleanup

The compiler does not reduce every object to an untyped generic finalizer.
Concrete IR types carry cleanup requirements and a verified user-destructor
function identity. Generated code can therefore produce direct typed helper
calls and recursively destroy exact field layouts.

This enables ordinary C compilers to inline and optimize cleanup. It also keeps
destructor selection out of string-based runtime lookup on the primary backend.

### FFI views do not silently extend lifetimes

Borrowed strings and mutable byte slices are documented as call-scoped. Native
callbacks must copy data they need to retain. Buffers do not acquire hidden
reference counts just because a slice was created.

This is normal C interoperation. The contract is strict, but it is legible and
does not pretend that unsafe native pointers can be made safe without cost.

### Fail-fast generated executables are defensible

Generated standalone programs currently terminate on allocation failure. That
can be a legitimate policy. Many native applications cannot meaningfully
recover after arbitrary heap exhaustion, and a single checked allocation
boundary is preferable to unchecked null dereferences.

The problem is not fail-fast behavior itself. The problem is that the public API
and some VM helpers presently describe or implement different policies. Aster
must choose and state the policy for each API surface.

## Findings requiring hardening

### 1. User destructors and automatic copies need one complete contract

This is the most important language-level memory issue.

Aster currently allows ordinary recursive copying of a type that has a custom
destructor. The documentation accurately admits that the compiler does not
prove that the destructor is compatible with the default copy.

That is safe for a destructor that merely observes the value or whose fields
already implement the complete ownership policy. It is unsafe when a destructor
manually releases external state not represented by a copy-aware field.

For example, a struct may contain a raw C pointer and free it from its
destructor. A default fieldwise copy duplicates the pointer. Both copies later
run the destructor, producing a double free. The same problem exists in C++ and
is why C++ has copy constructors, assignment operators, deleted copy operations,
and the rule of three/five.

Aster does not need Rust semantics to solve this. A conventional solution is:

1. Ordinary assignment and parameter passing remain ordinary source syntax.
2. A struct whose members all have sound value-copy behavior receives a
   compiler-generated memberwise copy.
3. External resources normally live in `NativeHandle`, whose automatic retain
   already makes ordinary copying safe.
4. A type author who manually owns state outside copy-aware fields supplies a
   type-level copy operation compatible with its destructor.
5. If no valid copy exists, the type can disable copying in the same spirit as
   a deleted C++ copy constructor. Call sites use the already-existing `ref`
   mechanism when reference semantics are intended.

The syntax for a user-defined copy operation is a separate language-design
decision. It should not require explicit `.clone()` at every assignment, and it
should not introduce source-level move or lifetime syntax. The copy operation is
selected automatically from the type, just as an ordinary C++ copy constructor
is.

The compiler should also diagnose a custom destructor on a trivially copied raw
pointer field unless the type explicitly establishes its copy policy. This is a
high-value diagnostic even though raw pointers remain unsafe.

### 2. Empty data needs one canonical invariant

The current generated-C string representation permits an empty string or view
to have a null data pointer. That representation is legal if every operation
obeys the invariant:

> `data` may be null exactly when `length` is zero, and no library operation
> passes it to an API whose contract rejects null pointers.

The implementation does not yet enforce that consistently. During this review,
the sanitizer-enabled suite reported undefined behavior in
`native_path_change_extension_value` at
[vm_builtins.c](../src/vm_builtins.c): an empty extension carried a null data
pointer, and the function passed that pointer to `memcpy` with a zero length.
The C standard library's non-null parameter contract still makes that undefined
behavior.

There are two conventional fixes:

- guarantee a non-null sentinel or allocated terminator for every empty
  string/view; or
- permit null empty views but guard every `memcpy`, comparison, write, and
  foreign call when the length is zero.

The first option makes leaf code simpler. The second can avoid a byte of storage
but spreads obligations across the runtime. Either is valid; mixing them is not.

The invariant must cover strings, builders, buffers, slices, HTML storage,
FFI-returned views, and generated constants. Helper functions for copy,
comparison, hashing, and foreign conversion should encode it centrally.

Zero-size allocation is part of the same contract. For example, the generated-C
buffer clone currently calls the fatal `aster_allocate(value->length)` even when
the logical length can be zero. ISO C permits `malloc(0)` to return null without
an allocation failure, so a generic allocator cannot interpret every null
zero-size result as heap exhaustion. Callers must normalize zero to a physical
allocation size, use a sentinel, or deliberately store a null empty buffer.

### 3. Allocation failure policy is inconsistent

`lang_vm_new` is documented in [lang.h](../include/lang/lang.h) as returning
`NULL` on recoverable allocation failure. Its implementation calls
`vm_allocate`, which prints an error and terminates the process on ordinary
allocation failure.

Elsewhere:

- generated C's `aster_allocate` terminates;
- some runtime `realloc` paths produce a VM runtime error;
- native result construction can return a static fallback error after `malloc`
  fails;
- integer-overflow checks sometimes return `NULL`, sometimes trap, and sometimes
  rely on the caller.

A conventional split would be:

- **Standalone generated executable:** fail fast on unrecoverable allocator
  exhaustion.
- **Public embedding constructors and registration APIs:** return failure as
  documented and never terminate the host process.
- **Language operations with specified fallible allocation:** produce their
  declared error or exception.
- **Internal impossible-state/overflow failures:** trap consistently with a
  precise diagnostic.

Fail-fast versus recovery is a policy choice. Contradicting the public contract
is an implementation defect.

### 4. Checked allocation arithmetic must be centralized

The code contains many good overflow checks around capacity doubling, string
growth, dictionary storage, and VM allocation helpers. However, other paths
still calculate expressions such as `count * sizeof(element)` before passing
the result to a one-argument allocator.

Manual-memory C code should centralize at least these operations:

- allocate one object;
- allocate a checked array of `count` elements;
- resize a checked array;
- add lengths with overflow checking;
- grow a capacity with a documented maximum;
- allocate zero logical bytes according to the canonical empty-data policy.

Generated C should emit calls to those helpers rather than repeating arithmetic
templates throughout thousands of lines of emitter logic. The VM and compiler
can use equivalent checked helpers.

This is not merely defensive polish. Integer wrap in allocation sizing can turn
a large logical allocation into a small physical allocation followed by an
out-of-bounds write.

### 5. VM reference-count zero must be treated as an invalid state

`vm_value_clone` currently contains recovery logic that changes a zero reference
count to one before incrementing it. A reachable reference-counted object should
never have a zero count. Zero means the object's lifetime has ended, so reviving
it masks an ownership error and risks use-after-free behavior.

The conventional policy is:

- constructors establish a count of one or another explicitly documented
  initial count;
- retains require a nonzero count and check overflow;
- releases require a nonzero count;
- the transition from one to zero performs destruction exactly once;
- retaining or releasing at zero is an internal runtime error.

The VM already traps on reference-count overflow. It should be equally strict
about underflow and resurrection.

### 6. The current arena is a lifetime arena, not yet an allocation arena

Generated C's `aster_arena_alloc` performs one allocation for linked-list block
metadata and another allocation for the requested bytes. The VM likewise
allocates a tracking object plus the data allocation and grows tracking arrays.

This provides useful semantics:

- all allocations are released together;
- reset invalidates every allocation;
- the arena itself has one deterministic owner;
- the VM can detect expired raw allocations.

But it does not yet provide the usual arena performance benefits. A conventional
C arena normally obtains larger chunks and advances an aligned bump pointer
inside the current chunk. Reset either rewinds the chunks or releases them in
bulk. Large exceptional allocations may receive dedicated chunks.

A production arena should therefore add:

- chunk capacity and current offset;
- alignment-aware bump allocation;
- checked size/alignment arithmetic;
- a growth policy and optional retained chunks after reset;
- a clear rule for oversized allocations;
- optional debug poisoning or generation tracking.

This is an implementation improvement within Aster's existing design. The
noncopyable grouped-lifetime source semantics should remain unchanged.

### 7. VM raw pointers and native raw pointers intentionally differ

The VM represents an arena raw pointer as a `RawAllocation *` tracking record.
Loads and stores validate that the allocation remains active and is large
enough. Generated C can use a real native pointer.

This VM representation is useful: it catches expired arena pointers and
undersized accesses during development. It is comparable to a debug C
allocator that wraps allocations with metadata.

The project should nevertheless document that this is a checked VM surrogate,
not the native pointer ABI itself. Pointer equality, arithmetic, casts, and
foreign interoperation must not accidentally acquire different source semantics
between the two backends.

### 8. Narrow reference counting needs an explicit concurrency boundary

Strings, native handles, cancellation state, and tasks use ordinary non-atomic
reference counters. That is correct for Aster's current single-threaded executor
and ordinary single-threaded application model. Atomic increments would add cost
without providing complete thread safety by themselves.

Before adding a worker pool or permitting Aster values to cross native threads,
the project must choose one conventional policy:

- values remain confined to an executor/thread;
- cross-thread transfer goes through explicit synchronized host APIs;
- or the relevant shared runtime types use atomic reference counts and
  thread-safe payload state.

This decision can remain deferred while the runtime is single-threaded, but the
current counters must not silently be advertised as thread-safe.

### 9. Shared-reference cycles must remain narrow or receive a policy

Reference counting does not reclaim cycles. Aster currently limits shared
identity to a narrow set of runtime types rather than making every aggregate a
reference object. That substantially reduces the cycle problem and is a good
reason not to add a general tracing collector preemptively.

If future tasks, continuations, callbacks, UI signals, or shared handles can
form arbitrary cycles, the project will need an explicit rule. Conventional
non-GC options include acyclic ownership contracts, weak references, explicit
cycle breaking, or executor-owned graphs released as a unit. The appropriate
answer should be driven by an actual application graph, not speculative runtime
generality.

### 10. Copy and cleanup policy is duplicated across backends

The C backend emits per-type clone/drop helpers from IR metadata. The VM has a
large `ObjectKind` switch implementing corresponding clone and destruction
behavior. This duplication is unavoidable to some extent because the
representations differ, but semantic facts should have one canonical source:

- whether a type is trivial, deep-copying, reference-counted, borrowed, or
  noncopyable;
- whether it owns elements or payloads;
- its destructor function identity;
- whether it may appear in an initialized VM object slot;
- how calls treat its arguments.

At present, some of this information is inferred from type shapes, some from
type names, some from checker flags, and some from backend-specific object
kinds. That makes memory policy vulnerable to drift when a new built-in type is
added.

The typed IR should eventually carry a closed copy/drop policy enum or equivalent
backend-facing metadata. This is ordinary compiler metadata, not a new
source-level ownership system.

### 11. Documentation contains representation and handle-policy conflicts

[values-and-cleanup.md](values-and-cleanup.md) and the embedding header say that
native-handle copies share their underlying resource. The VM clone operation
also retains native handles. [ffi.md](ffi.md), however, calls native handles
"move-only" and says they cannot be cloned.

This may intend to distinguish ordinary source assignment from an obsolete
explicit clone operation, but the wording makes the ownership contract
ambiguous. Resource documentation must state exactly:

- whether ordinary assignment retains;
- whether by-value calls transfer or retain;
- whether explicit copy APIs exist;
- when the registered C destructor runs;
- whether handles may cross threads.

The current implementation most closely resembles an automatically retained
shared handle, which is a good C#/C++-style policy.

[data-layout.md](data-layout.md) also says that source `string` occupies a
pointer-plus-length value, while the current language decision and generated C
use a pointer-sized handle to a reference-counted string object. A borrowed
native string view is pointer-plus-length, but it is not the owning source
representation. These statements should be reconciled before the ABI is called
stable.

## Copy semantics without Rust-style ceremony

Aster can keep its current pleasant source surface and still close the custom
destructor hole.

The recommended conventional model is:

### Trivial values

Scalars, plain enums, raw pointers, and aggregates containing only trivial
values use ordinary bitwise/memberwise copying.

### Compiler-generated value copies

Structs, arrays, options, results, and unions recursively invoke the established
copy operation of each active member. Containers allocate new storage and copy
their elements. No call-site syntax changes.

### Shared immutable or identity values

Strings, native handles, tasks, and cancellation state retain their shared
control block automatically. Assignment still looks like assignment. The final
release destroys the shared state.

### User destructor types

A user destructor does not automatically make a type invalid or undesirable.
The compiler checks whether the type has a coherent copy strategy:

- generated memberwise copy when the destructor does not introduce ownership
  outside copy-aware members;
- an automatically invoked type-defined copy operation when custom duplication
  or retain behavior is needed;
- disabled copying, analogous to a deleted C++ copy constructor, when there is
  no coherent duplicate.

Resource wrappers should normally put the resource in `NativeHandle` and then
allow their surrounding value struct to copy normally. This minimizes the need
for custom copy code.

### References and pointers

`ref T` remains the conventional explicit mutable-reference mechanism. Raw
pointers remain an unsafe escape hatch. Neither requires lifetime syntax or a
borrow checker.

This gives Aster the safety expected from well-written C++ RAII while preserving
the ordinary C#/C++ application syntax it wants.

## Comparison with PHP's manual runtime engineering

PHP is also implemented in C with deliberate manual memory management. Its
language surface happens to expose dynamically typed, garbage-collected and
reference-counted behavior, but the engine itself relies on explicit allocation,
reference counts, destructors, request lifetimes, and cycle collection.

PHP's memory architecture is more complex because PHP values can form arbitrary
dynamic graphs and because the engine serves long-running, adversarial web
workloads. It includes:

- a specialized request allocator;
- allocation size classes and chunk/page management;
- central `zend_string` and `zval` invariants;
- reference counting and copy-on-write containers;
- cycle collection for reference graphs;
- request startup and teardown boundaries;
- extensive memory-manager debugging and stress tests.

Aster should not copy PHP's entire memory model. In particular, it does not need
to introduce general tracing collection or copy-on-write semantics merely
because PHP uses them. Aster's deep-copy values and narrow shared handles can be
simpler and more deterministic.

The PHP lessons worth adopting are engineering lessons:

- centralize allocation arithmetic;
- define empty and zero-length representations once;
- make reference-count states strict;
- separate request/runtime/compiler lifetime domains;
- use one canonical declaration of each type's memory policy;
- test allocation failure and malformed sizes;
- fuzz parsing and runtime boundaries;
- make debug builds hostile to use-after-free and stale pointers;
- keep public embedding contracts exact.

PHP demonstrates that manual memory management becomes trustworthy through
stable invariants and sustained testing, not by avoiding manual memory
management.

## Test evidence

This evaluation used the current Aster source and a separate sanitizer-enabled
build configured with strict warnings. The full CTest inventory contained 342
tests. The result was:

- 341 passed;
- 1 failed under UBSan;
- the failure was the null empty-view pointer passed to `memcpy` described
  above.

Relevant passing coverage includes:

- deterministic generated-C cleanup;
- leak-checked normal scope, return, break, exception-transfer, and catch-entry
  cleanup;
- VM RAII cleanup;
- language destructors;
- nested destructors;
- aggregate cleanup;
- temporary-value cleanup;
- imported destructor copy checking and module identity;
- file RAII;
- native failure cleanup;
- destructor execution during a VM trap.

For a young language, this is strong coverage. It demonstrates that cleanup is
not merely documented; many difficult exit paths have executable tests.

Production hardening should add:

- allocation-failure injection at every allocator boundary;
- systematic zero-length inputs for every byte/string/slice function;
- maximum-size and overflow-oriented container tests;
- reference-count underflow, overflow, and invalid-state assertions;
- randomized sequences of container copies, replacement, and destruction;
- randomized control flow combining construction, exceptions, loops, and
  returns;
- differential VM/generated-C tests that include allocation and destructor
  event logs;
- fuzzing for the bytecode verifier and VM, not only source-level examples;
- repeated long-running application tests to expose retained memory growth.

These are ordinary C runtime tests. They strengthen manual memory management
rather than replacing it.

## Production hardening order

### Priority 0: correctness contracts

1. Choose and enforce one empty pointer/length invariant across all runtime and
   FFI values.
2. Fix the demonstrated sanitizer failure and audit every zero-length memory
   operation.
3. Define the automatic copy contract for destructor-bearing types using
   conventional copy constructors, shared handles, or disabled copying.
4. Align `lang_vm_new` and every public embedding API with a documented
   allocation-failure policy.
5. Centralize checked allocation and resize arithmetic in both the VM/compiler
   runtime and generated C.
6. Treat zero reference counts, underflow, and resurrection as internal errors.

### Priority 1: semantic consolidation

1. Put copy/drop/borrow policy in closed backend-facing type metadata.
2. Remove backend reliance on scattered type-name tests for memory behavior.
3. Resolve the `NativeHandle` copying documentation conflict.
4. Expand differential tests to compare destructor order and copy counts, not
   only final output.
5. State the single-threaded/non-atomic reference-count contract explicitly.

### Priority 2: allocator quality and observability

1. Turn `Arena` into a chunked, aligned bump allocator while retaining its
   current source semantics.
2. Add allocation counters by type and operation.
3. Measure allocation count, bytes allocated, peak live bytes, copy count, and
   destructor count on substantial applications.
4. Consider small-object or request-local allocation strategies only after
   measurements identify a real bottleneck.
5. Add debug poisoning, generation checks, or guard modes where they materially
   improve diagnosis.

### Priority 3: future concurrency

1. Decide whether Aster values are thread-confined or transferable.
2. Keep reference counts non-atomic while confinement is guaranteed.
3. If selected values become cross-thread, harden only those control blocks and
   payloads rather than making every allocation atomic.
4. Define how executor shutdown releases pending tasks, timers, continuations,
   frames, and native handles.

## Production acceptance criteria

Aster's memory model should be considered production-ready only when all of the
following are true:

- GCC and Clang warning-clean builds pass under ASan and UBSan with leak
  detection;
- every supported empty value representation is documented and tested;
- destructor-bearing types cannot acquire an incoherent automatic copy;
- every owning type has one tested copy, transfer, replacement, and drop policy;
- every cleanup edge behaves identically in generated C and the VM;
- public embedding calls never terminate the host contrary to their contract;
- allocation size arithmetic is checked before multiplication and addition;
- reference counts cannot be resurrected, underflowed, or silently overflowed;
- arena reset/drop behavior is tested against stale pointers in the VM;
- long-running application tests show bounded live memory;
- resource handles close exactly once under success, failure, exception,
  cancellation, and discarded-result paths;
- concurrency guarantees match the implementation of reference counts and
  mutable payloads.

## Final assessment

Aster's decision to use deterministic, manual-memory-backed resource management
is one of its stronger architectural choices. The checker-to-IR-to-backend
cleanup pipeline is coherent. Explicit IR drops, generated-C live flags, VM
initialized flags, recursive typed destruction, reference-counted immutable
strings, shared native handles, and noncopyable arenas are all conventional
solutions with good precedent in C, C++, and C# systems.

The implementation is not yet production-hardened. The sanitizer defect proves
that representation invariants still leak into leaf code. The custom
destructor/default-copy interaction needs a formal rule. Allocation failure and
overflow handling need consolidation. The arena needs a real chunk allocator if
it is intended to provide allocation performance as well as grouped lifetime.
Reference counting needs stricter state assertions and a stated concurrency
boundary.

These are reasons to invest further in Aster's manual memory model, not reasons
to abandon it. With conventional copy/destructor rules, centralized checked
allocation, hardened invariants, and sustained sanitizer and differential
testing, Aster can preserve deterministic cleanup and ordinary application
syntax without adopting a tracing collector or Rust-style ownership language.
