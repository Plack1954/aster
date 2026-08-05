# Aster performance-ceiling benchmarks

This suite gives Aster generated C and the typed-IR VM an explicit performance
target. Each workload has a hand-written C implementation compiled with the
same C compiler and optimization flags. Its median wall time is the workload's
**time floor** (and therefore its throughput ceiling); results report how many
times slower each Aster backend is than that floor.

Run the suite from anywhere in the repository:

```sh
./benchmarks/performance_ceiling/run.sh
```

The runner requires CMake, a C17 compiler, Hyperfine, and jq. It validates
byte-identical output before measuring, uses release builds and `-O3
-march=native`, and writes raw Hyperfine JSON beneath
`build-performance-ceiling/results/`. By default the complete run is placed in
a systemd unit limited to 2 GiB of RAM with swap disabled.

Useful controls:

```sh
ASTER_CEILING_BENCH_RUNS=20 ./benchmarks/performance_ceiling/run.sh
ASTER_BENCH_MEMORY_MAX=3G ./benchmarks/performance_ceiling/run.sh
ASTER_CEILING_BENCH_BUILD=/tmp/aster-ceiling ./benchmarks/performance_ceiling/run.sh
```

## Workloads and optimization questions

| Workload | What it isolates | C floor strategy | Aster optimization signal |
| --- | --- | --- | --- |
| `arithmetic` | Tight checked integer control flow | Direct scalar loop | Cost of checked arithmetic and generated branches |
| `function_calls` | Source-level abstraction in a hot loop | Inlineable helper | Whether generated functions optimize away cleanly |
| `list_growth` | Generic list growth, indexing, and cleanup | One exact allocation and direct indexing | Collection layout, growth, bounds checks, and destruction |
| `owned_strings` | Interpolation, allocation, formatting, and destruction | Direct formatted-length calculation with no allocation | Owned-string and formatting overhead |

The C programs are deliberately best-case targets, not claims of identical
language semantics. In particular, `list_growth.c` knows the final capacity,
and `owned_strings.c` computes the observable formatted length directly instead
of constructing strings. Those differences are intentional: they establish the
best plausible time to chase and make the remaining abstraction cost visible.

Do not treat a single run as a release gate. Track medians on a quiet machine,
keep the compiler and CPU fixed, and investigate persistent ratio changes per
workload. The absolute number is machine-specific; the Aster-to-C ratio is the
primary signal.
