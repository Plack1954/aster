# Local baseline: 2026-08-01

These are narrow local results on an Intel Core i7-13700F with GCC 13.3 and
current stable Nim 2.2.10. Every executable produced byte-identical output.

The strongest observed Nim result from ARC/ORC release/danger configurations is
shown beside the existing Aster medians:

| Workload | Aster C | Best Nim 2.2.10 | Nim configuration |
| --- | ---: | ---: | --- |
| Logic, 20M iterations | 14.50 ms | 14.24 ms | ARC danger |
| Functions, 10M calls | 7.14 ms | 7.88 ms | ORC danger |
| Owned strings, 300k | 9.45 ms | 15.46 ms | ORC danger |
| Escaped HTML, 200k | 8.07 ms | 17.29 ms | ARC release |

Modern tuned Nim is effectively tied with Aster on logic and close on the
function workload. Aster remains 1.64x faster on owned strings and 2.14x on
HTML. The HTML advantage is the meaningful language-specific result: Aster
lowers typed HTML directly into a destination-aware builder, while Nim is
executing general string code.

Nim's full configuration matrix:

| Workload | ARC release | ORC release | ARC danger | ORC danger |
| --- | ---: | ---: | ---: | ---: |
| Logic | 17.65 ms | 17.08 ms | 14.24 ms | 14.93 ms |
| Functions | 14.76 ms | 15.24 ms | 8.15 ms | 7.88 ms |
| Strings | 16.54 ms | 16.52 ms | 15.95 ms | 15.46 ms |
| HTML | 17.29 ms | 17.42 ms | 18.99 ms | 18.17 ms |

Compile time for the equivalent HTML program with caches warm:

| Build | Median |
| --- | ---: |
| Aster development executable | about 38 ms |
| Nim 2.2.10 ARC danger executable | 157.1 ms |
| Nim 2.2.10 ARC release executable | 159.9 ms |
| Aster release executable | about 164 ms |

Aster development builds are about four times faster. Nim and Aster full
optimized builds are effectively in the same class on this small program.
