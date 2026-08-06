# Typed IR

Aster's typed IR is the shared semantic input for the bytecode and C
backends. It is a control-flow graph rather than a stack program. Generated C
is the primary consumer and bytecode is the development consumer; both receive
the same verified semantics.

The current foundation represents:

- concrete checked types and typed virtual values;
- explicit local load, store, move, clone, drop, and discard operations;
- checked integer arithmetic and distinct floating-point arithmetic;
- direct Aster calls, indirect calls, and native calls;
- parameter descriptors with names, types, modes, and source spans;
- function ABI, async-suspension, and render metadata;
- closed copy/drop policies and verified copy/destructor function IDs;
- native-call descriptors with operand types, modes, ABI, and result type;
- basic blocks with jump, branch, return, exception propagation, and trap terminators;
- source spans and host target metadata;
- monomorphized functions as independent IR functions;
- arrays, structs, and enum/union construction;
- explicit local-place and temporary field/index operations, including
  non-owning projection operations used as custom-copy sources;
- enum/union tests and consuming union payload extraction;
- `switch` and `try` lowered to ordinary control-flow edges;
- owning iterator state for fixed arrays and vectors;
- typed raw allocation, load, and store operations;
- owning element builders, typed properties, children, and component calls.

Every block must have one terminator. Every operand must reference a defined
typed value, every local and branch target must exist, and every result must
agree with the function value table. `lang_ir_verify_module` checks these
structural invariants before a backend may consume the module.

`local_drop` means: destroy the owning value in the local slot if that slot is
still initialized. `local_move` transfers the value and leaves the source slot
empty. This makes cleanup paths explicit without destroying moved values.
`value_clone` also records whether its input is a borrowed local view or a
fresh owning temporary. Cloning the latter consumes and destroys the original
temporary after producing the clone; cloning a local view leaves its owner
unchanged.

Struct construction retains source evaluation order. Each operand carries its
resolved declaration-field index, allowing a backend to place fields in target
layout order without reordering initializer side effects. Local field and index
operations are distinct from temporary aggregate operations: the former mutate
or inspect a stable local place, while the latter consume a temporary aggregate.

`switch` stores its subject in one owning local, tests variants without
consuming that local, and consumes it exactly once on the selected arm. Payload
bindings use `local_enum_payload_move`; payload-free arms drop the complete
union value.

Unchecked exceptions are explicit in IR: calls are followed by a pending test,
exceptional edges execute their cleanup plan, and then jump to the active catch
block or propagate from the function. `exception_set`, `exception_pending`, and
`exception_take` carry the base `Exception` value without changing ordinary
function signatures. Active `finally` bodies are emitted on normal and abrupt
control-flow edges. Exceptional completion of a `finally` replaces the pending
exception, matching the source semantics.

The legacy Result `try` expression has no hidden unwinder in the IR. It branches on `Result.Ok`. The success
block moves out the success payload and continues evaluation. The error block
moves out the error payload, constructs the current function's concrete
`Result.Err`, executes ownership cleanup in reverse declaration order, and
returns it. Function exits walk cleanup-requiring IR locals in reverse order.
A drop checks whether its slot is still initialized, so moved and
branch-inactive locals are harmless while compiler-created owners such as
iterators cannot be missed.

Ordinary `for` consumes an iterable value into an explicit iterator local.
Array iteration receives a copied array value; cleanup-managed vectors transfer the
source automatically after checker validation. The loop CFG tests `has_next`,
moves the next item into the ordinary iteration binding, and drops the owning
iterator on exhaustion or `break`.

Non-consuming `foreach` emits `borrowed_iterator_begin` against its source
local. The iterator itself never owns or frees collection storage. Each
ordinary value binding is then constructed from the borrowed element through
the element type's copy policy; `ref` iteration remains an alias. The VM and C
iterator representations carry this ownership bit explicitly.

Custom-copy projections use explicit borrow operations for local fields,
array elements, list elements, queue fronts, stack tops, dictionary values,
and tagged-union payloads. The borrowed virtual value is not cleanup-owning;
the following recursive-copy sequence constructs the result before the source
aggregate can be discarded.

Half-open integer ranges lower differently: both bounds are stored once in
scalar locals, the counter is compared in the loop header, and the counter is
advanced with the ordinary checked integer-add operation before entering the
source body. They allocate no iterator object. Consequently bytecode and C
share the same range CFG and overflow semantics.

Raw-pointer operations retain their element type and mutability in the IR.
`raw_load` requires a pointer whose element type matches the result.
`raw_store` additionally requires a mutable pointer and a matching stored
value. These checks do not prove pointer validity or lifetime.

Native elements lower to an owning `element-builder` local. `element_begin`
records the resolved element symbol, `local_element_property` consumes a
statically checked property value, `local_element_append` consumes a valid
child or child collection, and `local_element_finish` consumes the builder to
produce `Html`. A directly nested `element_begin` also records its parent
builder local, allowing a backend to write the child into the parent's output
destination without changing the first-class `Html` result type.

Direct interpolation is segmented in the IR rather than first becoming a
`string`. Attributes use `local_element_property_begin`, one
`local_element_property_append` per literal or formatted value, and
`local_element_property_end`. Text uses `local_element_append_formatted` per
segment. The operand type determines scalar formatting, while the operation
determines text or attribute escaping. Interpolation in an ordinary value
position lowers through an explicit `StringBuilder` and returns owned
`string`.

