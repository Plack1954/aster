# Compile-time comparison

This benchmark measures executable production for the equivalent HTML
workloads already used by the Go and Rust comparisons. It separates Aster's
front end from downstream C compilation and distinguishes a cached Go build
from a build where the command-line application package is forced to compile.

Run it from the repository root:

```sh
./benchmarks/compile_comparison/run.sh
```

The runner requires CMake, a C17 compiler, Go, Rustc, Cargo, Hyperfine, and jq.
Language standard libraries and Rust dependencies are already installed or
compiled before timing. It does not measure toolchain installation, dependency
downloads, or a completely cold machine.

The Aster timings precompile the stable runtime once with
`lang emit-c-runtime`. Program translation units are then compiled with
`ASTER_EXTERNAL_RUNTIME` and linked to that object. Plain `emit-c` output
remains standalone when that define is absent.

The primary commands produce executables:

- Aster development: emit portable C, compile it with `-O0`, and link the
  precompiled runtime;
- Aster release: emit portable C and compile it with `-O3`; when the selected
  C compiler supports `-fwhole-program`, use it because the translation unit
  program is a complete executable rather than a linkable library, then link
  the precompiled runtime;
- Go changed package: use a unique command-line package identity each run so
  the application package cannot be reused from Go's build cache;
- hand-written Rust: invoke `rustc -C opt-level=3`;
- Sailfish Rust: remove and rebuild only the benchmark crate in Cargo release
  mode while retaining compiled dependencies.

Go's unchanged-source cached build and Aster's front end are reported
separately. Every produced executable must print the same bytes.
