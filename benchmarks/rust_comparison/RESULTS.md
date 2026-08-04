# Local baseline: 2026-08-01

These are narrow local observations, not general Aster or Rust performance
claims. Every CLI implementation and both HTTP servers produced byte-identical
bodies before timing.

Environment:

- Intel Core i7-13700F, Linux x86-64;
- GCC 13.3, Aster generated C compiled with `-O3 -DNDEBUG`;
- Rustc 1.98.0-nightly, compiled with `-C opt-level=3 -C debuginfo=0`;
- Sailfish 0.11.2, compiled with Cargo's release profile;
- twenty measured CLI runs after two warmups;
- five HTTP runs of 2,000 requests after one warmup;
- HTTP concurrency one, one request per connection.

Median whole-process wall times:

| Workload | Aster C | Hand-written Rust | Sailfish | Aster VM | Observation |
| --- | ---: | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 14.01 ms | 14.02 ms | — | 44.21 ms | Compiled results are effectively tied |
| Functions, 10M source calls | 8.84 ms | 8.42 ms | — | 122.36 ms | Rust is 1.05x faster than Aster C |
| Owned strings, 300k | 9.43 ms | 15.13 ms | — | 35.71 ms | Aster C is 1.60x faster than Rust |
| Escaped HTML, 200k | 8.32 ms | 14.85 ms | 8.97 ms | 64.53 ms | Aster C is 1.08x faster than Sailfish |

HTTP client wall time for 2,000 requests:

| Server | Median | Relative result |
| --- | ---: | --- |
| Rust synchronous `TcpListener` | 66.65 ms | effectively tied |
| Aster generated C | 66.81 ms | effectively tied |

The compiled logic results are effectively tied, Rust narrowly wins the
function case, and Aster generated C wins the owned-string case. Both compiled
paths retain normal inlining. Aster keeps
checked integer operations; Rust's release build normally omits overflow
checks. All benchmark values remain in range, but the semantic cost is not
identical.

The Rust string workload uses a 64-byte `String` and `std::fmt::Write`. Its HTML
workload uses a 128-byte `String`, direct writes for known-safe markup, and a
small text-context escaping loop. Aster uses native typed HTML with the same
dynamic escaping and output. The C backend now recognizes a directly rendered
component and lowers its typed element operations into one destination string
builder. The earlier owner/tree path measured 42.43 ms; destination-aware
lowering reduced that workload to 17.29 ms without changing Aster source.
This remains a comparison with hand-written Rust, not a Rust template framework
or component library. The first contemporaneous run against an actual Sailfish
0.11.2 compiled template measured 19.51 ms for Aster, 16.73 ms for
hand-written Rust, and 10.94 ms for Sailfish. Applying the concrete lessons from
that comparison changed Aster's direct renderer to select an initial capacity
from its statically known markup, keep that initial storage inline with its
owner, reserve escaped output once, and expose internal append helpers for C
inlining. At the same `-O2` setting, the follow-up measured 11.21 ms for Aster,
13.61 ms for hand-written Rust, and 8.14 ms for Sailfish. Correcting the runner
to give generated C the same release-level `-O3` optimization as Rust produced
8.23 ms for Aster, 14.06 ms for hand-written Rust, and 8.86 ms for Sailfish in
a reversed-order 150-run check. The complete twenty-run suite reproduced the
result at 8.32 ms for Aster, 8.97 ms for Sailfish, and 14.85 ms for hand-written
Rust. Aster is 1.08x faster than Sailfish and 1.79x faster than the hand-written
Rust implementation on this narrow workload.

The Rust HTTP program is a hand-written, blocking standard-library HTTP/1.1
handler because Rust has no standard HTTP server. Aster's server is likewise
synchronous and experimental. The result only describes this one-request-per-
connection transport case; it says nothing about frameworks, concurrency,
keep-alive, TLS, proxies, or production workloads.

Aster's second-priority VM is slower than hand-written compiled Rust: 3.15x on
logic, 14.53x on functions, 2.36x on strings, and 4.35x on HTML in these runs.
Generated C remains the relevant primary-backend comparison: it effectively
ties logic, loses functions narrowly, and wins strings and HTML.
