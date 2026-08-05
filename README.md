# Aster

Aster is an experimental, compact C-like systems language implemented in
portable C17. Verified typed IR is its semantic backend boundary. Portable C17
output is the primary implementation target, and the bytecode VM is the fast
development and differential-testing path.

Aster does not use a garbage collector, LLVM, or a custom machine-code/JIT
backend. Immutable strings use reference counting; general objects and
resources do not.

## Project thesis

> Aster is a deterministic application language with explicit costs, simple
> cleanup, first-class typed HTML, excellent generated C, and a fast bytecode
> VM development loop.

The project is deliberately not trying to win a feature-count comparison with
V, Go, Rust, or a mature web ecosystem. It is trying to make deterministic,
production-shaped command-line and synchronous server applications unusually
coherent: visible allocation and cloning, predictable cleanup, portable C
deployment, and fast VM feedback from the same typed IR.

New features must be justified by real Aster programs, have an explainable
cost model, lower explicitly through typed IR, and work correctly in generated
C and the VM. See the complete [Aster thesis](docs/thesis.md).

## Build and use

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/lang run examples/hello.lang
./build/lang run examples/html.lang
ASTER_PROCESS_TEST=demo \
  ./build/lang run examples/process.lang -- first-argument
./build/lang run examples/cli.lang -- \
  --verbose --output=report.txt input.lang
./build/lang project test examples/testing_project/aster.toml
# Copy a file through a bounded 1 KiB Aster buffer:
./build/lang run examples/file_copy.lang -- input.bin output.bin
# Read arbitrarily long LF-delimited lines through a 31-byte buffer:
./build/lang run examples/read_lines.lang -- input.txt
./build/lang run examples/for_each_line.lang -- input.txt
./build/lang run examples/sqlite.lang
./build/lang project run examples/issue_tracker/aster.toml render
./build/lang project test examples/issue_tracker/aster.toml
```

Clang builds can optionally enable the measured computed-goto VM dispatch:

```sh
cmake -S . -B build-computed \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DLANG_VM_COMPUTED_GOTO=ON
cmake --build build-computed
```

The portable switch VM remains the default. Computed goto is deliberately
opt-in because it improved the current Clang benchmark but regressed GCC.

Backend output consumes the same verified typed IR:

```text
./build/lang emit-c examples/c_backend.lang > generated.c
cc -std=c17 -O2 generated.c -o generated
./generated
./build/lang project emit-c examples/issue_tracker/aster.toml render > issue-render.c
./build/lang project build-web examples/wasm_counter/aster.toml \
    examples/wasm_counter/dist counter
