# Architecture decisions

## Compact modules before surface completeness

Decision: implement a working vertical slice in six C modules.

Context: an empty repository and a broad two-version language design.

Alternatives: create the full requested file tree as placeholders.

Reason: executable semantics and tests expose design mistakes earlier.

Consequences: deliberately deferred post-Version-1 features remain explicitly
unsupported, while the lexer-to-VM and typed-element paths are real.

## Typed AST

Decision: annotate the parsed AST with resolved `Type *` values.

Context: bytecode must not be emitted directly from unresolved syntax.

Alternatives: duplicate the tree into a separate HIR.

Reason: a typed AST is smaller for the first version and preserves a clean
future backend boundary.

Consequences: ownership transitions are checked during AST traversal; function
targets retain their defining declarations and every parameter, local, loop
binding, and switch binding receives a unique checker-assigned identity. Reads,
moves, and direct aggregate places retain that identity, so bytecode generation
does not repeat name lookup or confuse shadowed bindings. The checker also
attaches reverse-order binding lists to normal block exits, `return`, `break`,
`continue`, and the error edge of `try`. These cleanup plans make the
destruction control flow explicit for later backends; the bytecode backend
consumes them when lowering lexical and explicit statement exits. Explicit
move/clone/drop operations appear in emitted bytecode.

## Source-declared element metadata

Decision: resolve `element name -> Result { ... }` declarations through the
normal module symbol table.

Context: element names must not be parser keywords or strings interpreted at
runtime, and user-defined schemas must not require editing the checker.

Alternatives: retain a C table of standard HTML schemas or treat components as
untyped runtime constructors.

Reason: declarations make required, optional, and child properties ordinary
typed module metadata while leaving the parser backend-neutral.

Consequences: `std/html.as` defines the standard tag vocabulary,
`Option<T>` properties are optional, `children: Html` permits bodies, and
custom typed tags work without C changes. The checker owns only a narrow
universal-attribute contract; tag-specific attributes remain source-declared.

## Make ordinary element content native HTML text

Decision: parse bare element content as static HTML text. Reserve `<` for nested
markup and `{...}` for dynamic Aster expressions. Keep recognized Aster
statement forms as code at child boundaries.

Context: requiring `"..."` around every fixed sentence made native HTML look
like a tree of Aster string expressions and obscured the language's primary UI
advantage. Static text must receive the same safe escaping guarantee as dynamic
text.

Alternatives: retain mandatory string literals, require a heredoc-like text
form, treat all element bodies as an unrelated template language, or guess
whether statement-shaped content is prose.

Reason: HTML already has an unambiguous structural boundary. Static text can
lower directly into the destination buffer without allocation and still use
context-aware escaping. Code keeps priority when it has the expected control-
flow shape; deliberately code-shaped prose can use an explicit braced string
expression.

Consequences: indentation-only text disappears, normal source whitespace is
normalized, and both static and dynamic text escape by destination. Tag-shaped
literal `<` and literal `{` use an explicit braced string expression. Quoted
child expressions require braces; the former unbraced quoted-child grammar is
not retained.

## Preserve native HTML attribute spelling and semantics

Decision: allow dashed and keyword property names inside element openings,
canonicalize dash/underscore only for schema lookup, and preserve the source
name for rendering. Treat an `Option<T>` attribute value as conditional
omission and a `bool` attribute as HTML presence.

Context: real HTML uses names such as `aria-label`, `http-equiv`, `for`, and
`type`. Rendering `disabled="false"` is semantically wrong, and forcing
applications to branch around every optional attribute turns native markup
back into a string-building API.

Alternatives: quote property names, rename them to Aster-safe identifiers,
render all scalar values as quoted text, or build a complete HTML schema into
the compiler.

Reason: markup names occupy an unambiguous syntactic position. The typed IR
already records the source name and operand type, so both primary backends can
implement omission, presence, escaping, and numeric formatting without hidden
allocation or dynamic dispatch.

Consequences: `data-*` and `aria-*` accept strings universally; a small set of
genuine global text, boolean, and numeric attributes is available on every
declared element; other attributes remain checked against `std/html.as`.
The VM and generated C produce the same byte output.

## Recursive namespace dependencies

Decision: map project namespace paths deterministically to snake_case file
paths: `App.Name` maps to `app/name.as`. Public `System.*` and `Aster.*`
standard-library namespaces resolve through an explicit compiler table, so
their source-tree filenames are not part of the language API.

Context: Version 1 needs useful dependencies without a package manager.

Alternatives: repository-root namespaces or textual inclusion.

Reason: adjacent files are predictable for a small implementation, while
declarations still enter normal resolution rather than preprocessing headers.

Consequences: dependencies are deduplicated, cycles are rejected, and private
declarations stay namespace-local. Whole-namespace `using` declarations and
namespace aliases are supported; selective item imports are not.

## Blocking HTTP before callbacks

Decision: begin with `HttpServeOnce(borrow server, body)`.

Context: request callbacks and reusable language handlers require function
values and a VM re-entry contract.

Alternatives: hide a complete event loop inside one native call.

Reason: serve-once proves socket ownership, bounded protocol parsing, borrowed
handles, HTML output, and cleanup while preserving an incremental route to
request objects and handlers.

Consequences: Version A is deliberately single-threaded, blocking, fixed-body,
and one connection per call.

## Language-owned HTTP routing

Decision: expose accept/request/respond primitives, while keeping routing and
the reusable server loop in ordinary Aster functions.

Context: later HTTP milestones need method, path, headers, routing, and
handlers without introducing a web-specific sublanguage.

Alternatives: register routes through native string tables, or add VM callback
function values before they are otherwise needed.

Reason: normal functions, string comparisons, `if`, `while`, moves, and typed
HTML already express the required behavior and exercise the intended language
model directly.

Consequences: handlers initially worked as exact non-capturing function
values, and bounded sequential keep-alive remains in the ordinary Aster server
loop. Bound class-method delegates now add an explicit borrowed receiver;
arbitrary captures, concurrency, and broader production protocol features
remain future work.

## Destructor identity belongs to runtime values

Decision: store the resolved language destructor on each owned VM aggregate.

Context: local-slot cleanup alone misses custom-drop values nested inside
structs, arrays, enum payloads, or discarded as temporaries.

Alternatives: attach cleanup only to local bytecode slots, or prohibit nesting
types with custom destructors.

Reason: ownership moves transfer runtime values, so attaching destructor
identity to the value naturally preserves exactly-once cleanup across moves and
recursive aggregates.

Consequences: structural VM destruction can invoke language bodies during
ordinary exits, `try`, and trap unwinding. Direct-local field/index inspection
borrows the aggregate and copies only the selected copyable value.

## Module-qualified semantic identity

Decision: retain the defining declaration on named types and resolved typed-AST
expressions, and qualify runtime destructor metadata with its module.

Context: flattened import storage made same-spelled functions and types
vulnerable to load-order selection.

