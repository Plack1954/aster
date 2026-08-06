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
lang emit-c app.as > app.c
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

Typed IR is the intended complete semantic boundary before any backend runs:

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

Process spawning follows this split: the POSIX fork/exec, pipe, wait, and
signal mechanisms live in `process_native.c`, while `ProcessStartInfo`, list
iteration, validation, and the `System.Diagnostics.Process` facade are Aster
library code. Arguments are staged individually and never flattened into an
implicit shell command.

Native `System.Net.Http` follows that boundary. Its Aster request/response and
owned-content API is transport-facing library code; the optional
`http_client_curl.c` component delegates HTTP/TLS protocol machinery to
libcurl. Synchronous calls use easy handles; asynchronous calls share a multi
handle and advance it through nonblocking executor-timer polls, allowing
concurrent transfers, cancellation, and connection reuse in both native
backends. Streaming responses pause libcurl when their bounded native queue is
full and resume as the Aster caller drains it into borrowed spans. Streaming
uploads invert that flow: Aster fills a bounded queue from borrowed spans and
libcurl pauses whenever the producer has not supplied the next chunk.
`ASTER_ENABLE_CURL=OFF` retains the compiler and core runtime with
typed unavailable stubs. Browser/Wasm does not use this component and will use
Fetch.

`System.Security.Cryptography` uses the same optional-component boundary.
Random bytes come directly from the operating system (`getrandom`, system CNG,
or the platform secure random facility); SHA-256, HMAC-SHA256, and the native
constant-time comparison delegate to OpenSSL when enabled. The compiler and
core runtime contain no cryptographic algorithms and do not link OpenSSL.

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

The IR owns parameter descriptors (name, type, mode, and span), function
ABI/async/render metadata, aggregate member metadata, native-call signatures,
and closed copy/drop policies. Checked-type and source-declaration links exist
only while lowering and are cleared before verification. The bytecode and C
backends therefore cannot recover semantics from frontend objects.

Copy policy is one of trivial, deep, shared retain, noncopyable, or custom.
Drop policy is trivial, recursively generated, or custom. A language
destructor is a verified function ID, not a backend name lookup.

Aggregate metadata is backend-facing: every struct field has a name, source
span, and canonical resolved `IrTypeId`; every enum or union member has a name,
span, stable discriminant, and either a canonical payload `IrTypeId` or the
explicit payloadless marker. This includes concrete generic substitution. The
verifier checks the tables and struct construction operands against them.

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
