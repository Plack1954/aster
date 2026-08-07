# Final Lime retained-DOM measurement

Measurement date: current repository release-validation run. Control: Vue
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
| startup to ready | **17.6 ms** | 17.8 ms |
| post-DOM hydration/mount | 3.8 ms | **0.0 ms** |
| create 1,000 | 19.1 ms | **11.5 ms** |
| update every tenth | 33.9 ms | **4.8 ms** |
| swap two rows | 31.8 ms | **2.6 ms** |
| append 1,000 | 48.7 ms | **7.6 ms** |
| delete one row | 63.5 ms | **4.3 ms** |
| clear 1,999 | 6.8 ms | **6.7 ms** |
| latest async completion | 6.1 ms | **1.2 ms** |
| JS heap delta, 3 create/clear cycles | 93.1 KB | **68.4 KB** |
| JS heap delta, 10 mount/unmount cycles | **112.6 KB** | 148.4 KB |
| active Wasm linear memory | 7.5 MB | n/a |
| raw client payload | **61.5 KB** | 112.7 KB |
| gzip client payload | **17.5 KB** | 42.2 KB |
| clean Aster web build | 314.1 ms | n/a |
| repeated Aster web build | 321.8 ms | n/a |

## Findings

The retained client remains substantially smaller and startup is comparable.
Mount/unmount JS retention is lower in this run. Clear is effectively tied.
Vue is decisively faster for update, reorder, append, delete, and latest async
completion.

The benchmark found allocator fragmentation during repeated collection cycles:
the previous free list exhausted the 16 MB Wasm maximum on the second cycle.
The browser allocator now keeps free blocks address-sorted, coalesces adjacent
blocks, and reuses the top of the heap. The validated run completes repeated
cycles with a 7.5 MB active linear-memory high-water mark.

The principal remaining performance problem is architectural but internal:
ordinary class mutation currently renders and plans a complete keyed list.
Future work should generate sparse structural deltas from compiler-known
collection mutations without restoring public DOM commands, projection batches,
or a general VDOM.

## Release validation

After the targeted browser, Wasm, generated-C, strict-warning, application, and
measurement tests, the complete release matrix passed:

```text
100% tests passed, 0 tests failed out of 470
Total Test time (real) = 345.73 sec
```