Alternatives: prohibit duplicate short names across the entire import graph or
re-run name resolution during bytecode generation.

Reason: declaration identity is the semantic fact a future backend needs.

Consequences: locals shadow imports, ambiguous public imports are diagnosed,
same-spelled module types remain distinct, and the compiler consumes resolved
call/component targets from the typed AST. Clone eligibility also follows the
exact named declaration, so a same-spelled destructor or field layout in
another imported module cannot change a local type's ownership behavior.

## Direct aggregate places

Decision: support field and fixed-array replacement only when rooted at a
mutable direct local.

Context: Version 1 needs predictable aggregate mutation without partial moves,
general references, or borrow analysis.

Alternatives: clone aggregates for reads and write back copies, or implement
general lvalue/reference semantics.

Reason: direct local slots provide an explicit, deterministic ownership
boundary.

Consequences: replacing an owning slot drops its old value; reads borrow the
parent and copy only a copyable selected value. Nested-place assignment remains
future work.

## Parenthesized complex properties

Decision: bare `/` and `>` terminate an element property expression.

Context: they also delimit self-closing and opening tags.

Alternatives: layout-sensitive parsing or mandatory commas.

Reason: deterministic token parsing and familiar compact markup.

Consequences: division/comparison property expressions must be parenthesized.

## Compiler-known minimum containers

Decision: implement `Option<T>`, `Result<T, E>`, `Span<T>`, and `List<T>` as
isolated compiler-known type constructors in Version 1.

Context: the required standard surface needs typed containers before general
user-defined generic declarations are mature.

Alternatives: erase element types at runtime, or delay containers until a
complete generic language feature exists.

Reason: structural element typing and monomorphic runtime values preserve the
intended ownership model without adding traits or runtime type erasure.

Consequences at the time: nested type arguments worked and `List<T>` ownership
was enforced before arbitrary declarations existed. Aster 0.2's later generic
aggregate/function decisions supersede that limitation while retaining these
compiler-known storage constructors.

## Clone bytecode records operand ownership

Decision: the clone instruction records whether its input is borrowed from a
local or is an owned temporary.

Context: ordinary expression lowering materializes a copy of copyable
aggregate locals. Reusing that path for `clone local` produced two deep clones
and abandoned the intermediate object.

Alternatives: restrict cloning to locals, add a general runtime reference value,
or have the VM guess operand ownership.

Reason: one explicit operand bit preserves the small stack-machine model and
defines destruction for both operand forms.

Consequences: cloning a local leaves it available. Cloning an expression
consumes its temporary after constructing the clone. The verifier rejects
unknown ownership encodings.

## Project mode uses strict namespace identity

Decision: manifest projects map every complete namespace path through one source
root and require the source declaration to match that path.

Context: Version 0 resolved imported files relative to the importer and kept
compatibility with several fixtures whose filename and declared namespace differ.
That behavior is unsuitable for packages, canonical generic instantiation, or
reproducible builds.

Alternatives: search several implicit roots, preserve suffix-only resolution,
or require callers to enumerate every source file.

Reason: one deterministic mapping makes the dependency graph explainable and
gives future monomorphization a stable namespace identity.

Consequences: project using declarations expose only direct public dependencies
and support namespace aliases. Legacy single-file commands retain their
old relative lookup as a compatibility mode. The manifest intentionally has
no registry or dependency-version solver.

## Data layout precedes native code generation

Decision: define and expose a target data-layout model while bytecode remains
the only executable backend.

Context: generic ownership, C interoperability, and future native lowering all
need a common answer for pointer width, alignment, aggregate offsets, enum
representation, and endianness.

Alternatives: let the VM hide layout until a native backend exists or reuse C
layout accidentally through implementation structs.

Reason: explicit layout prevents VM implementation details from becoming an
unreviewed language ABI.

Consequences: `dump-layout` reports the current declaration-order struct and
tagged-enum model. Normal aggregate ABI remains provisional; explicit C-layout
declarations and validation are still required before claiming stable C ABI
compatibility.

## Generic aggregates are canonical monomorphic types

Decision: instantiate user-defined generic structs and enums once per exact
declaration and structural argument list.

Context: modules must agree on type identity, while ownership and destruction
depend on the concrete field types substituted for each parameter.

Alternatives: runtime type erasure, declaration-name identity, or a full
constraint/trait system.

Reason: canonical monomorphization preserves static field and payload types,
keeps bytecode representation simple, and is directly reusable by a future
native backend.

Consequences: `Wrapper<T>` is copyable only if every substituted field is
copyable and requires destruction if any field does. Recursive expansion is
bounded and diagnosed. Construction currently infers arguments from an
expected applied type. Generic C-layout declarations, specialization, traits,
and variance are not supported.

## Generic functions specialize typed AST bodies

Decision: infer generic function arguments and clone one typed AST body for
each canonical concrete argument list.

Context: the VM is dynamically represented, but ownership cleanup and future
native lowering must not depend on whichever instantiation happened to be
checked last.

Alternatives: erase generic function bodies to one runtime implementation,
annotate one shared AST several times, or require explicit type arguments at
every call.

Reason: independent monomorphic bodies preserve resolved types, local ownership
states, destructor plans, and direct bytecode call targets.

Consequences: inference uses parameter structure and an available expected
return type. Recursive calls and calls from separate modules reuse the same
specialization. Explicit generic call arguments, constraints, specialization,
and generic FFI declarations remain unsupported.

## Function values begin without captures

Decision: represent non-capturing language functions as typed, copyable
function-table indices.

Context: routing, middleware, algorithms, and testing need callbacks, but
capturing closures add environment allocation, capture ownership, destructor
generation, and FFI lifetime questions.

Alternatives: add closures immediately, encode callbacks as string names, or
keep every call statically named.

Reason: direct function identities provide most callback composition while
remaining compatible with bytecode and a future native code-pointer
representation.

Consequences: `B(A)` signatures are checked exactly and indirect calls
have verifier/runtime guards. Imported language functions are valid values.
Extern function values, arbitrary captures, and owned closure environments are
deferred.

Evolution: class instance methods may now form a two-word delegate containing
a generated invocation target and a borrowed class receiver. This adds no heap
environment and does not retain the manually managed object. Arbitrary local
capture and struct boxing remain deferred.

## Library migration keeps mechanisms below policy

Decision: move reusable generic operations and routing policy into Aster while
retaining allocation, tagged storage, and OS access as narrow runtime
primitives.

Context: replacing every primitive at once would require allocator and ABI work
unrelated to whether Aster can express useful library APIs.

Alternatives: keep the whole standard surface compiler-known or prematurely
reimplement memory and socket mechanisms in unsafe Aster.

Reason: Aster-written Pair, Option/Result queries, List/StringBuilder
composition, typed routing, and middleware pressure-test generics, callbacks,
and cleanup-managed values without hiding host operations.

Consequences: user-facing behavior increasingly lives in `.as` modules. The
current Option/Result representation, List backing storage, HTML nodes, and OS
handles remain explicit privileged mechanisms to be reduced incrementally.

