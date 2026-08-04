# Nim comparison

This benchmark compares equivalent Aster and Nim logic, function-call,
owned-string, and escaped-HTML workloads. Every result is
checked byte-for-byte before timing.

Run from the repository root:

```sh
./benchmarks/nim_comparison/run.sh
./benchmarks/nim_comparison/run_compile.sh
```

Set `NIM` to select a compiler. `NIM_MODE` selects `release` or `danger`, and
`NIM_MM` selects `arc` or `orc`. The documented baseline uses Nim 2.2.10 with
ARC. For example:

```sh
NIM_MODE=danger NIM_MM=orc ./benchmarks/nim_comparison/run.sh
```
