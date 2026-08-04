# Local baseline: 2026-08-01

These are local whole-process wall times on an Intel Core i7-13700F with warm
filesystem/toolchain caches. They are not general language compile-time claims.

| Operation | Median | Relative context |
| --- | ---: | --- |
| Aster front end and C emission | 0.89 ms | Emits portable C |
| Go unchanged-source cached build | 38.07 ms | Reuses Go's application-package cache |
| Aster development executable (`-O0`) | 38.21 ms | Front end, GCC, and precompiled runtime link |
| Hand-written Rust release executable | 84.48 ms | Direct `rustc`, standard library precompiled |
| Go changed-package executable | 102.22 ms | Standard library cached, main package rebuilt |
| Sailfish release executable | 120.06 ms | Benchmark crate rebuilt, dependencies cached |
| Aster release executable (`-O3 -fwhole-program`) | 163.67 ms | Front end, GCC, and precompiled runtime link |

Aster's language front end is not the compile-time problem. The C backend now
omits normal and legacy render-into component variants when every use can write
directly to the final string builder. That reduces this 32-line HTML program
from 1,437 lines and 49,920 bytes to 1,167 lines and 41,794 bytes. Plain GCC
`-O3` compilation fell from roughly 547 ms to 438 ms.

The stable runtime can now be emitted once with `lang emit-c-runtime` and reused
by compiling programs with `ASTER_EXTERNAL_RUNTIME`. Hot destination-aware
builder helpers remain inline in program C, preserving Aster's runtime lead
over Sailfish, while cold string, HTML-tree, arithmetic, and routing machinery
comes from the precompiled object. This lowers the development build from about
61 ms to 38.21 ms and release from 172.31 ms to 163.67 ms. GCC still spends most
release time optimizing the large program-specific direct-render function.