## HTTP reuse preserves one RAII connection owner

Historical decision for the first handwritten HTTP experiment: add explicit
request limits, timeouts, Content-Length body framing, response streaming, and
bounded sequential keep-alive without introducing concurrency or async syntax
in that slice. This restriction is superseded for the language roadmap by the
accepted C#-shaped async design in [`async.md`](async.md).

Context: the HTTP layer is a systems-integration pressure test, not a mandate
to design Aster's eventual concurrency runtime.

Alternatives: add async/await now, depend on unbounded buffering, split socket
ownership across request values, or claim the transport is production ready.

Reason: configurable bounds, one socket-owning handle, RAII connection cleanup,
and correct framing exercise important failure paths while preserving the
small runtime model.

Consequences: handlers, routers, and middleware are Aster code; socket parsing
and writes remain C primitives. Sequential HTTP/1.0 and HTTP/1.1 reuse is
limited by timeout and request count. Pipelining disables reuse instead of
creating hidden buffering ownership. TLS, concurrency, inbound chunking, and
binary bodies remain explicitly unsupported.

## Parsers and traversal policy stay in Aster

Decision: expose borrowed string byte views and RAII file/directory handles as
narrow primitives, while implementing configuration parsing, suffix filtering,
and traversal loops in Aster.

Context: the documentation server must pressure-test Aster-written application
logic without pretending portable directory streams or file descriptors are
language-level mechanisms.

Alternatives: parse the complete configuration in C, bake the documentation
list into source, or expose unowned host directory pointers.

Reason: byte offsets and opaque handles are small honest FFI boundaries.
Aster then owns tokenization, validation, error policy, iteration, and
resource composition.

Consequences: byte-view functions are explicitly byte-oriented rather than
Unicode scalar operations. Directory end currently arrives as a distinguished
error string from the primitive. POSIX traversal is implemented; Windows
returns a typed error until its directory adapter exists.

## HTTP application types remain Aster values

Decision: define Request, Response, Handler, Route, and Router in
`std::http_app`, and offer Result-returning wrappers around routine transport
operations.

Context: typed callbacks alone do not provide an application boundary if every
handler still receives an opaque native handle and every socket failure traps
the VM.

Alternatives: make request/response objects compiler-known, expose a native
route registry, or use exceptions for transport failures.

Reason: ordinary structs, enums, function values, and Result already express
the model and pressure-test the intended Aster-written library layer.

Consequences: Request contains borrowed views and inherits the language's
honest raw-lifetime limitation. Response uses fixed status variants so its
cleanup-managed Html payload can be consumed without partial struct moves. The C
layer still owns framing and sockets; application routing and error policy are
Aster code.

## Establish one typed control-flow IR before adding native backends

Decision: lower the checked AST into a typed control-flow graph shared by the
interpreter and C backend.

Context: direct AST-to-bytecode compilation leaves ownership and cleanup
decisions coupled to one stack VM and would force future backends to rediscover
Aster semantics independently.

Alternatives: keep separate AST-based backends, make the stack bytecode the
universal IR, or begin with a backend-shaped SSA representation.

Reason: typed virtual values, explicit ownership operations, ordinary basic
blocks, and target metadata preserve Aster semantics without committing the
front end to C syntax or a specific interpreter organization.

Consequences: the current bytecode compiler remains active during migration.
The IR verifier and `dump-ir` command make the new contract observable.
All remaining pointer, iteration, and native-element coverage must be complete
before execution is switched to an IR backend.

## Result propagation is explicit control flow

Decision: lower `try` into success and error basic blocks before any backend.

Context: the original bytecode VM propagated an error by returning directly
from its `OP_TRY` implementation and relied on frame teardown for cleanup.
That behavior cannot be left for three different backends to reproduce
implicitly.

Alternatives: retain a privileged try instruction, add exception-like
unwinding, or let each backend synthesize cleanup.

Reason: the checker already provides the exact cleanup plan. An ordinary branch
can move the success payload on one edge and reconstruct the caller's typed
`Err`, drop exited owners, and return on the other.

Consequences: IR backends need no exception machinery and cannot disagree
about cleanup order. The IR is somewhat larger, but all ownership-affecting
control flow is visible to verification and optimization.

## Iterators are explicit owning IR locals

Decision: lower `for` to an owning iterator local plus `has_next` and `next`
operations.

Context: arrays are copied for iteration while cleanup-managed vectors transfer their
storage. Hiding this state in a backend loop risks leaks on return, `try`,
`break`, or traps.

Alternatives: lower every iterable to index and length operations, retain one
privileged for-loop instruction, or make the source collection a borrowed
iterator.

Reason: explicit iterator state supports arrays and vectors uniformly and
makes ownership transfer observable. Function-exit cleanup includes synthetic
IR locals as well as source locals.

Consequences: backends implement a small iterator runtime representation.
Iterator slots are deterministically dropped on exhaustion and loop exit.
Future specialized array-loop optimization can remove the abstraction after
semantic lowering.

## Native elements lower through owning builders

Decision: represent element construction with explicit owning builder locals
and lower function components to ordinary direct calls.

Context: element bodies can contain the same declarations, `if`, `for`, and
`switch` statements used elsewhere. A backend-specific HTML stack would hide
ownership transfer and encourage separate template control-flow semantics.

Alternatives: keep the original bytecode HTML stack as the semantic model,
lower elements to untyped constructor calls, or introduce element-only branch
and loop operations.

Reason: a builder local makes child ownership and incomplete construction
visible while letting the existing CFG lowering handle every ordinary
statement. Resolved element and property names remain metadata on typed
operations; resolved function components use the normal call convention.

Consequences: backends need a small element-builder runtime contract.
`local_element_finish` consumes the builder, and conditional local drops make
early exits safe. Builders created inside a loop are dropped by `break` and
`continue`, while a builder surrounding that loop remains available to collect
its children. Directly nested builders may name that surrounding builder as
their render destination. A component call may do the same only when typed IR
records a direct returned element or `<>...</>` fragment root with no early
return through its construction; otherwise the ordinary owning `Html` call
path remains in force. A fragment is the same builder operation with an empty
tag contract: it participates in ownership cleanup and destination composition
but emits no wrapper bytes.

## Migrate execution through an IR-to-bytecode adapter

Decision: make the first IR backend translate verified typed IR into the
existing bytecode container and VM.

Context: replacing the VM at the same time as replacing direct AST lowering
would conflate semantic lowering bugs with runtime bugs. Aster also needs a
fast development interpreter after native backends exist.

Alternatives: write a second IR interpreter, begin with C generation, or switch
all programs to IR lowering in one step.

Reason: reusing the bytecode verifier and runtime isolates the new boundary.
Assigning each IR virtual value a temporary VM local keeps the operand stack
empty across CFG edges and provides a direct representation of value
consumption.