Function components lower to normal `call_direct` instructions, with their
body collected into a `#fragment` builder for the typed `children` parameter.
When a component's returned root is a direct element with no early return
through that construction, the function and call carry explicit render-root
and render-destination metadata. Backends may use that proof to render the
component into its parent; an ordinary call retains the normal function ABI.

The builder stays active while the ordinary statement lowering visits an
element body. Consequently `if`, `for`, and `switch` retain the same AST nodes
and become the same branch, iterator, and enum-test CFG operations used
outside elements. A loop exit drops only builders created inside the exited
loop; an outer builder collecting loop output remains live.

The IR lowers scalar expressions, calls, local ownership operations, arrays,
structs, enum/union construction and switching, aggregate mutation, `Result`/`try`,
`if`, `while`, `for`, loop exits, raw-pointer primitives, native elements,
components, returns, and checker-produced cleanup plans. This is the sole
source-to-bytecode lowering path.

Use:

```sh
lang dump-ir examples/arithmetic.as
lang dump-ir-bytecode examples/arithmetic.as
lang run-ir examples/arithmetic.as
lang project run-ir examples/docs_server/aster.toml render
```

`run` and its explicit `run-ir` alias lower verified typed IR into the existing
bytecode container and then use the existing VM.
Every IR virtual value receives a temporary VM local, which leaves the operand
stack empty at basic-block boundaries and makes arbitrary CFG edges simple to
patch. Operands move out of those temporary slots when consumed.

Project targets use the same adapter through `project run` and `project test`.
`project run-ir` is an explicit alias. Backend tests compare complete
multi-module applications between the typed-IR VM and generated C.

The current adapter accepts scalar operations, fixed arrays, structs, plain
enums, and discriminated unions:
constants, local load/move/store/drop, checked integer and floating arithmetic,
comparisons, branches, direct/indirect/native calls, function values, returns,
casts, traps, aggregate construction and cloning, and field/index reads and
mutation. Struct IR types retain declaration-order field names; constructor
operands retain source evaluation order and map each operand to its resolved
field index. This lets the adapter generate the VM metadata without inspecting
the AST or checker types.

Enum and union IR types retain declaration-order member names. Construction
combines the resolved family and member; tag tests are non-consuming, and union
payload extraction moves the complete union local exactly once. Recursive copy
lowering instead uses a non-owning payload projection, branches on the active
tag, copies that payload, and reconstructs the same variant without changing
the source. `Result` and `Option` use the union representation. `try` is already ordinary IR CFG, so
the adapter needs no privileged try opcode: its error block moves the error,
reconstructs `Err`, performs explicit reverse cleanup, and returns.

Recursive dynamic-collection copy is also explicit control flow. Sequence
collections use a borrowed iterator and rebuild a fresh destination one element
at a time. Dictionary lowering borrows each key and value by logical index,
applies their independent copy policies, and inserts them into a fresh table so
the destination recomputes hashes and bucket placement.

Owning iterator locals lower to dedicated VM-local operations: initialization
consumes the iterable, `has_next` is non-consuming, and `next` moves one item
out of iterator storage. Both arrays and vectors therefore use the same IR
contract. Exhaustion and `break` converge on explicit iterator cleanup blocks;
cleanup-managed item locals are cleaned on normal iteration, `continue`, and
`break`.

Raw allocation, load, and store lower from their typed IR operations to the
interpreter's tracked arena-pointer primitives. The IR verifier has already
checked pointer element type and mutable-store permission. Allocation borrows
the arena owner; reset and destruction invalidate tracked allocations, so an
expired access traps in the interpreter with the same source location as the
original execution path. Null remains an ordinary typed raw-pointer constant.

Element builders lower to verified VM-local operations. Begin creates an
owning builder, property and append instructions mutate that known local, and
finish consumes it to produce `Html`. Append accepts typed scalar children and
recursive `Option`, array, or `List` child collections. The VM uses the same
escaping and ownership-aware child consumption as the original bytecode path.
Components remain ordinary calls, while their collected children use a
fragment builder. Live HTTP integration tests exercise both compiler paths,
including routing, keep-alive, static files, HTML, and socket cleanup.

During lowering, the type table temporarily retains a borrowed link to the
checker's canonical type object and functions retain source declarations.
Lowering clears both links before returning the completed module. The verifier
rejects a module that still contains either link, and backend-boundary tests
compile bytecode and generated C with those links absent.

Each concrete IR type also records its target size and alignment when the
target layout is known. Struct and union types record a verified destructor
function ID when they have a language destructor. Cleanup therefore reaches a
backend as two explicit facts:

- control-flow edges contain `local_drop` instructions in deterministic order;
- the dropped type identifies its concrete destructor without name lookup.

The verifier also checks copy/drop policy consistency, parameter descriptors,
native-call operand signatures, aggregate discriminants, function ABI flags,
and the recorded number of async suspension points.

Backends remain responsible for representing whether an owning local is live.
The C backend uses a local boolean. A move must clear the source state, and a
drop must test and clear it exactly once.
