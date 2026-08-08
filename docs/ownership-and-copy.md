# Ownership, moves, and explicit copies

Aster makes the cheap ownership operation the default and makes potentially
expensive duplication visible in source.

## Core rule

Every value has a closed compiler-known copy policy. Assignment, argument
passing, aggregate construction, and return use that policy as follows:

- trivially copyable values are copied;
- values with deep, shared-retain, custom, or noncopyable policies are moved;
- `copy(value)` explicitly requests an independent copy and never consumes its
  source.

Trivial copying cannot allocate, retain shared storage, or call user code. It
includes scalar values, class references, raw pointers, plain enums, and
aggregates made entirely from trivial fields. Moving a value transfers its
existing representation without running its copy operation.

```aster
List<int> first = new();
first.Add(1);

List<int> transferred = first;       // move; no allocation or element copy
List<int> independent = copy(transferred); // explicit deep copy
```

There is no source-level `move`, rvalue-reference type, reference collapsing,
or perfect forwarding. The ordinary value path is already the move path when a
type has non-trivial copy cost.

## Moved locals

Moving from a local makes that local unavailable. Reading, borrowing, or moving
it again is a compile-time error until an assignment gives it a new value.
Cleanup ignores an unavailable local, so every owned resource is destroyed
exactly once.

```aster
List<int> destination = source;
Console.WriteLine(source.Count); // error: `source` was moved

source = new();
source.Add(2);                    // valid after reassignment
```

The rule is path-sensitive. A local is available after a branch only when it is
available on every path that reaches the following statement. A loop back edge
must preserve the availability state with which the iteration began.

## `copy(value)`

`copy` is a language intrinsic, not an ordinary overloadable function. It
accepts one value and applies the type's copy policy:

- trivial values are copied directly;
- shared immutable or shared-handle values retain their storage;
- containers and owning aggregates recursively copy their contents;
- a public copy constructor performs a custom copy;
- a type with a deleted copy constructor is rejected.

The result is a separately owned value. Mutating or destroying it must not
invalidate the source. `copy` may allocate, retain storage, and execute user
code, so the call remains visible even when optimization later removes some of
that work.

```aster
Buffer second = copy(first);
Use(first);  // valid: copy did not consume first
Use(second);
```

## Calls and returns

A by-value parameter consumes a non-trivial argument. A `ref T` or `const ref
T` parameter borrows its argument for the call and does not consume it.
An instance-method receiver is an implicit borrow; a method does not consume
the object merely because it was called with member syntax.

An async method on a value type is the exception: its receiver is moved into
the task frame because a borrow could outlive the initiating call. Use
`copy(value).MethodAsync()` when the caller must retain an independent value.
Class receivers remain cheap reference values. Async functions reject `ref`,
`const ref`, and `out` parameters rather than hiding a copy or retain.

```aster
void Store(Html page);                 // consumes page
void Inspect(const ref Html page);     // borrows page

Inspect(page); // page remains available
Store(page);   // page is moved
```

Returning a non-trivial local moves it into the caller. Fresh expressions are
constructed directly in their destination whenever possible. Neither operation
requires a deep copy.

## Projections and containers

A borrowed projection does not silently duplicate a non-trivial stored value.
Use `copy(container[index])` when the stored value must remain in the container,
or use a consuming collection operation when ownership should be removed from
the container.

A non-trivial direct field of an owned struct local can be moved out. This is a
destructive extraction: the field is transferred, every other live field is
cleaned up immediately, and the whole owner becomes unavailable. Aster does
not leave a partly initialized struct behind.

```aster
Envelope envelope = MakeEnvelope();
Html body = envelope.body; // moves body; cleans the rest of envelope
Use(envelope);             // error: `envelope` was moved
```

Use `copy(envelope.body)` instead when both the field value and the complete
owner must remain available. A field reached through a borrowed owner, and a
field or element stored in a container, cannot be destructively extracted.

Iteration is borrowed by default when the collection remains usable. Copying an
iterated non-trivial element must therefore be explicit.

## Classes and unsafe code

Class values are non-owning references with explicit `delete`, so assigning a
class value copies the reference. It does not clone the object. Aster does not
insert garbage collection or reference counting for class instances.

Raw pointers remain outside the ownership guarantee. `copy(pointer)` copies the
address; it does not duplicate the pointed-to allocation. Unsafe code remains
responsible for pointer validity, aliasing, and lifetime.

## Performance contract

The language guarantees that ordinary transfer of a non-trivial value does not
invoke its copy constructor, recursively copy elements, retain shared storage,
or allocate. Those effects occur only at an explicit `copy` expression or in an
API whose documented purpose is copying.

The typed IR records every local load, move, copy, store, and drop explicitly.
Both the VM and portable-C backend must implement the same ownership decisions;
neither backend may infer an additional copy.