Consequences: `run-ir` and `dump-ir-bytecode` are experimental during
migration. Unsupported IR operations fail explicitly. Differential tests
compare stdout, stderr, and exit status with the original bytecode compiler
before each language slice becomes eligible for the default path.

## Keep split iterator operations in bytecode

Decision: add local `has_next` and consuming `next` bytecode operations for the
typed IR adapter.

Context: the original stack bytecode fused iterator testing, item assignment,
and a branch target into one instruction. Typed IR represents iterator state
as an owning local and expresses testing and extraction as separate typed
operations.

Alternatives: reconstruct the fused instruction during CFG lowering, keep a
pending item in another hidden local, or weaken the IR iterator model.

Reason: split local operations preserve the IR contract directly, keep the
operand stack empty at block boundaries, and apply uniformly to arrays and
vectors.

Consequences: the VM has two small additional verified instructions. `next`
moves an item out and clears its iterator-owned slot, so iterator destruction
only frees unconsumed elements. Loop CFG remains ordinary branch and jump
bytecode.

## Use local element-builder bytecode operations

Decision: lower typed IR element construction to verified operations that
mutate and finally consume a builder stored in a VM local.

Context: the original bytecode compiler keeps an in-progress HTML builder on
the operand stack. Typed CFG blocks require an empty operand stack at control
flow edges, and ordinary `if`, `for`, and `switch` may cross many such edges
while one outer element remains under construction.

Alternatives: reconstruct a stack-held builder around every CFG edge, add a
second HTML runtime, or lower elements to native calls.

Reason: `HTML_ATTR_LOCAL`, `HTML_APPEND_LOCAL`, and `HTML_FINISH_LOCAL` directly
preserve the IR's owning builder-local contract. They reuse the VM's existing
escaping, child collection, and deterministic drop behavior.

Consequences: the verifier checks builder local and constant indices. Finish
empties the source local before producing the owning `Html` result, so cleanup
cannot destroy it twice. Components use ordinary calls and fragment builders;
element-body control flow needs no special bytecode instructions.

## Keep project IR execution explicit during migration (superseded)

Decision: expose `project run-ir MANIFEST [TARGET]` alongside `project run`.

Context: single-file examples are insufficient evidence for module identity,
canonical generic specialization, manifest source roots, and substantial
Aster-written applications. During migration, changing every project at once
would have removed an independent behavior comparison.

Alternatives: switch project execution immediately, add an environment
variable, or test combined source text outside the manifest loader.

Reason: an explicit command exercises the real project loader and target rules
while keeping backend selection visible and reproducible.

Consequences: differential tests compare status, stdout, and stderr for module
fixtures, generic targets, project test targets, and the documentation server.
The earlier compiler remained the default until network integration and the
remaining runtime fixtures had equivalent coverage. The later decisions below
record the completed transition and its eventual removal.

## Make typed IR the default execution boundary

Decision: route `run`, `project run`, and `project test` through typed IR and
the IR-to-bytecode adapter.

Context: differential coverage now includes scalar and aggregate semantics,
ownership cleanup, generics, modules, raw pointers, FFI slices, elements,
substantial manifest applications, and live HTTP servers. Keeping the direct
compiler as the default would let future backends bypass Aster's intended
shared semantic representation.

Alternatives: keep IR opt-in indefinitely or immediately remove the earlier
compiler before the typed-IR path had equivalent coverage.

Reason: the default path must exercise the architecture that C and VM
backends share.

Consequences: the default switch exposed and fixed slice-local support,
unreachable value-return merge blocks, the `long` minimum literal, and
consuming clone cleanup for fresh owning temporaries.

## Remove the parallel AST-to-bytecode compiler

Decision: delete the earlier compiler and its CLI/API entry points. Source
execution always passes through verified typed IR; backend differential tests
compare the typed-IR VM with generated C.

Context: maintaining two independent semantic lowerings caused collection,
reference-argument, exception, and ownership behavior to drift. Typed IR now
covers the supported language and is the shared semantic boundary for both
remaining backends.

Reason: a single lowering path makes ownership and control-flow rules
enforceable in one place and prevents an obsolete backend from constraining or
silently contradicting the language.

Consequences: `run` and `project run` are canonical. `run-ir` and `project
run-ir` remain descriptive aliases, while backend comparison means typed-IR VM
versus generated C.

## Apply only block-local bytecode adapter peepholes

Decision: remove redundant IR temporary-local traffic and fallthrough jumps
during bytecode emission, but never carry an operand-stack value across a CFG
block boundary.

Context: assigning each IR virtual value a VM local made arbitrary CFG edges
simple and safe, but initially produced roughly three times the dynamic
instruction count of the legacy compiler on small benchmark workloads.

Alternatives: leave all adapter traffic intact, build a global bytecode
optimizer immediately, or replace the stack VM with a register VM.

Reason: immediate `STORE_LOCAL x; MOVE_LOCAL x` pairs, discarded unit values,
self move/store prologues, and jumps to the next block are locally provable
artifacts. Removing them preserves the verified IR ownership model and the
empty-stack rule at every actual edge.

Consequences: the benchmark workloads now execute roughly 1.6–1.7 times the
legacy instruction count instead of about three times, while remaining faster
in the current VM measurements. The benchmark reports timing and dynamic
counts rather than treating these observations as permanent performance
claims. Larger scheduling or a register-bytecode design remains measurement
driven.

## Start native code generation with a portable C17 slice

Decision: add `emit-c` as the first native-oriented backend consuming verified
typed IR, beginning with scalar functions and CFG.

Context: Aster now executes through typed IR by default. A second consumer is
needed to prove that the IR is backend-neutral, while C provides an inspectable
portability and optimization
baseline.

Alternatives: begin native code generation immediately, transpile the AST directly, or wait until
every runtime type has a final ABI.

Reason: a narrow C backend exposes missing IR contracts early and can be
compiled by both GCC and Clang without adding a compiler framework. Consuming
the AST would recreate semantic lowering and violate the shared-backend
architecture.

Consequences: the initial emitter accepts scalar locals, direct calls,
branches, loops, comparisons, and checked add/subtract/multiply/negate.
Generated narrow integers use wide C storage plus explicit Aster-width
bounds. Aggregates, RAII cleanup, native calls, and project linking remain
unsupported with diagnostics until their runtime and ABI contracts are
implemented.

## Put resolved aggregate member types in typed IR

Decision: store a canonical type ID for every struct field and an optional
canonical payload type ID for every enum variant in the typed IR type table.

Context: the VM's dynamic aggregate objects could operate from field indices
and runtime values, but C must know concrete member types to declare
layouts. Generic declarations make names alone insufficient because
`Pair<int, bool>` and `Pair<long, Html>` have different concrete members.

Alternatives: let each backend inspect checker declarations, introduce a
backend-specific layout AST, or postpone all aggregate native emission.

Reason: resolved member types are semantic facts shared by every backend.
Capturing them once preserves the rule that backends consume verified typed
IR and ensures generic substitution is identical across bytecode and C.

