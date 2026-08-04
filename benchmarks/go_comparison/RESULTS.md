# Local baseline: 2026-08-01

These are narrow local observations, not general Aster or Go performance
claims. Every CLI implementation produced byte-identical output before timing.

Environment:

- Intel Core i7-13700F, Linux x86-64;
- GCC 13.3, Aster generated C compiled with `-O2 -DNDEBUG`;
- Go 1.22.2 linux/amd64, compiled with normal `go build` optimization;
- twenty measured CLI runs after two warmups;
- five HTTP runs of 2,000 requests after one warmup;
- HTTP concurrency one, one request per connection.

Median whole-process wall times:

| Workload | Aster C | Go | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 21.43 ms | 21.90 ms | 57.13 ms | Aster C and Go are effectively tied |
| Functions, 10M source calls | 10.82 ms | 11.51 ms | 167.45 ms | Aster C and Go are effectively tied |
| Owned strings, 300k | 12.36 ms | 33.12 ms | 55.57 ms | Aster C is 2.68x faster than Go |
| Escaped HTML, 200k | 34.54 ms | 44.14 ms | 87.45 ms | Aster C is 1.28x faster than Go |

HTTP client wall time for 2,000 requests:

| Server | Median | Relative result |
| --- | ---: | --- |
| Aster generated C | 127.52 ms | 1.68x faster |
| Go `net/http` | 213.70 ms | baseline |

The two scalar compiled results are close enough that scheduling variance can
reverse their order. The meaningful local result is parity, not a broad claim
that either compiler is faster. Both compilers retain their normal inlining
policy for the function workload.

The string workload uses an idiomatic pre-grown `strings.Builder`,
`strconv.FormatInt`, and `strconv.FormatBool` in Go. The HTML workload uses the
same builder strategy plus `html.EscapeString`; Aster uses native typed HTML
with destination-aware escaping. Both materialize a fresh owned result on each
iteration.

Aster's VM is slower than compiled Go, as expected for the second-priority
interpreter path: 2.60x on logic, 14.55x on functions, 1.68x on strings, and
1.98x on HTML in this run. Generated C remains Aster's primary backend and is
competitive with or faster than Go across all four workloads.

The HTTP result compares Aster's experimental synchronous server with Go's
general-purpose standard server under a deliberately single-connection,
no-keep-alive client. It does not predict production behavior under concurrent
load, persistent connections, TLS, reverse proxies, or realistic application
work.
