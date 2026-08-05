# Version 0/1 acceptance map

This file maps the specification's numbered acceptance criteria to concrete
repository evidence. It is intentionally narrower than a roadmap: a green
entry means the bounded 0.1 behavior is implemented and directly exercised.

| # | Criterion | Evidence |
|---:|---|---|
| 1 | Builds with Clang and GCC | Strict `-Werror` build commands below; warnings are applied to every C target. |
| 2 | Normal test suite passes | `ctest`; the `language_tests` aggregate also checks every positive and negative source fixture. |
| 3 | ASan and UBSan pass | `LANG_SANITIZE=ON` build and test command below. |
| 4 | Hello world runs | `hello_run`, `examples/hello.as`. |
| 5 | Functions, structs, arrays, enums execute | `arithmetic`, `structs`, `arrays`, `enum_match_run`, and `recursion_run`. |
| 6 | Ordinary values copy normally | Buffer, string, List, aggregate, and NativeHandle copy fixtures. |
| 7 | Assignment never invalidates its source | `buffer_value_copy`, `native_handle_value_copy`, and conditional/loop copy fixtures. |
| 8 | Noncopyability is narrow | `noncopyable_arena`, `noncopyable_struct`, and `generic_noncopyable`. |
| 9 | Reverse-order destruction | `raii_cleanup_run` and `language_destructors_run`. |
| 10 | Early-return destruction | `raii_cleanup_run`. |
| 11 | `try` error cleanup | `raii_cleanup_run` and typed-AST cleanup-plan assertions. |
| 12 | Arena destruction frees allocations | `arena_run`, expired-pointer trap, and leak-enabled sanitizer suite. |
| 13 | Registered native C calls | `ffi_registration`, source FFI, mutable-slice FFI, and native Result FFI. |
| 14 | Deterministically cleaned native handles | `file_raii_run`, native-handle copy tests, trap/native-failure cleanup, and HTTP socket handles. |
| 15 | Elements are normal expressions | AST-shape test and every HTML golden program. |
| 16 | Element names use normal resolution | Source declarations in `std/html.as`, custom elements, qualified-name golden test, and unknown-element diagnostic. |
| 17 | Properties are statically checked | Missing, unknown, duplicate, wrong-type, and children-disallowed diagnostics. |
| 18 | Closing-tag mismatch is diagnosed | `mismatched_element_closing_diagnostic`. |
| 19 | Ordinary `if` works in element bodies | `html_control_run`, golden output, and AST-shape assertion. |
| 20 | Ordinary `for` works in element bodies | `html_control_run`, golden output, and AST-shape assertion. |
| 21 | No template control-flow AST exists | `StmtKind` is shared; `element_control_ast_shape` asserts `STMT_IF`, `STMT_FOR`, `STMT_MATCH`, and `STMT_BLOCK`. |
| 22 | Supplied HTML shape renders | `html_render_golden` and the qualified/control-flow goldens. |
| 23 | HTML text and attributes escape | `html_all_escaping_golden`; raw insertion requires `Html.UnsafeRaw`. |
| 24 | No tracing GC | Strings and NativeHandle resources use narrow reference counting; other managed values use deterministic copy and cleanup. |
| 25 | No compiler framework or C backend in 0.1 | The completed 0.1 slice used only bytecode. The later user-approved 0.2 roadmap deliberately adds a typed-IR C emitter; LLVM and Cranelift remain absent. |
| 26 | Safety limits are documented | `values-and-cleanup.md`, `language.md`, `ffi.md`, and `native-elements.md`. |

Additional 0.1 gates cover parser recovery, bytecode verification, runtime
stack traces, recursion limits, integer overflow and shifts, null/expired
pointers, module cycles/visibility/ambiguity, exact declaration identity,
explicit typed-AST cleanup plans, HTTP A-D, and GCC/Clang warning cleanliness.

The browser-Wasm trial emits the same Aster source as native C for initial
server HTML and as freestanding `wasm32` for checked counter and form
transitions. Native `onclick`, `oninput`, and `onsubmit` properties generate
typed hydration metadata; no marker names or C export adapters are handwritten.
Headless Chrome verifies direct updates while retaining the SSR DOM.

