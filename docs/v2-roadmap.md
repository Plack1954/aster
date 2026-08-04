# Aster 0.3 direction and status

Aster 0.3 is aimed at substantial applications written primarily in Aster.
Portable generated C is the primary backend and the verified bytecode VM is the
second implementation and development path. Both consume the same typed IR;
backend priority does not change language semantics.

This ordering follows the [Aster thesis](thesis.md). New work must be
motivated by substantial Aster programs, expose its allocation and ownership
costs, and preserve predictable typed-IR lowering. Backend breadth by itself is
not a milestone.

## Completed foundation

1. Manifest projects with strict source roots, namespace/file identity, named
   binary/library/test targets, whole-namespace using declarations, and aliases.
2. Explicit target data layout: integer and pointer widths, alignment,
   endianness, field offsets, tagged-enum layout, and checked `extern struct`
   boundaries.
3. User-defined generic structs and enums with canonical cross-module
   instantiations.
4. Ownership substitution: an applied aggregate is copyable only when all
   concrete fields are copyable, and nested owning fields are destroyed
   deterministically.
5. Inferred monomorphized generic functions with independent typed AST and
   bytecode bodies, recursion, and cross-module deduplication.
6. Exact, copyable, non-capturing `ReturnType(ParameterTypes)` values and verified indirect
   bytecode calls.
7. Aster-written Pair, Option/Result operations, List/StringBuilder helpers,
   typed routing, and middleware.
8. Configurable HTTP header/body bounds, socket timeouts, Content-Length
   framing, request body views, 413 handling, `:name` path parameters, and
   chunked response streaming.
9. Bounded sequential HTTP/1.0 and HTTP/1.1 keep-alive with a single
   socket-owning RAII handle, read timeouts, a request-count cap, and explicit
   pipelining rejection.
10. Typed Aster-written Request, Response, Handler, and Router values plus
    Result-returning transport operations used by the server loop.
11. A multi-module documentation-server project with render/server binaries,
   Aster-written configuration parsing, RAII directory traversal, Markdown
   filtering, a served static CSS asset, Aster-written smoke tests, and a
   live two-request HTTP integration test.
12. Expression-valued `if` and exhaustive `switch`, lowered to explicit typed-IR
    CFG and covered by VM and generated-C tests.
13. Non-consuming `for borrow item in collection` for copyable fixed-array,
    slice, and vector elements, retaining the collection after the loop
    without cloning its storage.
14. Whole-project C emission plus C runtime coverage for strings,
    builders, vectors, indirect calls, typed URLs, HTML builders, cloning,
    escaping, rendering, and deterministic cleanup.
15. Generated-C execution of the documentation-server render target and the
    SQLite issue-tracker render target through a typed registered-native bridge.
16. An integrated issue-tracker proof target that loads validated
    configuration, reads seed records through a 17-byte buffer, validates
    application input, creates and queries SQLite state, dispatches an
    Aster-written typed router, and renders typed HTML through both the VM and
    generated C under leak sanitizers.
17. A bounded live issue-tracker server executed through both the VM and
    generated C. The shared socket harness verifies SQLite-backed GET, decoded
    and validated POST, route-parameter lookup, typed HTML, redirect ownership,
    413 body-limit rejection, 400 malformed-request rejection, process exit,
    and leak-free connection cleanup.

## Current architectural boundary

Aster code owns application policy, generic composition, routing, middleware,
HTML construction, and RAII wrappers. Small C runtime boundaries remain for
allocator and operating-system mechanisms, established libraries, VM storage,
registered file/socket calls, and HTTP framing.

There is no garbage collector. Immutable strings use narrow reference counting;
resources and general objects do not. Function values do not allocate
environments. Generic code is monomorphized.

## Deliberately deferred

- capturing closures and callback environments
- explicit generic call arguments and generic constraints
- traits, specialization, variance, or reflection
- stable native aggregate and function ABI
- machine-code generation
- HTTP pipelining, TLS, concurrency, and inbound chunked bodies
- dependency registry and version solver

Async/await is now an accepted first-class direction. See
[`async.md`](async.md) for the staged compiler/runtime work.

The next useful depth work should come from extending this bounded server
rather than adding isolated syntax: response streaming, timeout behavior,
keep-alive reuse, or deterministic cleanup under interrupted writes. Choose
the next case from observed application or integration failures. The VM must
retain semantic parity and fast feedback.

## Verification

The release gate is the complete CTest suite under warning-clean GCC and Clang
builds plus GCC AddressSanitizer/UndefinedBehaviorSanitizer with leak detection.
The docs-server render, server check, and Aster test target are part of that
suite.
