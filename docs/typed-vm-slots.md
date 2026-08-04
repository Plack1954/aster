# Typed VM Slot Design

## Decision

Keep `LangValue` as the public FFI and general runtime-value representation.
Introduce a separate 16-byte `VmSlot` representation for bytecode locals and
operand stacks only after every bytecode function carries static slot-kind
metadata.

## Context

`LangValue` is currently 24 bytes on a 64-bit target:

```text
tag                         8 bytes including alignment
largest payload            16 bytes (string view or byte slice)
total                      24 bytes
```

This is appropriate at dynamic boundaries, but wasteful for statically typed
`i32`, `i64`, `u64`, `f64`, `bool`, pointers, and function values. Their type
is already known by the checker, typed IR, bytecode instruction, and local
slot.

Removing or compressing the public tag would complicate native callbacks and
would make string/slice representation fragile. The optimization boundary
therefore belongs inside the VM.

## Representation

```c
typedef struct VmSlot {
    uint64_t low;
    uint64_t high;
} VmSlot;

typedef enum VmSlotKind {
    VM_SLOT_UNIT,
    VM_SLOT_BOOL,
    VM_SLOT_SIGNED,
    VM_SLOT_UNSIGNED,
    VM_SLOT_FLOAT,
    VM_SLOT_STRING_VIEW,
    VM_SLOT_BYTE_SLICE,
    VM_SLOT_OBJECT,
    VM_SLOT_RAW_POINTER,
    VM_SLOT_FUNCTION
} VmSlotKind;
```

`VmSlotKind` is stored once in `BytecodeFunction.local_kinds`, not once per
runtime slot. `frame_initialized` remains a separate runtime bitset because
move state is dynamic.

Payload rules:

- signed/unsigned integers and floats use `low`;
- bool uses zero or one in `low`;
- object, raw pointer, and function identity use `low`;
- string views and byte slices use pointer in `low`, length in `high`;
- unit has no payload.

No pointer tagging is used.

## Metadata generation

The typed-IR bytecode backend already has:

- `IrFunction.locals[l].type` for declared locals;
- `IrFunction.value_types[v]` for temporary values;
- `IrModule.types[type]` for the concrete type shape.

During `lower_function`, it can allocate `local_kinds` parallel to
`local_destructors`:

```text
slot 0 .. local_count-1             from IrLocal.type
slot local_count .. end             from value_types
```

The direct AST bytecode backend must either generate equivalent metadata or
continue using `LangValue` until it is retired. A bytecode module must not mix
slot representations within one function.

The verifier checks:

- every local has a valid slot kind;
- typed opcodes agree with source and destination slot kinds;
- object-only operations target object slots;
- calls agree with callee parameter and result kinds.

## Runtime boundaries

Conversion between `LangValue` and `VmSlot` occurs only at:

- entry-function arguments and result;
- native C calls;
- public VM API calls;
- constants loaded from the module constant pool;
- object aggregate fields while aggregates still store `LangValue`.

Aster-to-Aster calls copy `VmSlot` directly. Scalar calls therefore avoid
tag copies and conversion entirely.

## Ownership

The representation does not alter ownership rules.

- `frame_initialized` remains the authority for Available/Moved state.
- only `VM_SLOT_OBJECT` can invoke object destruction;
- moving a slot copies 16 payload bytes and clears its initialized bit;
- copying is allowed only when semantic checking emitted a copy operation;
- string views, raw pointers, and function values remain non-owning/copyable;
- destructor lookup remains attached to bytecode local metadata and objects.

The VM must never infer ownership from payload bits.

## Migration sequence

1. Emit and verify `local_kinds` while retaining `LangValue` storage.
2. Add conversion helpers and differential tests for every slot kind.
3. Switch frame locals to `VmSlot`, leaving the operand stack as `LangValue`.
4. Benchmark scalar loops and calls.
5. Switch the operand stack only if it provides an additional measured win.
6. Consider storing aggregate scalar fields in typed storage separately.

Step 1 is deliberately useful on its own: it makes bytecode self-describing
enough for a future native backend and catches compiler bugs.

## Required tests

- every primitive width and signedness;
- float NaN and negative zero preservation;
- string and mutable-slice pointer/length round trips;
- raw pointers and function values;
- direct, indirect, and native calls;
- cleanup-managed objects and destructor order;
- `try` propagation and trap unwinding;
- malformed kind metadata rejection;
- direct-bytecode versus typed-IR differential tests;
- normal, ASan, and UBSan builds.

## Expected effect

Local frame storage falls from 24 to 16 bytes per slot, a 33% reduction.
Scalar instructions stop loading and writing runtime tags. The likely benefit
is larger in call-heavy and local-heavy programs than in the current compact
loop, where dispatch still dominates.

This is a structural runtime change. It should be implemented behind one
representation boundary and accepted only after representative benchmarks,
not estimated from memory reduction alone.
