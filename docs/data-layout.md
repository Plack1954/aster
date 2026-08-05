# Target data layout

Aster 0.2 makes target layout explicit before adding a native backend. The
host target description records:

- pointer size and alignment
- byte order
- enum/union tag size and alignment
- whether the host satisfies the currently supported C ABI assumptions

Primitive integer widths are defined by their language names. `nint` and
`nuint` follow the target pointer width. Raw pointers and opaque owning runtime
handles occupy one pointer; `string` and slices are pointer-plus-length values.
`List<T>` is one such pointer-sized owning handle; its data pointer, length,
capacity, element stride, and allocation policy are private runtime state.
Function values occupy two pointer-sized ABI slots: an invocation target and
an optional borrowed receiver. An unbound function leaves the receiver empty.
A bound class-method delegate stores the class reference without retaining or
owning it. This is an internal language ABI, not an extern C function-pointer
ABI.

Every class object begins with a compiler-owned runtime type identifier.
Generated C uses it for virtual dispatch, virtual method delegate binding, and
destruction through a base reference. The bytecode VM carries equivalent class
metadata. The identifier does not provide reachability, reference counts, or
automatic lifetime management.
Interface values use the same one-pointer representation as class references.
They do not allocate interface boxes or embed per-object interface tables.
Whole-program dispatch metadata maps an interface slot and runtime type ID to
the implementing function.

Static fields are not members of aggregate or class-object layout. Generated C
emits a separate internal static object for each field, while bytecode assigns
each field a module-slot index.

Normal structs use declaration order with conventional alignment padding.
Fixed arrays retain their length. Plain enums use a 32-bit declaration-order
integer. A discriminated union uses a 32-bit tag followed by aligned
maximum-size payload storage. This representation is inspectable. Normal
aggregate ABI remains provisional.

`extern struct` marks a declaration intended for direct C layout:

```text
extern struct CPoint {
    x: float,
    y: float,
}
```

Its fields must recursively use C-ABI scalar types, raw pointers, fixed arrays,
aliases of compatible types, plain enums, or other `extern struct`
declarations. Managed runtime values such as `string`, `List<T>`, `Html`,
slices, and discriminated unions are rejected. This validates Aster's side
of the layout contract; an
embedding library should still use C static assertions against the actual C
declaration it wraps.

Use:

```sh
lang dump-layout file.as
```

The output includes target properties, aggregate sizes and alignments, struct
field offsets, enum values, and union tag/payload information. Recursive inline
aggregates
produce an invalid layout rather than overflowing layout computation. Generic
declarations are listed as templates; every concrete instantiation reached
during checking is listed separately with its substituted field offsets and
tagged payload layout. Generic `extern struct` declarations are rejected until
a cross-language generic ABI is defined.