Consequences: the verifier rejects missing or invalid member metadata and
checks struct-construction labels and operand types. The C backend can emit
copyable fixed arrays and structs, including aggregate calls and mutation.
Its generated layouts remain internal C representations; stable `extern C`
layout is a separate ABI milestone.

## Represent copyable enums as tagged C unions

Decision: emit each supported enum as a C struct containing a declaration-order
`uint32_t` tag and an optional union of payload-bearing variants.

Context: typed IR now records every concrete variant payload type. Supporting
`Result` and `try` requires constructors, tag tests, and payload extraction,
while user enums and compiler-known enums should not have separate backend
semantics.

Alternatives: special-case only `Option`/`Result`, use one byte array sized to
the largest payload, or delay enums until the complete native ABI is frozen.

Reason: a typed C union is inspectable, warning-clean, and preserves payload
types for the portable reference backend. One representation lets ordinary
user unions, generic instantiations, `switch`, and `try` share the same lowering.

Consequences: copyable enum programs now compile and execute through C.
Move-only payloads remain rejected until native cleanup and ownership runtime
contracts exist. The representation is internal and does not yet promise the
target enum layout or an external C ABI.

## Make destructor identity a typed IR contract

Decision: attach a verified concrete destructor function ID and target
size/alignment to each applicable IR type. Keep cleanup locations as explicit
`local_drop` instructions.

Context: the VM historically recovered language destructors from runtime
metadata strings. That is workable for dynamic VM objects but would force C
to duplicate name lookup and semantic knowledge. Native lowering also
needs a precise way to distinguish an available owning slot from a moved one.

Alternatives: retain runtime name lookup, lower destructors into every cleanup
edge before IR, or introduce reference counting.

Reason: destructor identity is type metadata, while cleanup order is control
flow. Keeping those concerns explicit lets every backend implement the same
small ownership state machine without recreating checker analysis.

Consequences: generated C uses one live bit beside each owning local. Stores
transfer ownership in, moves clear the source, and drops conditionally invoke
a typed helper once. Tests cover reverse scope order, early return, `break`,
`try` propagation, and inter-function moves. Native handles and the broader
shared runtime ABI remain separate work.
## Superseded: spell the borrowed text type `string`

Decision: use `string` as the canonical source spelling for Aster's borrowed
immutable UTF-8 view. Keep `str` only as a temporary compatibility alias in the
type resolver. Public text helpers likewise use `string_` names.

Reason: `str` is unexplained shorthand inherited from another language's
surface conventions. `string` fits Aster's C/C#-shaped vocabulary and is
clear in ordinary application and component signatures. This is a source-name
change only; it does not alter representation, ownership, allocation, or ABI.

## Use one immutable reference-counted string type

Decision: source Aster has one `string` type. It stores immutable UTF-8 bytes
with an exact byte length behind a pointer-sized reference-counted handle.
Assignment, field access, argument passing, and return retain the handle;
destruction releases it. There is no source `String`, `StringView`, conversion
constructor, string move, or post-call invalidation.

Native code may borrow a pointer-and-length view for the duration of a call.
That ABI view is an implementation detail, not a second source type. Builders
still own mutable capacity and transfer their finished byte allocation into a
new `string` handle without copying the bytes.

Reason: strings should behave like ordinary C#/PHP application values while
remaining deterministic, tracing-GC-free, UTF-8-capable, and cheap to pass.
Reference counting adds a small retain/release cost but avoids deep copies and
removes ownership ceremony from essentially every web-facing API.

This supersedes the surrounding historical split-string decisions.

## Superseded: give borrowed strings a two-word native ABI

Decision: represent native `string` as an aggregate containing a data pointer
and an explicit byte length. Emit literal bytes as immutable module data and
pass the two-word view through the generated-C ABI.

Context: Aster string views are borrowed UTF-8 bytes, not C strings. Printing,
file paths, HTTP fields, callbacks, and future FFI signatures must preserve
embedded NUL bytes and must not introduce hidden allocation.

Alternatives: pass NUL-terminated pointers, allocate a runtime string object
for every literal, flatten strings into two unrelated function arguments, or
use a private runtime handle.

Reason: the two-word representation matches typed IR target layout, keeps
borrowing and ownership visible, and maps directly to conventional native
slice ABIs. Static literal storage makes construction allocation-free.

Consequences: copying a `string` copies only the borrowed view. Native functions
receive pointer and length separately, byte-content equality uses both
lengths, and no operation may infer length with `strlen`. The cleanup-managed owned
`String` remains distinct.

## Superseded: keep owned strings behind a pointer-sized runtime handle

Decision: represent native `String` as a cleanup-managed pointer to a small runtime
allocation containing owned bytes and their exact length.

Context: an owned string needs deterministic destruction and deep cloning,
while its capacity and allocator strategy should remain changeable without
altering every generated function signature.

Alternatives: expose `{data, length, capacity}` directly in the language ABI,
reuse the borrowed two-word `string` representation with hidden ownership bits,
or reference-count shared buffers.

Reason: one pointer makes ownership transfer cheap and explicit, keeps the
borrowed and owning representations unambiguous, and leaves future growth
policy inside the runtime. It requires neither garbage collection nor hidden
reference counting.

Consequences: `String::from` allocates, `clone` performs a deep copy, moves
transfer the handle, and generated drops free it exactly once. Borrowed native
operations inspect the handle without consuming it. Aggregate fields store the
same pointer-sized handle and recursive aggregate destruction invokes its drop
helper.

## Transfer StringBuilder storage when finishing

Decision: represent native `StringBuilder` as a pointer-sized cleanup-managed handle
whose runtime allocation stores data, byte length, and capacity. Grow capacity
geometrically and transfer the byte allocation into `String` when finishing.

Context: repeated text construction must avoid quadratic copying, while
`finish(move builder)` already expresses that the builder cannot be reused.

Alternatives: reallocate to exact size on every append, copy all bytes into a
new String during finish, expose capacity fields in the language ABI, or share
the buffer through reference counting.

Reason: amortized growth gives conventional builder performance, and the
explicit consuming finish makes zero-copy allocation transfer straightforward.
The runtime handle keeps capacity policy private.

Consequences: append preserves embedded NUL bytes using explicit lengths,
finish frees only the builder shell, and the resulting String owns the
transferred buffer. Dropping an unfinished builder frees its buffer. Explicit
builder clone performs a deep copy so later mutation is independent.

`StringBuilder.AppendByte` uses the same growth path for one unformatted raw
byte. It exists because Aster-written URL, form, and binary parsers cannot
correctly reconstruct percent-decoded bytes through scalar formatting or
single-byte temporary strings.

## Keep List as an opaque owning handle

Decision: give every monomorphized `List<T>` a pointer-sized language ABI while
storing its data pointer, count, capacity, and element stride in private
runtime state.

Context: the VM already represented vectors as owning runtime objects and the
data-layout reference classified managed values as opaque handles, but the
checker incorrectly reported `List<T>` as a three-word inline value. That would
make structs and native call ABIs disagree across backends.

