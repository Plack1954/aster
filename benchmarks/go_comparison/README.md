# Go comparison benchmarks

These workloads compare Aster with compiled Go on a deliberately narrow
application surface: integer logic, function calls, owned string construction,
escaped typed HTML, and synchronous HTTP.

Run all workloads from the Aster repository root:

```sh
./benchmarks/go_comparison/run.sh
```

The runner requires Go, CMake, a C17 compiler, Hyperfine, ApacheBench, Curl,
and jq. It compiles ordinary Go executables with the installed production Go
toolchain, builds Aster generated C with `-O2 -DNDEBUG`, and executes Aster's
typed-IR VM. Every timed implementation must emit byte-identical output first.
Raw results are stored beneath `build-go-comparison/results/`.

## Workload contract

- `logic` executes the same twenty-million-iteration recurrence.
- `functions` executes ten million source-level calls to the same recurrence
  function. Go and generated C retain their normal compiler inlining policy.
- `strings` constructs 300,000 equivalent owned records and sums byte lengths.
- `html` renders 200,000 equivalent escaped cards into owned strings.
- `http` renders the same dynamic HTML response per request at concurrency one.

Aster arithmetic remains checked. Go's ordinary signed integer operations
use Go's defined two's-complement behavior; the benchmark values stay in range,
so both implementations produce the same result, but the language semantics
are not identical on overflow.

The HTTP comparison uses Go's standard `net/http` server and Aster's
experimental synchronous generated-C server. It is a narrow local stack
comparison, not a general production-capacity claim. ApacheBench uses one
request per connection unless its configuration is changed.