The application foundation now includes `std.process`: `lang run` forwards
explicit application arguments after an optional `--`, indexed access returns
normal strings through typed Results, and environment values are copied into
immutable language storage.
`std.filesystem` adds Result-returning existence/type queries, directory
creation, rename, file removal, and empty-directory removal. Its integration
test performs the complete workflow beneath an isolated build-directory tree
and verifies that no test path remains.
Text utilities now provide Aster-written prefix, suffix, containment, and
byte-search operations plus decimal formatting. `std.cli` uses these
with process arguments to produce flag, named, and positional enum values,
including conventional `--` termination.
`std.testing` adds Result-returning assertions, owned expected/found failure
messages, case reporting, summary accumulation, and exit-status integration.
The testing example runs as a real manifest `kind = "test"` target; an
intentional-failure fixture verifies nonzero status and readable output.
File I/O now includes bounded reads into caller-owned byte slices and complete
prefix writes. The Aster-written `CopyFileBuffered` loop reuses one buffer
and relies on RAII for both handles. Its integration test copies a multi-chunk
fixture, compares SHA-256 hashes, and removes the isolated test tree.
`std.bytes` now supports bounded slice inspection and owned range copies.
The Aster-written line reader handles delimiters and lines split across
chunks, blank lines, and a final unterminated line. Building it also exposed
and fixed safe whole-local reinitialization after move.
The callback line reader adds bounded-retention processing with a typed
non-capturing handler. Integration coverage exercises cross-chunk lines,
successful early stop, callback error propagation, and RAII cleanup.
Application routing now uses an Aster-owned `List<Route>` rather than two
fixed fields. Checked copyable vector access and borrowed cleanup-managed parameters
let one router serve repeated requests without per-request cloning. Typed
GET/POST routing, path parameters, method mismatch, and fallback behavior have
integration coverage.
Type-qualified functions support inferred `self` receivers with by-value,
read-only `in`, and mutable `ref` parameter modes. Static method-call sugar
does not hide ownership transfer: consuming a cleanup-managed receiver still
invalidates the caller's value.
Half-open integer ranges provide allocation-free `for index in start..end`
loops with one-time bound evaluation, checked increments, and normal
break/continue cleanup. Text and router library traversal use the range form.
Typed `foreach (Article article in articles)` provides read-only iteration over
cleanup-managed collection elements without cloning or consuming the collection.
Optional SQLite integration now provides cleanup-managed database and prepared
statement handles, deterministic close/finalize, owned errors, typed binding,
step state, and checked copied column access. The integration test performs a
complete parameterized insert/query against an in-memory database.
The SQLite-backed issue tracker combines typed GET/POST routing, form
decoding, prepared statements, owned query results, HTML element control flow,
and the reusable server loop. Its compact SSR design explicitly tests literal
sans-serif typography, conventional blue links, issue rows, and closed-state
rendering.

The 0.2 backend foundation includes a verified typed CFG IR and an
IR-to-bytecode adapter. `run` and project execution now use it by default.
The adapter executes scalars,
fixed arrays, structs, enums, `Option`, and `Result`, including loops,
recursion, function values,
indirect/native calls, casts, cloning, aggregate mutation, and nested
destruction. `switch` and `try` execute through ordinary IR control flow,
including cleanup on propagated errors. Differential tests compare stdout,
stderr, and exit status between the typed-IR VM and generated C. Owning
iteration over fixed arrays and cleanup-managed vectors is also migrated, including
cleanup on exhaustion and `break`. Typed arena allocation, raw load/store,
null comparison, reset invalidation, and expired-pointer traps are migrated as
well. Native element builders, typed properties, child collections, function
components, escaping, and ordinary element-body control flow now execute
through the adapter too. Manifest targets retain their strict source roots and
module mapping through the adapter. Differential tests
cover module/type identity, generic specialization, project test targets, and
the multi-module documentation server's render and smoke-test applications.
Live network tests also run both the language-handler server and documentation
server through typed IR. Leak-enabled testing additionally covers consuming
clone operations on fresh owning temporaries.

The benchmark harness measures typed-IR compilation and VM execution and
counts executed bytecode instructions. Local adapter peepholes
remove redundant temporary-local round trips and fallthrough jumps without
crossing CFG boundaries.

