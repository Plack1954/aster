# Ownership, automatic last-use moves, and `copy`

Aster has ordinary value syntax and deterministic destruction without making
programmers spell routine ownership transfers. The compiler copies a copyable
value when its source is still needed and moves it when the transfer is the
source's last use.

## Core rule

Assignment, by-value arguments, aggregate construction, and returns use one
rule:

- trivial values are copied;
- a copyable non-trivial value is moved when its source is dead after the
  transfer, and copied when its source remains live;
- a noncopyable value may only be transferred from an owned source that is not
  used again before reassignment;
- `copy(value)` forces an independent copy and never consumes its source.

```aster
List<int> source = new();
source.Add(1);

List<int> first = source; // copies because source is used below
Use(source);
List<int> last = source;  // moves because this is source's last use
```

There is no source-level `move`, `take`, rvalue-reference type, reference
collapsing, or perfect forwarding. Last-use selection is mandatory compiler
analysis, not a backend guess and not an optional optimization.

## What “last use” means

The typed-IR ownership pass computes local liveness over the complete control-
flow graph. A transfer moves only at the last observable use of that ownership
incarnation: no reachable continuation may read it directly or through a live
overlapping safe borrow. Uses in either branch, after a merge, or on a loop
back edge keep it live. Reassigning a local ends the previous value's lifetime,
so a transfer before an unconditional reassignment can still move.

Scope-exit cleanup is not counted as a use. A moved slot is empty, and its
compiler-emitted drop therefore does nothing. This preserves exactly-once
destruction without forcing a copy merely to satisfy cleanup.

Copy constructors are observable. Adding a later use can turn an earlier move
into a copy and therefore invoke the type's copy operation. Code must not put
unrelated semantic effects in a copy constructor.

## `copy(value)`

`copy` is a language intrinsic for the uncommon case where duplication itself
is required. It accepts one value and applies the type's copy policy:

- trivial values copy directly;
- immutable strings and shared handles retain their storage;
- containers and owning aggregates recursively copy their contents;
- a public copy constructor performs a custom copy;
- a type with a deleted copy constructor is rejected.

```aster
Buffer second = copy(first);
```

This always leaves `first` available, even when it has no later source use.
That distinction makes `copy` useful in tests, copy-constructor-sensitive code,
and APIs whose contract explicitly requires duplication. Ordinary code should
normally omit it and let liveness choose the cheapest correct operation.

## Calls, returns, and temporaries

A by-value parameter receives its own value. The caller's argument is copied
when the caller needs it afterward and moved otherwise. A `ref T` or `const ref
T` parameter borrows and never consumes its argument.

A borrow remains live through the complete call. If one argument borrows a
local and another by-value argument refers to the same local, the by-value
argument is copied rather than moved, regardless of argument order. The callee
therefore never observes a borrow into an emptied source slot.

Call ownership transfer is transactional. During left-to-right argument
evaluation, every prepared owning argument remains caller-owned and registered
for exceptional cleanup. Only after every argument succeeds does a non-throwing
commit move those values into the callee parameters. An exception in a later
argument therefore destroys every earlier prepared value exactly once.

Returning a local is normally its last use and therefore moves. Fresh
expressions are constructed directly in their destination where possible.
Returning an immutable-reference parameter must copy because borrowed storage
cannot be consumed.

## Fields and containers

A direct field of an owned struct local follows the same liveness rule. If the
owner is dead afterward, the field is moved out, the remaining fields are
cleaned up, and the owner becomes empty. If the owner remains live, the field
is copied instead.

```aster
Envelope envelope = MakeEnvelope();
Html body = envelope.body; // copies because envelope is used below
Use(envelope);

Html finalBody = envelope.body; // moves on the final use
```

An index read cannot remove an element while preserving its container shape.
It therefore copies a copyable non-trivial element. A noncopyable stored value
must be accessed through a borrowing or consuming collection API instead.

Collection callbacks whose parameter is passed by value follow the same rule.
`List<T>.ForEach`, predicate methods, and `FindAll` pass each retained list
element through its semantic copy operation, including custom copy
constructors nested inside aggregates. They reject noncopyable element types;
the collection keeps ownership of its elements throughout the callback.
If copying or invoking a callback throws, iteration stops before the next
element. Partial `FindAll` results are destroyed, while a partially completed
`RemoveAll` retains a valid list containing the unprocessed suffix.

## Classes and unsafe code

Class values are non-owning references with explicit `delete`; assignment
copies the reference and does not clone the object. Raw pointers likewise copy
their address. Neither operation proves pointed-to lifetime or duplicates the
allocation.

## Performance contract

For a copyable non-trivial local or direct owned field, Aster emits a move at
last use. That path does not allocate, retain storage, recursively copy
elements, or invoke a copy constructor. When the source must remain usable,
the compiler emits the type's real semantic copy; it never substitutes a raw
bit copy for a custom or recursive copy policy.

The unresolved transfer exists only during lowering. Mandatory CFG liveness
rewrites it into explicit typed-IR move or copy operations before verification
and before either the VM or C backend sees the program.
