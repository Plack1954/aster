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
Function values occupy one pointer-sized ABI slot in the target model. The
bytecode VM stores a function-table index in that slot; this is not yet an
extern C function-pointer ABI.

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
