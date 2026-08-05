# Aster backend architecture

Aster is a deterministic application language with explicit costs, simple
ownership, first-class typed HTML, excellent generated C, and a fast bytecode
VM development loop. The full design and feature gate are recorded in
[`docs/thesis.md`](docs/thesis.md).

The implemented architecture is:

```text
Aster source
    ↓
typed AST
    ↓
verified Aster typed IR
    ├──→ portable C17       primary deployment backend
    └──→ bytecode VM        development and differential path
```

The backends do not have equal scheduling priority.

## Backend roles

### Portable C17

Generated C is Aster's main backend. It provides the release path,
portability baseline, performance baseline, inspectable output, and integration
boundary with operating-system facilities and established C libraries.

```sh
lang emit-c app.lang > app.c
lang project emit-c aster.toml app > app.c
```

GCC or Clang remains responsible for machine optimization, register
allocation, object formats, debug information, and platform coverage. Aster
remains responsible for preserving its type, trap, and ownership semantics in
the emitted program.

C-backend gaps exposed by substantial applications take priority. See
[`docs/c-backend.md`](docs/c-backend.md).

### Bytecode VM

The VM provides the edit-run-test loop, source-aware diagnostics, embedding
path, and an independent execution path for the typed IR. Normal `run` and
project execution lower typed IR to verified bytecode.

VM performance work is measurement-driven. Relevant experiments include
richer typed instructions, less adapter traffic and value copying,
register-oriented bytecode, and portable switch versus optional threaded
dispatch. None of these experiments may change source semantics or introduce
implicit ownership behavior.

## Semantic boundary

Typed IR must express Aster semantics completely before any backend runs:

- typed virtual values and concrete monomorphized types;
- basic blocks and explicit branches;
- direct, indirect, and native calls;
- explicit moves, clones, discards, and destructor calls;
- cleanup edges for return, loops, `try`, and traps;
- checked, wrapping, and unchecked arithmetic as distinct operations;
- aggregate construction, field/index access, and target layout;
- raw-pointer operations and source locations;
- no implicit ownership behavior left for a backend to guess.

For example:

```text
block entry:
    %buffer = call Buffer.allocate(1024)
    %result = call consume(move %buffer)
    drop %result
    return 0
```

By backend lowering, the checker and IR construction have already decided
whether `%buffer` is alive. A backend represents that decision; it does not
infer or revise it.

The runtime boundary stays small and pragmatic: allocation, file/socket/process
mechanisms, trap reporting, established libraries, and backend storage
mechanisms may remain in C. Application policy and reusable APIs should move
into Aster when real programs show that doing so improves the design.

Differential coverage compares generated C and VM output, cleanup, traps, and
exit status.

## Typed IR foundation

The first Aster IR is a control-flow graph with typed virtual values and
explicit local storage. It is intentionally not tied to the current stack VM,
C syntax or the VM stack model.

The initial invariants are:

- every value has one concrete checked type;
- every basic block ends in exactly one terminator;
- branch targets and value operands are verified before backend lowering;
- checked integer operations remain distinct from floating-point operations;
- local copy, move, clone, store, discard, and drop are explicit;
- calls distinguish direct Aster, indirect function-value, and native calls;
- cleanup plans from the checker become ordinary IR drop instructions;
- every instruction and terminator retains its Aster source span;
- monomorphized functions enter the IR as independent concrete functions;
- target pointer width, alignment, enum-tag layout, and endianness belong to
  the IR module.
- concrete types carry target size/alignment and a verified destructor
  function ID where applicable.

The bootstrap IR may temporarily retain a borrowed link to the checked type
object while its owned backend-facing type table is expanded. Backends must
consume the IR, never inspect or reinterpret the raw AST.

Aggregate metadata is now backend-facing: every struct field has a canonical
resolved `IrTypeId`, and every enum or union member has either a canonical
payload `IrTypeId` or the explicit payloadless marker. This includes concrete
generic substitution. The verifier checks the tables and struct construction
operands against them. C layout emission consumes the same table rather than
rebuilding aggregate types from declarations.

The portable C representation currently uses generated wrapper structs for
fixed arrays, declaration-order fields for structs, declaration-order
`uint32_t` values for plain enums, and a `uint32_t` tag plus inline payload for
discriminated unions. Union layout is backend-internal; plain enums retain a
predictable integer representation.

Source and project execution always lower through verified typed IR before
producing VM bytecode or portable C. There is no parallel AST-to-bytecode
compiler.

The foundation now exists in `src/ir.c`. It lowers scalar code, calls, explicit
local ownership operations, cleanup plans, arrays, structs, enum/union
construction,
aggregate reads and mutation, `if`, `while`, loop exits, and returns. Struct
operands retain source evaluation order plus resolved field indices.
Union `switch` and `Result.try` are ordinary CFG branches with explicit consuming
payload operations; `try` error edges construct the caller's concrete `Err`,
drop exited owners, and return. `lang dump-ir` runs the structural and typed
verifier and prints the result. Iteration uses owning iterator locals, and raw
pointer access uses typed unsafe operations. Native elements use owning builder
locals with explicit property, child, and finish operations. Function
components become ordinary direct calls, while `if`, `for`, and `switch` inside
element bodies remain the same CFG operations used elsewhere. The typed-AST
coverage foundation feeds the default IR-to-bytecode adapter. It executes
constants, locals, arithmetic, CFG, direct and indirect
calls, native calls, casts, arrays, structs, aggregate cloning and mutation,
enums/unions, `switch`, `Result`/`try`, owning array/vector iterators, raw
pointers,
native element builders, components, cleanup, and returns through the existing
VM. Manifest targets and project tests use the same path. Module identity,
generic targets, the documentation-server programs, and live HTTP servers are
checked across the typed-IR VM and generated-C backends.