Alternatives: expose a three-word inline header everywhere, retain the
inconsistent backend-specific layouts, erase element layout, or use reference
counting.

Reason: one opaque handle matches the existing ownership model and leaves
allocation strategy private. Monomorphized typed IR still supplies exact
element size and generated destruction behavior, so the buffer is not
type-erased semantically.

Consequences: vector moves transfer one pointer, push copies the concrete
element representation after ownership transfer, and generated drop callbacks
destroy elements in reverse order. Consuming iterators own the vector and track
the consumed prefix; dropping an iterator destroys only its unconsumed suffix.
The checker now reports the documented pointer-sized layout.

## Generate typed callbacks for native vector cloning

Decision: clone copyable vector elements with bulk byte copying and generate
one concrete clone callback for each supported cleanup-managed runtime element
type.

Context: `List<T>` stores monomorphized target-layout bytes. Deep cloning must
preserve the exact `T` semantics without runtime reflection, type tags, hidden
reference counting, or a universal object representation.

Alternatives: prohibit vector clone in native builds, attach runtime type
descriptors to every vector, call back into the VM, or shallow-copy owning
handles.

Reason: generated callbacks keep type knowledge in typed code. The generic C
runtime only manages allocation and traversal, while generated callbacks clone
`String`, `StringBuilder`, and nested `List<T>` handles into destination slots.

Consequences: copyable vectors use one `memcpy`; owning vectors deep-clone each
element. Callback composition supports nested vectors without runtime type
tags, and cloned vectors can grow independently. User-defined cleanup-managed
aggregate callbacks require structural clone-helper generation and remain a
separate extension.

## Copy process inputs into owned language strings

Decision: expose application arguments and environment lookup through
registered primitives that return owned `String` values, and exclude launcher
arguments from the guest-visible list.

Context: command-line programs need process inputs, but host `argv` and
environment pointers have external lifetimes and must not become accidental
long-lived guest string views.

Alternatives: pass arguments to `main`, expose borrowed host strings, eagerly
allocate a complete argument vector, or omit process inputs.

Reason: indexed lookup is a small FFI surface, performs no allocation until an
argument is requested, and fits the existing explicit `Result` and cleanup-managed
`String` model.

Consequences: `lang run file.as -- a b` exposes exactly `a` and `b`.
Successful lookups allocate owned strings; missing indexes or variables
produce typed errors. Project-target argument forwarding remains separate
follow-up work.

## Keep filesystem mutation narrow and non-recursive

Decision: expose path queries, one-directory creation, rename, file removal,
and empty-directory removal as separate Result-returning primitives.

Context: real command-line and server programs need filesystem operations, but
a broad native path object or implicit recursive mutation would enlarge the
FFI and make destructive behavior easier to invoke accidentally.

Alternatives: expose raw POSIX calls, create a privileged native path object,
make removal recursive, or implement all traversal policy in C.

Reason: small operations preserve typed error handling while leaving recursive
walking, filtering, and application policy in Aster. Separate file and
directory removal makes intent visible.

Consequences: path encoding and rename replacement details currently follow
the host platform. Directory creation does not create parents. Directory
removal requires an empty directory, and file removal does not remove a
directory.

## Parse command lines in Aster

Decision: expose only indexed process arguments from C and implement CLI token
classification in `std::cli`.

Context: applications need conventional flags and named options, but putting a
schema or callback-driven parser in the runtime would move application policy
back into privileged C code.

Alternatives: native parser objects, passing `argv` directly to `main`, schema
macros, or requiring every application to parse byte strings independently.

Reason: Aster loops, enums, generics, owned strings, and Result can express a
small reusable parser and directly test Aster-written application libraries.

Consequences: the initial parser recognizes flags, `--name=value`,
positionals, and `--`. It returns a consuming vector rather than a random
access map because the current List API emphasizes ownership transfer. Schema
validation, help generation, and repeated-option policy remain higher-level
Aster work.

## Assertions return values instead of adding a test opcode

Decision: implement assertions and case summaries in `std::testing` using
ordinary `Result`, `String`, structs, and functions.

Context: project manifests can execute test targets, but programs previously
had to hand-write comparisons and exit-code bookkeeping. A privileged assert
opcode would duplicate existing control flow and cleanup behavior.

Alternatives: compiler-known test declarations, trap-based assertions, a
native testing registry, or external-only golden tests.

Reason: Result-returning assertions compose with `try`, preserve deterministic
cleanup, and let one target report multiple independent cases. Owned failure
strings support useful expected/found output without special diagnostics.

Consequences: each manifest test target still has one ordinary `main`.
`TestRecord` reports cases and `TestFinish` supplies its exit status.
Automatic source-level test discovery and captured output are not yet
implemented.

## Stream files through caller-owned buffers

Decision: add borrowed `read_into` and exact-prefix `write_bytes` primitives,
then implement the reusable copy loop in Aster.

Context: `read_all` is convenient for configuration and small assets but
forces memory usage to scale with file size. Parsers, asset pipelines, and
protocol tools need bounded byte processing.

Alternatives: return a newly allocated buffer for every read, expose `FILE *`
as a raw pointer, implement the whole copy operation natively, or wait for a
larger I/O framework.

Reason: the existing unsafe `Buffer` to `Span<byte>` bridge already expresses
non-owning byte access. Borrowing that slice for one native call preserves
explicit ownership while Aster controls looping, EOF, totals, and cleanup.

Consequences: reads may initialize only a prefix and report its length.
Writes consume exactly that prefix or fail. The first API is synchronous and
buffered by the host C stream; seeking, asynchronous I/O, and memory mapping
remain separate features.

## Reinitialize wholly moved mutable locals

Decision: assignment to a mutable local makes it available even when its prior
state was moved, destroyed, or conditionally moved.

Context: reusable builders and state machines naturally move a value into a
finishing operation and then assign a fresh value to the same binding. The
checker previously rejected this even though initialized-slot assignment
already handles live and empty destinations.

Alternatives: require a new lexical binding per transition, insert clones, add
a special reset operation, or retain the restriction.

Reason: whole-local reinitialization creates no duplicate owner. A live old
value is destroyed before replacement; an empty slot receives the new owner.

Consequences: use between move and assignment remains an error. Immutable
locals still cannot be reassigned. Partial-field moves remain unsupported.

## Assemble streaming lines in Aster

Decision: expose byte-slice inspection and explicit range copying, then
implement LF line assembly in `std::file`.

Context: bounded reads alone cannot support parsers when tokens cross input
buffer boundaries.

Alternatives: native `getline`, C-owned iteration, callbacks from C into
Aster, or continuing to require `read_all`.

Reason: byte inspection plus `StringBuilder` lets Aster control delimiter
scanning, cross-chunk state, allocation, and ownership.

