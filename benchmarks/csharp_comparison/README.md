# Aster versus C# benchmarks

This suite compares Aster's generated native C with optimized .NET 10 C# over
fourteen process workloads and an end-to-end HTTP workload. It is intended to
find optimization work, not produce one headline score.

```sh
./benchmarks/csharp_comparison/run.sh
```

The runner builds Aster in Release mode, publishes the C# project in Release
mode, compiles generated C with `-O3 -march=native`, verifies identical output,
and then measures both executables with Hyperfine. .NET tiered compilation is
disabled so hot methods receive optimized JIT code immediately. The measured
C# command still includes CLR startup and JIT cost; workloads are deliberately
large enough to keep that from dominating most results.

Required tools are CMake, a C17 compiler, the .NET 10 SDK, Hyperfine, jq,
Curl, and ApacheBench (`ab`). The HTTP tools are unnecessary when HTTP is
disabled.

By default the complete build and benchmark runs in a systemd unit limited to
2 GiB of RAM with swap disabled. Raw JSON and generated C are written beneath
`build-csharp-comparison/results/`.

## Coverage

| Workload | Area measured |
| --- | --- |
| `arithmetic` | Checked integer arithmetic and tight loops |
| `function_calls` | Inlineable source-level abstraction |
| `branches` | Branch-heavy control flow, modulo, and bitwise work |
| `list_growth` | Dynamic growth, writes, indexed reads, cleanup |
| `list_scan` | Repeated indexed traversal over reserved storage |
| `dictionary` | Integer hashing, insertion, lookup, and removal |
| `hash_set` | Set insertion, membership, and removal |
| `queue` | Reserved FIFO enqueue and dequeue throughput |
| `owned_strings` | Interpolation, numeric formatting, allocation, cleanup |
| `string_builder` | Incremental text construction and final materialization |
| `text_search` | Prefix, suffix, substring, and ordinal search operations |
| `json_parse` | Repeated DOM parsing and typed property access |
| `exceptions` | Construction, throwing, matching, and catching failures |
| `html_render` | Escaped dynamic HTML construction and materialization |
| `http` | Single-connection HTTP request and dynamic HTML response throughput |

## Controls

```sh
# Faster smoke run of selected workloads
ASTER_CSHARP_BENCH_RUNS=3 \
ASTER_CSHARP_WORKLOADS="arithmetic list_growth json_parse" \
./benchmarks/csharp_comparison/run.sh

# Include the typed-IR VM as a third measured backend
ASTER_CSHARP_INCLUDE_VM=1 ./benchmarks/csharp_comparison/run.sh

# Skip HTTP, or alter its sample/request counts
ASTER_CSHARP_INCLUDE_HTTP=0 ./benchmarks/csharp_comparison/run.sh
ASTER_CSHARP_HTTP_RUNS=10 ASTER_CSHARP_HTTP_REQUESTS=20000 \
./benchmarks/csharp_comparison/run.sh

# Change the bounded build location or memory limit
ASTER_CSHARP_BENCH_BUILD=/tmp/aster-csharp \
ASTER_BENCH_MEMORY_MAX=3G \
./benchmarks/csharp_comparison/run.sh
```

## Interpretation

The runner reports each median as a ratio to C#. A value below `1.0x C#`
means Aster was faster; a value above it means Aster was slower. Track each
workload independently because averaging unrelated operations hides the exact
optimization opportunity.

These are equivalent application-level algorithms using each platform's
normal public collections, strings, JSON APIs, exceptions, and HTML approach.
They are not claims of identical runtime semantics: Aster uses deterministic
manual memory management, while C# uses a tracing GC and JIT compilation. That
difference is part of what the suite is intended to measure.