```

Production C builds can extract scoped component CSS while VM development and
ordinary C emission remain inline:

```text
mkdir -p public/assets
./build/lang emit-c-site app.lang public/assets > app.c
```

The C backend is advanced first as real applications expose missing runtime or
ABI support. VM parity comes next. Current limits are documented in
`docs/c-backend.md`.

Sanitized development build:

```sh
cmake -S . -B build-asan -DLANG_SANITIZE=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
```

The executable supports `run`, `run-ir`, `check`, `dump-tokens`, `dump-ast`,
`dump-types`, `dump-layout`, `dump-ir`, `dump-ir-bytecode`, `dump-bytecode`,
`emit-c`, `repl`, `test`, and `bench`.

Aster projects support manifest-defined source roots, strict namespace-to-file
mapping, named binary/library/test targets, and selective or aliased imports:

```sh
./build/lang project run tests/project/aster.toml
./build/lang project run-ir tests/project/aster.toml
./build/lang project check tests/project/aster.toml math
./build/lang project emit-c tests/project/aster.toml app > app.c
./build/lang project test tests/project/aster.toml
```

See [projects and targets](docs/projects.md).
Standard-library imports do not depend on the launch directory: installed
toolchains find `share/aster/std` relative to `bin/lang`, portable bundles may
place `std` beside `bin`, manifests may set `stdlib`, and
`ASTER_STDLIB_PATH` provides an explicit override.
See the [Aster 0.3 direction and status](docs/v2-roadmap.md).
See the accepted [C#-shaped async/await design and implementation status](docs/async.md).

The medium-sized [documentation server example](examples/docs_server/README.md)
combines manifest targets, generic Aster libraries, typed callbacks, HTML,
file/directory/socket RAII, Aster-written configuration parsing and traversal,
static assets, and the bounded HTTP FFI.

The bounded [browser-Wasm counter trial](examples/wasm_counter/README.md)
compiles one Aster source through generated C for both native server-rendered
HTML and a freestanding browser WebAssembly state transition. A tiny loader
hydrates native-HTML markers and updates one text slot without a virtual DOM.

`lang dump-layout file.lang` prints the explicit host target description,
aggregate sizes, alignments, field offsets, plain-enum representation, and
discriminated-union payload layout.
See [target data layout](docs/data-layout.md).
See the [.NET-referenced standard-library API map](docs/standard-library-api-map.md).
`lang dump-ir file.lang` prints the typed control-flow IR, including explicit
ownership cleanup, aggregate operations, raw pointers, iteration, and native
element builders.
See [typed IR](docs/ir.md).
See the [portable C backend](docs/c-backend.md).
See the [backend architecture](docs/architecture.md).

`lang run` now executes the verified typed IR through the IR-to-bytecode
adapter. `lang dump-ir-bytecode file.lang` disassembles that bytecode.
`run-ir` remains an explicit alias for scripts that want to name the backend.
There is no separate AST-to-bytecode execution path.

## Implemented surface

- UTF-8 source bytes with ASCII identifiers, nested block comments, spans, and
  line/column diagnostics
- functions, typed parameters/results, locals, mutability, statement and
  expression forms of `if` and `switch`, `while`,
  `for`, cleanup-aware `break`/`continue`, arrays, structs, concrete enum
  constructors, exhaustive payload `switch`, calls, and recursion
- user-defined generic structs and enums with canonical monomorphic
  instantiations, substitution-aware ownership/destruction, and applied-type
  layouts
- inferred generic functions with canonical cross-namespace specialization,
  recursive specialization, and ownership-aware typed bytecode bodies
- typed non-capturing function values, including generic higher-order
  functions, imported callbacks, and verified indirect bytecode calls
- Aster-written generic `Pair`, Option/Result operations, ownership-preserving
  vector/text helpers, a typed function-value router, and middleware chains
- checked typed integer arithmetic and shifts, with trapped overflow, invalid
  shift counts, division by zero, and index errors
- mutable direct struct fields and array indexes with deterministic replacement
  cleanup
- typed `null` raw pointers, pointer equality, and trapped null/expired loads
- stack bytecode, stable disassembly, call frames, source-aware traps, and
  control-flow-aware bytecode stack verification
- value-copying `Buffer` and `Html` with reverse-order cleanup instructions
- compiler-known `Result<T, E>` and cleanup-aware `try` propagation
- immutable reference-counted `string`, mutable `StringBuilder`, typed `Url`,
  and value-copying `List<T>`
- `$"..."` string interpolation with direct, context-escaped writes in HTML
  text and attributes and ordinary `string` results elsewhere
- source-aware `panic`/`trap` with `never` control-flow typing
- file imports with duplicate suppression, `public` visibility enforcement,
  namespace-cycle diagnostics, namespace-qualified type/destructor identity, and
  ambiguous-public-symbol diagnostics
- noncopyable arenas with allocation/reset and typed raw pointers;
  bounded interpreter loads/stores require `unsafe`
- source-level `extern` functions, registered native calls, and opaque shared
  native handles with C destructors, explicit `ref` parameters, mutable byte
  slices, and public Result-value constructors
- Result-returning file open/read/write primitives with RAII stream closure
- source-declared typed HTML elements, native dashed/keyword attribute names,
  conditional `Option` attributes, HTML-presence booleans, global
  `data-*`/`aria-*`, required/optional/unknown/duplicate property checks,
  matching closing tags, wrapper-free `<>...</>` fragments, ordinary
  control-flow AST nodes in bodies, the current conforming HTML element
  vocabulary, safe `<doctype />`, escaped SSR, correct `script`/`style`
  raw-text behavior, native unquoted CSS inside `<style>` with structural
  parsing and unknown-syntax pass-through, compile-time component CSS via
  `<style scoped>`, and explicit `Html.UnsafeRaw`
- blocking HTTP/1.1 experiment with shared server/request handles, bounded
  request parsing, configured header/body limits and socket timeouts, compact
  method/path/header/body views, typed Request/Response/Handler routing,
  Result-returning transport operations, path parameters, bounded sequential
  keep-alive, typed middleware, chunked response streaming, static CSS
  responses, and escaped typed-HTML responses that consume the completed
  `Html` buffer directly without an intermediate string allocation

The current direction still leaves capturing closures, raw-pointer arithmetic,
and dependency version solving for later milestones. See
[language status](docs/language.md) and
[decisions](docs/decisions.md). Unsupported syntax is rejected; it is not
silently assigned different semantics.

## Layout

`include/lang/lang.h` is the embedding API. The implementation is split into
common infrastructure, lexer, parser, checker, compiler, and VM modules in
`src/`. Examples are executable programs; tests contain positive and
diagnostic cases. `packages/lime` is the first separately bounded framework
package, built as ordinary Aster code above the HTTP application primitives.
The `std/` files record the intended language-level standard
library boundary while its working primitives currently live in C.
Reusable library semantics are progressively moving into Aster; allocation,
tag storage, and operating-system calls remain explicit runtime primitives.

`lang bench` reports lexer, parser, and checker throughput plus side-by-side
legacy-direct and typed-IR compilation time, VM time, and dynamically executed
instruction counts for arithmetic/call and HTML-construction workloads.
The reproducible VM/generated-C execution and native-build comparison can be
rerun with `./benchmarks/run_backends.sh`.
The correctness-checked PHP comparison covers logic, function calls, owned
strings, typed HTML, and synchronous HTTP:
`./benchmarks/php_comparison/run.sh`.

The HTTP experiment is described in [docs/http.md](docs/http.md). To run the
reusable language-defined router:

```sh
./build/lang run examples/http_server.lang
# In another terminal, use the port printed by the server:
curl http://127.0.0.1:PORT/
```

The requirement-by-requirement release evidence is maintained in
[docs/status.md](docs/status.md).
