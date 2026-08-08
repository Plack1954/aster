# Aster TODO

## C++-style value copy control (historical plan)

> Superseded by the move-by-default and explicit `copy(value)` design in
> [`docs/ownership-and-copy.md`](docs/ownership-and-copy.md). The checklist
> below records the earlier implementation plan and is not the current source
> semantics.

Resolve custom resource-owning values with conventional C++ copy control. This
work does not introduce Rust ownership, borrow checking, lifetime annotations,
move-only-by-default values, or general alias analysis. Aster keeps the C/C++
trust boundary: the language defines copying and destruction, while pointer and
alias validity remains the programmer's responsibility.

Use the existing typed-IR `IR_COPY_TRIVIAL`, `IR_COPY_DEEP`,
`IR_COPY_SHARED_RETAIN`, `IR_COPY_NONCOPYABLE`, and `IR_COPY_CUSTOM` policies
and `IrType.copy_function`. Study Clang's copy-constructor classification,
deleted-copy diagnostics, and Rule-of-Three warnings for established behavior;
do not reproduce unrelated C++ special-member complexity.

### 1. Freeze the bounded language rule

- [x] Record a decision defining three user-value cases: implicit recursive
  copy, a user-defined copy constructor, and a deleted copy constructor.
- [x] Keep ordinary Aster values copyable by default when every field is
  copyable. A destructor alone does not make a type noncopyable.
- [x] Define assignment as constructing the replacement copy before destroying
  the old destination, making self-assignment and copy failure well-defined.
- [x] Keep class-variable assignment as pointer-like alias copying. A class copy
  constructor, if declared, is used only by explicit object construction such
  as `new Widget(existing)`.
- [x] Keep compiler-internal moves and return-value elision as implementation
  machinery. Do not add source lifetime or borrow rules as part of this work.

### 2. Add the source surface and AST

- [x] Approve one Aster/C++-family spelling for an immutable reference
  parameter, provisionally `const ref T`.
- [x] Parse a copy constructor on a value type, provisionally
  `public Buffer(const ref Buffer other) { ... }`.
- [x] Parse a deleted copy constructor, provisionally
  `private Buffer(const ref Buffer other) = delete;`.
- [ ] Retain the owner declaration, source span, custom/deleted state, and copy
  parameter on the AST; add parser recovery tests for malformed declarations.
- [x] Do not add C++ copy-assignment operators initially. Aster value assignment
  can reuse copy construction followed by replacement.

### 3. Implement checker semantics

- [x] Recognize exactly one canonical copy constructor per concrete type and
  diagnose duplicate or invalid signatures.
- [x] Compute copyability recursively: a default-copied type is noncopyable when
  any field is noncopyable; a custom copy constructor replaces that default;
  `= delete` always rejects copying.
- [ ] Route every semantic copy site through the selected policy: initialization,
  assignment, value parameters, returns that cannot be elided, field/index
  reads, aggregate copies, collection insertion, iteration, and generic
  instantiation.
  Direct sites now do so, including self-assignment, returns from `const ref`,
  deconstruction, `foreach`, and `List.Get`/`Queue.Peek`/`Stack.Peek`/
  `Dictionary.Get`, plus the conditional `Queue.TryPeek`, `Stack.TryPeek`, and
  `Dictionary.TryGetValue` out-parameter paths.
- [ ] Check that a custom copy constructor completely initializes its result and
  cannot mutate its immutable source through the reference.
- [x] Produce one consistent deleted-copy diagnostic that identifies both the
  copy site and the declaration that deleted copying.
- [ ] Add a Clang-style warning for a user destructor plus raw-pointer fields
  when copying remains implicit. It is a warning about likely Rule-of-Three
  mistakes, not an ownership error.

### 4. Complete typed IR and verification

- [x] Lower user copy constructors to a concrete monomorphized copy function and
  set `IR_COPY_CUSTOM` plus `IrType.copy_function`.
- [x] Preserve `IR_COPY_NONCOPYABLE` for deleted copy and recursively
  noncopyable types.
- [ ] Define and document the custom-copy IR ABI, including source-reference,
  destination initialization, failure, exception, and cleanup behavior.
- [x] Make `value_clone` call the custom copy function without changing ordinary
  move or return-elision paths.
- [x] Extend the IR verifier to check the copy function's exact signature,
  concrete type identity, and compatibility with its copy policy.

### 5. Implement both backends

- [x] Teach IR-to-bytecode lowering and the VM clone path to invoke custom copy
  functions and preserve exactly-once cleanup on success, trap, or exception.
- [x] Emit a typed C copy helper and call it at every `IR_COPY_CUSTOM` clone.
- [x] Ensure generated C constructs a replacement before dropping an assignment
  destination and remains warning-clean under the strict suite.
- [x] Keep deleted-copy rejection entirely before backend execution; neither
  backend should receive an attempted clone of a noncopyable type.

### 6. Prove the semantics

Current recursive-copy coverage: nested user structs, generic user-struct
instantiations, fixed-size arrays, `Option`, `Result`, and payload-bearing user
unions execute member copy constructors in both the VM and generated C. Tagged
union lowering borrows and copies only the active payload. `List`, `Stack`, and
`Queue` copy each element, while `Dictionary` copies entries and rebuilds its
hash index. Custom-copy dictionary/set keys remain untestable until user value
types can satisfy the key-equality constraint.