`lang emit-c` is the first second-backend vertical slice. It emits warning-clean
portable C17 for scalars, direct functions, checked arithmetic, CFG, copyable
fixed arrays, structs, plain enums, and discriminated unions. Canonical IR
metadata includes resolved struct field types and union payload types, so the
backend never needs AST type declarations. Generated-C tests cover aggregate
parameters/results,
construction, mutation, checked indexing, generic unions, `Option`, `Result`,
`switch`, `try` propagation, deterministic cleanup-managed aggregate cleanup, and
overflow. IR types now carry verified destructor function IDs and target
size/alignment, removing backend name lookup. Unsupported
ownership/runtime types are rejected explicitly.

## Verification commands

```sh
cmake -S . -B build-gcc -DCMAKE_C_COMPILER=gcc \
  -DLANG_WARNINGS_AS_ERRORS=ON
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure

cmake -S . -B build-clang -DCMAKE_C_COMPILER=clang \
  -DLANG_WARNINGS_AS_ERRORS=ON
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=gcc \
  -DLANG_WARNINGS_AS_ERRORS=ON -DLANG_SANITIZE=ON
cmake --build build-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-sanitize --output-on-failure
```

## Aster 0.2 work in progress

Manifest projects, strict namespace mapping, named targets, whole-namespace
using declarations, namespace aliases, and explicit target layout are implemented. User-defined
generic structs and enums now use canonical concrete instantiations across
modules. Field and payload substitution drives checking, copy/move
classification, clone eligibility, deterministic nested destruction, and
layout reporting. Generic functions infer arguments from calls and expected
results, canonicalize specializations across modules, and compile independent
typed bytecode bodies, including recursive specializations. Copyable,
non-capturing function values have exact static signatures and verified
indirect calls; imported functions and generic higher-order functions are
covered.

Aster-written modules now provide generic Pair construction, Option/Result
queries, ownership-preserving List and StringBuilder composition, typed
two-route dispatch, and middleware chains over cleanup-managed Html values. The
compiler/VM still owns the low-level Option/Result tags, container storage, and
allocation primitives.

The HTTP experiment now has configurable header/body limits, read/write
timeouts, bounded Content-Length framing, body views, 413 handling, typed
callback routing/middleware, `:name` path matching and borrowed parameter
extraction, bounded sequential keep-alive, and chunked response streaming.
Keep-alive preserves one socket-owning RAII handle and is capped by time and
request count. `std.http_app` supplies typed Request, Response, Handler, and
Router values; `http_try_*` maps routine transport operations into Result
values. The server does not claim production readiness.

The `examples/docs_server` manifest project is the medium integration test. Its
model, config, pages, site composition, render binary, server binary, and test
target are separate modules. It combines generic documents, Aster-written
libraries, typed callbacks, middleware, HTML, file/directory/socket RAII,
Aster-written configuration parsing, Markdown directory traversal, and a
served CSS asset. `docs_server_http_application` launches the actual project,
checks CSS framing, reuses the connection, and checks typed HTML routing.

Pipelining, TLS, concurrency, and inbound chunked bodies remain explicit later
HTTP transport milestones.

The typed-IR bytecode VM has begun a measurement-driven performance correction.
Its hot loop no longer copies full instructions and source spans per dispatch;
function frames reuse VM-owned locals, initialization state, and operand-stack
storage instead of allocating or creating a 24 KiB C stack frame per call.
Verified local-transfer and checked-integer superinstructions remove common IR
adapter traffic. On the twenty-million-iteration backend workloads this reduced
the inline VM result from 1.974 seconds to 1.079 seconds and the call-heavy
result from 3.116 seconds to 1.743 seconds. Direct typed comparisons, compact
copy-argument calls, parameter round-trip elimination, and primitive cleanup
fast paths subsequently reduced those results to 0.618 seconds and 0.981
seconds. Direct checked signed-integer slot execution and further call-path
cleanup reduced those results to 0.495 seconds and 0.924 seconds. Splitting
the 40-byte instruction record into a 12-byte hot opcode/operand stream and a
cold exact-span stream leaves the inline result effectively unchanged at
0.498 seconds while reducing the call-heavy result to 0.858 seconds. Directly
targeting single-use numeric binary results at their final local removes
another redundant dispatch and now measures 0.427 seconds inline and 0.776
seconds with per-iteration Aster calls. Checked local/immediate arithmetic
then reduces those results to 0.370 seconds and 0.726 seconds. An iterative
call-frame prototype passed semantic tests but substantially regressed both
benchmarks and was removed. An optional computed-goto dispatch improves the
Clang 18 build to 0.353 seconds inline and 0.697 seconds with calls, but
regresses GCC 13; portable switch dispatch therefore remains the default.
A verified 64-bit packed-execution prototype regressed GCC and Clang under
both switch and computed-goto dispatch and was removed; its decode cost
outweighed the instruction-density gain.
All normal, strict-warning, computed-goto, and sanitizer suites pass. The
remaining gap to optimized C is still large and is recorded as open
performance work rather than accepted interpreter quality.