Consequences: `ReadLinesBuffered` returns `List<String>` and therefore bounds
input buffering but not total retained output. `ForEachLineBuffered` keeps
only the reusable input buffer and current line, invokes a typed non-capturing
callback, supports successful early stopping, and propagates callback errors
through ordinary `try` cleanup. Its borrowed line view is call-scoped by API
contract; Aster does not prove that a callback cannot retain a dangling raw
view. LF is the only delimiter and carriage returns are preserved.

## Borrow reusable application values across Aster calls

Decision: permit cleanup-managed parameters on ordinary functions to be marked
`borrow`, and add checked `vec_get` for copyable elements.

Context: a production-shaped router must inspect a growable route collection
for every request without consuming or deeply cloning it.

Alternatives: fixed route slots, clone the route vector per request, keep the
router in C, or introduce a general borrow checker.

Reason: a non-owning call-scoped parameter matches Aster's deliberately small
ownership boundary. `vec_get` copies only elements whose types are copyable.

Consequences: borrowed parameters are excluded from deterministic cleanup and
their callers retain ownership. Aster does not prove arbitrary alias
lifetimes. The Aster-written router preserves insertion order and has no
fixed route limit.

## Keep method receivers as ordinary parameters

Decision: permit the first parameter of a type-qualified function to use
inferred `self` syntax, with the ordinary `mut` and `borrow` modifiers.
Ordinary owning parameters may also be declared `mut`.

Context: static method-call sugar removed noisy call sites, but declarations
still repeated the owner type and could not mutate an owned parameter without
introducing another local.

Alternatives: retain fully explicit parameter types, add a separate method
declaration kind, infer receiver ownership, or introduce implicit references.

Reason: `self`, `mut self`, `borrow self`, and `borrow mut self` map directly
onto parameter behavior Aster already checks and lowers. This improves common
code without adding dispatch, hidden aliasing, or a second ownership model.

Consequences: inferred `self` is restricted to the first parameter of a
type-qualified function. Consuming a cleanup-managed receiver remains explicit at
the call site, and all backends continue to receive ordinary static calls.

## Lower integer ranges directly to scalar control flow

Decision: support half-open `start..end` syntax only in `for` loops and lower
it to checked scalar IR rather than constructing a range or iterator object.

Context: ordinary Aster library code repeatedly used mutable counters and
`while index < length` for bounded traversal.

Alternatives: retain manual loops, allocate a built-in `Range<T>`, introduce a
general iterator protocol, or immediately support inclusive and stepped
ranges.

Reason: a dedicated front-end lowering removes common boilerplate while
preserving predictable cost. Both bounds are evaluated once, integer typing is
static, and existing CFG plus checked arithmetic is sufficient for every
backend.

Consequences: ranges currently exist only as `for` syntax, are ascending and
half-open, and allocate nothing. Inclusive, descending, and custom-step ranges
remain explicit future additions rather than partially defined behavior.

## Keep SQLite behind a narrow registered adapter

Decision: link SQLite optionally and build a SQLite-shaped application API over
opaque cleanup-managed handles. Preserve real SQL, prepared statements,
binding, stepping, typed columns, transaction modes, savepoints, and SQLite's
execution model. Add ordinary row-mapping helpers and ordered SQL-file
migrations without introducing an ORM or provider-neutral hierarchy.

Context: database-backed HTTP applications need persistence, but arbitrary C
ABI exposure would leak SQLite pointer lifetimes and transient text views into
ordinary Aster code.

Alternatives: ADO.NET-shaped provider interfaces, a native ORM,
dynamic-library calls from Aster, returning raw `sqlite3 *` pointers, or
implementing a database engine.

Reason: SQLite's C API is stable and the existing native-handle boundary
already supplies exactly-once destruction and Result construction.

Consequences: `Database`, `Statement`, and `Transaction` are distinct public
types. Statements must not outlive their database, which Aster does not
statically prove. Text, blobs, and error messages are copied. Named parameters
are resolved by SQLite. Transactions roll back on cleanup and nested
transactions use savepoints. Migrations remain ordinary SQLite SQL. Lime may
later provide a more productive application layer without changing this core
contract.

## Make generated C the primary backend

Decision: define Aster as a deterministic application language with explicit
costs, simple ownership, first-class typed HTML, excellent generated C, and a
fast VM development loop. Prioritize portable C first, the bytecode VM second,
with no third backend competing for implementation priority.

Context: Aster had acquired enough application features to resemble a much
less mature V-like language, while older architecture notes still described
another native backend as the likely default build. Feature breadth and equal backend
coverage did not provide a distinctive or sustainable design criterion.

Alternatives: pursue feature parity with broad application languages, advance
all backends equally, add a custom native path, or narrow Aster into a
systems-only language.

Reason: deterministic cleanup without a tracing collector or Rust-style
lifetime model, visible allocation and cloning, typed application libraries,
portable C deployment, and quick VM feedback form a coherent combination.
Real applications can test that combination more effectively than isolated
syntax milestones.

Consequences: new features require pressure from real Aster programs and an
explainable cost model. They must lower explicitly through typed IR and work
correctly in generated C and the VM. No additional backend blocks C/VM
milestones or drives source design.
Established C libraries and operating-system mechanisms remain valid runtime
boundaries. The complete policy is maintained in `docs/thesis.md`.

## Make interpolation obey its destination

Decision: use `$"..."` for interpolation and make its cost depend on the
statically known destination. Direct HTML text and attribute interpolation
lowers as typed segments into the active builder. Ordinary value-position
interpolation returns an owned `String`.

Context: native UI markup needs convenient mixed text without turning Aster
into a template language or making every interpolation allocate. Attribute
and text destinations also require different escaping.

Alternatives: always allocate a temporary `String`, introduce a runtime
template object, make interpolation HTML-only, or require manual
`StringBuilder` calls everywhere.

Reason: the destination is already known during typed-IR lowering. Preserving
the segments lets generated C and the VM format scalars directly, apply the
correct escaping at the write, and keep allocation explicit when a string
actually escapes as a value.

Consequences: `$"..."` has type `String` in ordinary expressions, but direct
element text and properties consume it through destination operations without
materializing that value. Supported substitutions are text, owned strings,
booleans, characters, and numeric scalars. There is no dynamic formatting
protocol, reflection, reference counting, or hidden clone. Owned interpolation
uses one builder and formats scalar holes into stack storage rather than
allocating one string per hole. `Url::relative` may consume that owned result
and retype its allocation without copying it. Direct HTML interpolation
borrows an owned `String` place only for the immediate escaped append, avoiding
both a clone and a move; owned interpolation retains the ordinary explicit-move
rule. A component `string` or `Option<string>` property may receive interpolation
through a scoped exception: build one owned result, borrow it across that
component call, then drop it. This is not a general `String`-to-`string` coercion
and cannot hide a clone.

## Make CSS the final embedded parser

Decision: parse ordinary CSS syntax directly inside native `<style>` elements,
preserve unfamiliar constructs, and do not embed a JavaScript parser.