- [x] Add VM/generated-C differential tests for implicit recursive copying and
  custom deep copying of a raw allocation, plus checker rejection for deleted
  copying.
- [x] Test custom-copy values nested in arrays, structs, unions, `Option`, and
  `Result` in the VM and generated C.
- [x] Test custom-copy values in `List`, `Stack`, `Queue`, and `Dictionary`
  values in the VM and generated C.
- [ ] Define user value equality/hashing before permitting custom-copy structs
  as `Dictionary` or `HashSet` keys.
- [x] Test initialization, assignment, self-assignment, parameter passing,
  return, field/index reads, `foreach`, and collection insertion.
- [x] Lower and test conditional custom-copy reads through `Queue.TryPeek`,
  `Stack.TryPeek`, and `Dictionary.TryGetValue`, including default `out` values
  on failure in both the VM and generated C.
- [x] Test destructor counts and independent mutation after copying.
- [ ] Test a throwing/failing copy and prove that the old destination and source
  remain valid and that partial destination state is cleaned exactly once.
- [ ] Add negative fixtures for invalid signatures, duplicate copy constructors,
  copying deleted values, and recursively noncopyable aggregates/generics.
- [ ] Run the strict generated-C suite plus ASan/UBSan/leak checks for the new
  resource-owning fixtures.

### 7. Document and close

- [x] Update `docs/language.md` and `docs/values-and-cleanup.md` with the final
  syntax, copy-selection rules, assignment ordering, and examples.
- [x] State plainly that copy control follows C++ resource-management practice
  and adds no Rust lifetime or borrowing model.
- [ ] Add one small example containing a normal value, a custom-copy resource,
  a deleted-copy resource, a class alias, and a raw pointer so the categories
  can be compared in one place.
- [ ] Remove this section once every item and the full test suite pass; retain
  the stable rules in the language reference and architecture decisions.

## Async/await

The C#-shaped front end, typed IR, VM, and generated-C backend implement
`async`, `await`, `Task`, task combinators, timers, exceptions, deterministic
cleanup, and cooperative `CancellationToken` behavior. Continue
`docs/async.md`:

- replace the implemented timer-driven libcurl-multi polling bridge with native
  socket-readiness registration if profiling justifies the added executor API;
- add unknown-length/chunked upload streams if a concrete protocol consumer
  needs them; fixed-length bounded streaming uploads are implemented;
- add cancellation registrations and linked-token sources when real code needs
  them;
- keep Aster Web's implemented `Task<Response>` handler and `DispatchAsync` paths
  covered while the transport layer becomes cancellation-aware.
- add async process waiting and concurrent standard-stream draining only after
  the executor has a general native-readiness registration path; the bounded
  synchronous POSIX `Process` API is implemented.

## Remaining bounded-overload extension

Bounded C#-style overloads are implemented for free functions, static and
instance methods, function values, `ref`, and generic overloads separated by
arity. Calls resolve by arity and exact argument types; duplicate signatures
and ambiguous calls are diagnosed. The selected declaration is preserved
through the VM, typed IR, and generated C. Aster Web and Nook use overloaded `Get`.

- Add trial inference when multiple generic templates share both a name and
  an arity. Do not add conversion ranking or generic-preference rules.

## Signed collection-index ergonomics

Aster currently uses `nuint` for `List<T>.Count`, indexes, capacities, and
range arguments. This matches C/C++ `size_t`, C# `nuint`, Rust `usize`, the
generated-C representation, and Wasm memory sizing, but makes familiar C#-style
`int` loops noisy:

```aster
for (int index = 0; index < (int)items.Count; index++)
    Use(items[(nuint)index]);
```

Keep `nuint` as the storage and ABI type. Evaluate a bounded ergonomic layer
rather than introducing general implicit signed/unsigned conversions.

- [ ] Inventory every index/count surface together: `List`, arrays, strings,
  spans/slices, buffers, `Dictionary.KeyAt`/`ValueAt`, and range methods.
- [ ] Decide whether indexing expressions should accept signed integers through
  an explicit compiler-generated checked conversion that traps on negative or
  out-of-range values.
- [ ] Prototype checked `int` overloads for common list operations such as
  `Insert`, `RemoveAt`, `GetRange`, `RemoveRange`, and ranged `Reverse`.
- [ ] Decide how indexed `for` loops compare an `int` index with a `nuint`
  `Count` without adding unsafe global conversion ranking. Consider a narrowly
  checked comparison rule or retain explicit conversion at that boundary.
- [ ] Keep `Count`, `Capacity`, allocation sizes, and backend/runtime ABIs as
  `nuint`; do not cap collections at `int.MaxValue` merely for familiarity.
- [ ] Reject or trap negative indexes deterministically in the VM and generated
  C, with the same source-level diagnostic or runtime message.
- [ ] Add positive and negative differential tests for indexing, insertion,
  removal, empty collections, boundary values, and 32-bit Wasm behavior.
- [ ] Document the final rule with both idiomatic `foreach` and indexed-loop
  examples. Do not add LINQ, lambdas, or unrelated collection APIs as part of
  this task.