The first browser-Wasm islands milestone now uses native Aster HTML end to
end. `onclick`, `oninput`, `onchange`, and `onsubmit` accept checked public
scalar handlers; the compiler emits typed hydration metadata and the C backend
emits stable `aster_export_*` entry points. A 327-byte optimized Wasm module
drives one SSR counter and two independently scoped SSR contact forms through
a 3.6 KiB unminified runtime. Browser verification proves direct text,
`aria-invalid`, and `hidden` updates without rerendering or a virtual DOM.
The second browser milestone adds UTF-8 `string` parameters and results.
Generated wrappers flatten each input to a stable
pointer/length ABI and expose allocation, inspection, and explicit drop
functions only when an event handler needs strings. The reusable
`runtime/browser/aster.js` loader performs deterministic input and result
cleanup. The form now validates actual text and returns a personalized owned
message; Node proves 1,000 ownership cycles and Chrome proves 100 retained-DOM
submissions. The optimized Wasm is 2,254 bytes and the application-specific
loader is 94 bytes.

The third browser milestone adds island-scoped persistent scalar state without
a virtual DOM. Typed handler metadata initializes state once from SSR output;
handlers within the nearest ordinary `id` or `form` scope share it, and
separate component instances receive separate stores. Scalar results commit
to the store before direct text, `aria-expanded`, and controlled `hidden`
projections. Chrome verification deliberately corrupts rendered text and ARIA
state, then proves subsequent Aster transitions use persisted typed values
and repair the DOM. It also proves reset/increment handler sharing and two
isolated disclosure instances. The optimized Wasm is 2,326 bytes, the generic
unminified runtime is 8,023 bytes, and the application loader remains 94
bytes. Persistent aggregate state, keyed collections, and asynchronous
handlers remain later work.

The fourth browser milestone adds opaque typed struct results and persistent
keyed collections. A browser handler may return a supported Aster struct;
the C backend boxes it and generates typed scalar/string/Html field accessors
plus deterministic aggregate cleanup. Owning fields transfer exactly once.
`AddTodo` returns `{ next_id, Html item }`, so all item construction and HTML
escaping remain native Aster. The runtime initializes a keyed map from SSR
children, applies insert/replace/remove by ordinary element `id`, preserves
the collection node, and hydrates handlers inside inserted Aster HTML. Node
proves 100 aggregate/Html transfer/drop cycles. Chrome proves SSR and dynamic
removal, DOM-tamper-resistant next-ID state, escaped content, retained list
identity, and 50 repeated add/remove cycles. Current artifacts are 5,422 bytes
of Wasm, 12,494 bytes of unminified generic runtime, 94 bytes of application
loader, and 3,601 bytes of SSR HTML. Asynchronous handlers and general nested
aggregate ABI generation remain later work.

Lime Browser 0.1 promotes those retained-DOM milestones out of the isolated
trial. A binary project target can declare `browser_entry`; `project build-web`
emits its server C and produces optimized browser Wasm, the reusable runtime,
and a tiny generated loader in one output directory. The optional
`lime.browser` package emits the module tag and serves only those typed assets.
Direct owned Html handler results now join scalar, Boolean, string, and
supported patch-struct results. The live Lime integration serves SSR event
metadata, Wasm, runtime JavaScript, and the loader through generated C, while
a normal form POST proves the non-Wasm fallback. The browser suite also proves
direct Html keyed replacement without replacing its collection container.

