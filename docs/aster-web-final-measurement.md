# Final Aster Web retained-DOM measurement

Measurement date: 2026-08-07. Control: Vue
3.5.41 production ESM runtime. Browser: system Google Chrome in headless mode
with `--no-sandbox`. Both clients execute the same table and async behavior from
`examples/browser_compare`.

## Protocol

- Seven fresh pages per client; medians are reported without discarding a
  warm-up sample.
- Startup is navigation start to an application-owned ready timestamp.
- Hydration/mount is ready time minus `domContentLoadedEventEnd`.
- Operations create 1,000 keyed rows, update every tenth row, swap rows 1 and
  998, append 1,000 rows, delete row 500, and clear 1,999 rows.
- Async measurement starts a 25 ms transition and immediately supersedes it
  with a 1 ms transition; the latest result must commit and the stale result
  must not overwrite it.
- Heap values use Chrome CDP garbage collection and `JSHeapUsedSize`.
- Collection memory repeats create/clear three times after the main sequence.
- Mount memory creates and removes ten same-origin application iframes.
- Aster linear memory is the active `WebAssembly.Memory.buffer.byteLength`.
- Payload totals include the framework/runtime, application client, and Wasm
  where applicable. Gzip compresses each network artifact independently.
- Build latency wraps consecutive `lang project build-web` invocations with a
  nanosecond wall clock. Vue uses direct production browser modules in this
  fixture and therefore has no equivalent build measurement.

## Results

| measurement | Aster | Vue |
|---|---:|---:|
| startup to ready | **17.8 ms** | 20.7 ms |
| post-DOM hydration/mount | 3.8 ms | **0.0 ms** |
| create 1,000 | 15.0 ms | **11.8 ms** |
| update every tenth | 7.2 ms | **5.1 ms** |
| swap two rows | 2.8 ms | **2.7 ms** |
| append 1,000 | 13.1 ms | **8.2 ms** |
| delete one row | 5.1 ms | **3.5 ms** |
| clear 1,999 | 7.5 ms | **7.0 ms** |
| latest async completion | 1.8 ms | **1.3 ms** |
| JS heap delta, 3 create/clear cycles | 98.0 KB | **68.2 KB** |
| JS heap delta, 10 mount/unmount cycles | **112.6 KB** | 148.4 KB |
| active Wasm linear memory | 5.4 MiB | n/a |
| raw client payload | **85.2 KB** | 112.7 KB |
| gzip client payload | **21.8 KB** | 42.2 KB |
| clean Aster web build | 319.7 ms | n/a |
| repeated Aster web build | 329.4 ms | n/a |

## Findings

The retained client remains substantially smaller and startup is comparable.
Mount/unmount JS retention is lower in this run. Reorder and clear are now at
parity, and delete is within 1.5x. Vue remains 1.4x faster for sparse update and
1.6x faster for append, but the previous order-of-magnitude structural gap is
gone.

The benchmark found allocator fragmentation during repeated collection cycles:
the previous free list exhausted the 16 MB Wasm maximum on the second cycle.
The browser allocator now keeps free blocks address-sorted, coalesces adjacent
blocks, and reuses the top of the heap. The validated run completes repeated
cycles with a 5.4 MiB active linear-memory high-water mark in this run.

The compiler now records private `List<T>` mutation metadata while a synchronous
component handler runs. The browser validates the generated list ABI and DOM
layout, executes `Render()` to preserve component semantics, and applies a
sparse update, reorder, removal, clear, or append plan when that proof succeeds.
Append renders only the newly added keyed suffix. Transformed projections and
unsupported mutation combinations deliberately fall back to the complete
snapshot path. This introduces no public DOM command, projection batch, VDOM,
or runtime signal API.

Against the previous retained-snapshot baseline, update fell from 33.9 ms to
7.2 ms, swap from 31.8 ms to 2.8 ms, append from 48.7 ms to 13.1 ms, and delete
from 63.5 ms to 5.1 ms. The larger internal runtime/ABI increases gzip payload
from 17.5 KB to 21.8 KB, still about half the Vue fixture.

## Release validation

After the targeted browser, Wasm, generated-C, strict-warning, application, and
measurement tests, the complete release matrix passed:

```text
100% tests passed, 0 tests failed out of 470
Total Test time (real) = 158.26 sec
```