Context: native HTML made UI structure part of normal Aster programs, but CSS
still had to be fragmented into quoted Aster strings. A fixed property/type
catalog would lag the web platform, while treating CSS as an entirely opaque
blob would prevent structural diagnostics and future scoping or extraction.

Alternatives: require multiline strings, create an Aster-specific CSS DSL,
model every property as an enum or builder method, leave CSS in external files,
or embed general JavaScript parsing alongside CSS.

Reason: `<style>` is an unambiguous grammar boundary. A source-spanned tree of
rules, at-rules, and declarations can validate authored structure while raw
names and values retain forward compatibility. Static CSS then lowers as the
original bytes into the existing raw-text HTML destination with no allocation.

Consequences: CSS inside `<style>` uses CSS syntax rather than Aster child
expressions. Unknown properties, values, functions, descriptors, and at-rules
pass through unchanged. CSS interpolation, extraction, and minification remain
separate later layers over the preserved structure. CSS is the final embedded
parser; `<script>` remains raw text, with external JavaScript or Aster-compiled
Wasm supplying client behavior.

## Scope component CSS by rewriting the preserved tree

Decision: `<style scoped>` in an `Html` function rewrites style-rule selectors
with a stable module/function attribute and marks native elements emitted by
that function. Plain `<style>` remains global.

Context: native HTML and CSS should compose in normal Aster component code
without inventing a CSS DSL, requiring manually unique class names, or adding a
runtime styling system.

Alternatives: random scope identifiers per build, runtime selector rewriting,
class-name mangling, shadow DOM, allowing scoped styles anywhere, or propagating
the parent's scope through every child component call.

Reason: a deterministic presence attribute preserves ordinary CSS syntax and
keeps costs explicit. The parsed CSS tree identifies selector spans, including
nested rules, while original declaration bodies remain untouched. Applying the
marker in both bytecode compilation and typed IR keeps the VM and primary C
backend equivalent. Child components retain their own styling boundary.

Consequences: selector lists and pseudo-elements require structural insertion;
keyframe step selectors are excluded. The marker is compile-time metadata and
never appears on `<style>`. Moving or renaming a module/function may change its
identifier, but repeated builds of the same component do not. Extraction and
deduplication consume the same metadata without changing selector semantics.

## Treat static component styles as document resources

Decision: register each `<style scoped>` once per root `Html` document. Keep VM
and ordinary C builds inline; let `emit-c-site` replace reachable component
styles with one link to one deterministically hashed CSS asset.

Context: component syntax naturally executes once per component instance, but
shipping twenty identical style elements is wasted output. Production sites
also need cacheable CSS without maintaining a second source representation.

Alternatives: global process state, unconditional compile-time hoisting, one
asset per component, HTML post-processing, runtime CSS construction, or making
external CSS mandatory.

Reason: document-owned identities remain correct when documents are built
independently or interleaved. Detached `Html` buffers retain style occurrence
metadata, so composition can omit only duplicate style ranges without parsing
HTML. Extraction uses the same compiler metadata and stable rewritten CSS,
concatenates it in deterministic reachable-function/source order, and hashes
the exact asset bytes. Inline mode requires no separate deployment step.

Consequences: the first rendered component determines the inline style or link
position. External mode may include a reachable component's CSS even when a
runtime branch does not render that component; the browser receives one static
cacheable asset and runtime HTML does no CSS construction. Plain `<style>`
remains authored global markup and is not silently moved into the component
asset.

## Route dynamic CSS through validated custom properties

Decision: permit `--name=value` on native elements and serialize all such
properties into one `style` attribute. Keep native `<style>` content static and
refer to dynamic values with ordinary `var(--name)` CSS.

Context: components need dynamic colors and dimensions, but arbitrary string
interpolation can cross declaration and grammar boundaries, hides generated
CSS, and prevents static extraction.

Alternatives: interpolate into native CSS, accept arbitrary dynamic `style`
strings, create a CSS builder DSL, or initially expose only dedicated color and
length types.

Reason: CSS custom properties are the web platform's existing value channel.
Aster can keep the stylesheet parsed, scoped, deduplicated, and extractable
while validating the smaller runtime boundary. Numeric values need no
allocation. String values use a conservative single-atom alphabet that excludes
all grammar-breaking characters.

Consequences: `#e45b20`, `12px`, `1.5rem`, percentages, and identifiers work.
Compound values, functions, URLs, and whitespace-separated lists require future
typed constructors rather than widening raw-string authority. Dynamic `style=`
and interpolation inside `--name` are rejected.

## Put nginx at the public production boundary

Decision: deploy Lime's H2O adapter on loopback behind nginx. nginx owns public
TLS, HTTP-to-HTTPS redirection, public protocol negotiation, certificates, and
virtual hosting. Direct H2O TLS is not a Lime roadmap item.

Context: Aster targets PHP/C-style web deployment and should not make each
application responsible for certificate lifecycle or public TLS policy. H2O is
still valuable as the embedded, production-shaped application HTTP transport.

Alternatives: expose Aster's handwritten HTTP server publicly, configure
H2O as the TLS edge, embed certificate automation in Lime, or make a particular
external proxy part of Lime's application API.

Reason: nginx is mature infrastructure with a clear operational boundary.
Keeping it outside Lime preserves adapter-neutral application code while H2O
continues to own proven parsing, connection handling, streaming, and response
I/O behind that boundary.

Consequences: Aster/H2O binds to `127.0.0.1`; Lime honors forwarded origin
data only from an explicitly trusted nginx peer. nginx configuration and
certificate renewal belong to host administration. A systemd restart drains
the old H2O process gracefully but has a brief listener handoff until Lime
adds an explicitly justified zero-downtime mechanism.
## Use bounded C++-style copy control for user-owned values

Decision: add implicit recursive copy, a user-defined copy constructor, and a
deleted copy constructor for user value types. Begin with the Aster/C++-family
spellings `public T(const ref T source) { ... }` and
`private T(const ref T source) = delete;` (or `public` where exported).

Context: deterministic destruction does not by itself make the default copy of
a raw resource owner correct. C leaves that contract to conventions and helper
functions. C++ lets a type author define duplication or delete copying without
requiring lifetime annotations or borrow checking.

Alternatives: make every destructor-bearing type noncopyable, add a Rust-style
ownership model, retain unconditional field copying, or reproduce all C++
special members.

Reason: ordinary Aster values should continue to copy normally. Exceptional
resource owners need the same bounded choice used in conventional C++: accept
recursive member copying, implement a real copy, share through an existing
managed field, or reject copying. `const ref` describes the non-owning,
immutable source used while constructing the independent destination; it does
not introduce lifetime analysis.

Consequences: value assignment constructs its replacement before destroying
the previous destination. Class-variable assignment remains a pointer-like
alias copy; an explicit `new T(existing)` may separately invoke a class copy
constructor. Compiler-internal moves and return elision remain implementation
details. Aster does not initially add copy-assignment operators, move
constructors, lifetime annotations, a borrow checker, or move-only defaults.