The native HTML coverage milestone expands `std.html` from 49 to 113 source-
declared elements, covering the current conforming HTML element vocabulary and
the practical tag-specific attributes required by documents, media, forms,
tables, and interactive content. The safe `<doctype />` component keeps full
documents inside native angle syntax. Element paths now accept keyword-named
tags such as `<var>`, and `as=` attributes no longer collide with Aster cast
syntax. VM and generated C implement HTML raw-text semantics for `script` and
`style`, while ordinary text and every attribute remain context-escaped. The
full-page fixture proves doctype, metadata, responsive images, forms, tables,
keyword names, presence booleans, raw CSS/JavaScript, and identical VM/C
output. The generated-C runtime also matches raw-text and complete void-element
closing behavior; broader property lowering remains deliberately lower
priority than C and VM.

Native element bodies now accept ordinary unquoted HTML text. Static text lowers
as a distinct allocation-free typed-IR operation and is escaped by the same
context-aware policy as dynamic `{...}` children. Formatting indentation is
omitted, inline spacing is normalized, and syntactically recognized control
flow continues to parse as Aster code at child boundaries. String-valued child
expressions require braces. Both bytecode paths and generated C produce
identical output.

Typed HTTP responses now consume `Html` directly in both the VM and primary C
backend. The runtime borrows the completed value's existing contiguous buffer
for the synchronous write, then deterministically destroys the owner. This
removes the intermediate `Html`-to-`string` ownership transition without a
clone, allocation, or body copy, while retaining `Content-Length` and bounded
keep-alive. The issue-tracker HTTP integration exercises this path through both
execution modes. Construction-time socket streaming remains the separate
chunked-response facility rather than a hidden behavior of ordinary `Html`.

Native CSS now begins directly inside `<style>` without string delimiters.
Aster switches lexing modes at that element boundary and builds a source-
spanned CSS tree containing style rules, at-rules, declarations, nested rules,
and preserved values. Unknown at-rules, properties, functions, and custom
property contents remain valid and are emitted byte-for-byte, keeping the
grammar forward-compatible rather than binding it to a fixed property list.
Malformed comments, strings, delimiters, declarations, rules, and blocks are
diagnosed. The issue tracker now authors its stylesheet in this syntax, and VM
and generated-C HTTP integrations render it correctly. CSS is Aster's final
embedded parser; JavaScript in `<script>` remains deliberately unparsed.

The first native-CSS compatibility pass accepts all three canonical Nook
stylesheets unchanged (19,991 bytes after excluding byte-identical build
duplicates). A broader recovered-site corpus of 202 authored and minified
stylesheets totaling 18,181,751 bytes also parses without false rejection.
The pass added CSS Syntax compatibility for top-level CDO/CDC tokens and
semicolonless at-rules ending at EOF or a containing block, and permanent
coverage now includes layers, `:has()`, container queries/units, keyframes,
custom-property blocks, unknown at-rules, and an 8,192-declaration memory
stress case.

Component-local CSS is implemented with `<style scoped>` in `Html` functions.
The front end gives the declaring module/function a stable scope attribute,
rewrites selectors from the structural CSS tree, and both bytecode paths and
portable C add the same marker to that function's native elements. Selector
lists, nested rules, media/support/container blocks, and pseudo-elements are
covered; keyframe steps remain global to their animation declaration. Plain
styles remain global and byte-preserving. Scoping has no runtime parser or
allocation cost and does not implicitly leak through child component calls.
Static scoped styles now deduplicate per root `Html` document in both bytecode
paths and generated C, including when independently built component values are
appended later. `emit-c-site` and its project form combine reachable component
styles into one deterministically hashed asset and make generated HTML emit one
matching stylesheet link. Normal VM and C emission remain inline by default.
Dynamic component styling now uses native `--name=value` element properties.
Both bytecode paths and generated C combine them into one `style` attribute,
format numeric values directly, and reject string values capable of escaping
the custom-property value boundary. Static native CSS consumes them through
ordinary `var(--name)` syntax and remains eligible for scoping and extraction.
