# Rust comparison benchmarks

These workloads compare Aster with compiled Rust on a deliberately narrow
application surface: integer logic, function calls, owned string construction,
escaped typed HTML, and synchronous HTTP.

Run all workloads from the Aster repository root:

```sh
./benchmarks/rust_comparison/run.sh
```

The runner requires Rustc, Cargo, CMake, a C17 compiler, Hyperfine,
ApacheBench, Curl, and jq. Hand-written Rust programs are compiled with
`rustc -C opt-level=3`, corresponding to Cargo's release optimization level.
The HTML workload additionally builds Sailfish 0.11.2 in Cargo release mode as
a compiled-template comparison. Aster generated C uses `-O3 -DNDEBUG`, and
the typed-IR VM is measured separately. All CLI and HTTP bodies must be
byte-identical before timing. Raw data is stored beneath
`build-rust-comparison/results/`.

## Workload contract

- `logic` executes the same twenty-million-iteration recurrence.
- `functions` executes ten million source-level calls to the same recurrence
  function. Rust and generated C retain normal compiler inlining.
- `strings` constructs 300,000 equivalent owned records and sums byte lengths.
- `html` renders 200,000 equivalent escaped cards into owned strings using
  Aster, hand-written Rust, and a statically compiled Sailfish template.
- `http` renders the same dynamic HTML response per request at concurrency one.

Aster arithmetic remains checked. Rust release arithmetic normally omits
overflow checks; the benchmark values stay in range, so outputs match, but the
language behavior and cost on overflow are not equivalent.

Rust's standard library has no HTTP server framework. Its HTTP workload is an
explicit synchronous `TcpListener` HTTP/1.1 handler, comparable in scope to
Aster's experimental synchronous server. This is a narrow transport baseline,
not a production-web-server claim. ApacheBench makes one request per connection
unless its configuration is changed.
